#include "save_manager.hpp"
#include "config_manager.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <dirent.h>
#include <sys/stat.h>

#if defined(__3DS__) || defined(_3DS)
#include <3ds.h>

struct smdh_title_s {
    u16 shortDescription[64];
    u16 longDescription[128];
    u16 publisher[64];
};

struct smdh_s {
    char magic[4];
    u16 version;
    u16 reserved;
    smdh_title_s titles[16];
    u8 ratings[16];
    u32 region;
    u32 matchMakerId;
    u64 matchMakerBitFlag;
    u32 flags;
    u16 eulaVersion;
    u16 reserved2;
    u32 optimalBannerFrame;
    u32 streetpassId;
    u64 reserved3;
    u8 smallIcon[0x480];
    u8 largeIcon[0x1200];
};
#endif

namespace RomM3DS {

namespace {

#if defined(__3DS__) || defined(_3DS)
std::string utf16ToUtf8(const u16* src, size_t maxLen) {
    std::string out;
    for (size_t i = 0; i < maxLen && src[i] != 0; ++i) {
        if (src[i] < 0x80) {
            out.push_back(static_cast<char>(src[i]));
        } else {
            out.push_back('?');
        }
    }
    return out;
}
#endif

} // namespace

SaveManager::SaveManager() = default;
SaveManager::~SaveManager() = default;

std::string SaveManager::formatTitleId(uint64_t titleId) {
    std::ostringstream ss;
    ss << std::uppercase << std::setfill('0') << std::setw(16) << std::hex << titleId;
    return ss.str();
}

uint64_t SaveManager::parseTitleId(const std::string& hexStr) {
    return std::strtoull(hexStr.c_str(), nullptr, 16);
}

#if defined(__3DS__) || defined(_3DS)

std::string SaveManager::getTitleNameFromSmdh(uint64_t titleId, uint8_t mediaType) {
    u32 archPath[3] = {
        static_cast<u32>(titleId & 0xFFFFFFFF),
        static_cast<u32>((titleId >> 32) & 0xFFFFFFFF),
        mediaType
    };
    FS_Path apath = { PATH_BINARY, 12, archPath };
    FS_Path fpath = fsMakePath(PATH_ASCII, "/icon");

    Handle file;
    Result res = FSUSER_OpenFileDirectly(&file, ARCHIVE_SAVEDATA_AND_CONTENT, apath, fpath, FS_OPEN_READ, 0);
    if (R_FAILED(res)) {
        return "";
    }

    smdh_s smdh;
    u32 bytesRead = 0;
    res = FSFILE_Read(file, &bytesRead, 0, &smdh, sizeof(smdh_s));
    FSFILE_Close(file);

    if (R_SUCCEEDED(res) && bytesRead >= sizeof(smdh_s) && memcmp(smdh.magic, "SMDH", 4) == 0) {
        u8 lang = 1;
        CFGU_GetSystemLanguage(&lang);
        if (lang > 15) {
            lang = 1;
        }

        std::string name = utf16ToUtf8(smdh.titles[lang].shortDescription, 64);
        if (name.empty()) {
            name = utf16ToUtf8(smdh.titles[1].shortDescription, 64);
        }
        return name;
    }
    return "";
}

bool SaveManager::copyArchiveDirToFs(uint64_t archiveHandle, const std::string& archivePath, const std::string& fsDestPath) {
    FS_Archive archive = static_cast<FS_Archive>(archiveHandle);
    Handle dirHandle = 0;
    FS_Path dPath = fsMakePath(PATH_ASCII, archivePath.c_str());

    Result res = FSUSER_OpenDirectory(&dirHandle, archive, dPath);
    if (R_FAILED(res)) {
        return false;
    }

    ConfigManager::ensureDirectories(fsDestPath + "/");

    constexpr u32 ENTRY_COUNT = 32;
    FS_DirectoryEntry entries[ENTRY_COUNT];
    u32 readCount = 0;

    bool success = true;

    while (R_SUCCEEDED(FSDIR_Read(dirHandle, &readCount, ENTRY_COUNT, entries)) && readCount > 0) {
        for (u32 i = 0; i < readCount; ++i) {
            std::string entryName = utf16ToUtf8(entries[i].name, 256);
            if (entryName == "." || entryName == "..") {
                continue;
            }

            std::string nextArchivePath = (archivePath == "/") ? ("/" + entryName) : (archivePath + "/" + entryName);
            std::string nextFsPath = fsDestPath + "/" + entryName;

            if (entries[i].attributes & FS_ATTRIBUTE_DIRECTORY) {
                mkdir(nextFsPath.c_str(), 0777);
                if (!copyArchiveDirToFs(archiveHandle, nextArchivePath, nextFsPath)) {
                    success = false;
                }
            } else {
                Handle srcFile = 0;
                FS_Path fPath = fsMakePath(PATH_ASCII, nextArchivePath.c_str());
                if (R_FAILED(FSUSER_OpenFile(&srcFile, archive, fPath, FS_OPEN_READ, 0))) {
                    success = false;
                    continue;
                }

                FILE* dstFile = fopen(nextFsPath.c_str(), "wb");
                if (!dstFile) {
                    FSFILE_Close(srcFile);
                    success = false;
                    continue;
                }

                constexpr u32 BUFFER_SIZE = 0x10000;
                std::vector<u8> buffer(BUFFER_SIZE);
                u64 offset = 0;
                u32 bytesRead = 0;

                while (R_SUCCEEDED(FSFILE_Read(srcFile, &bytesRead, offset, buffer.data(), BUFFER_SIZE)) && bytesRead > 0) {
                    fwrite(buffer.data(), 1, bytesRead, dstFile);
                    offset += bytesRead;
                }

                fclose(dstFile);
                FSFILE_Close(srcFile);
            }
        }
    }

    FSDIR_Close(dirHandle);
    return success;
}

bool SaveManager::copyFsDirToArchive(uint64_t archiveHandle, const std::string& fsSrcPath, const std::string& archiveDestPath) {
    FS_Archive archive = static_cast<FS_Archive>(archiveHandle);
    DIR* dir = opendir(fsSrcPath.c_str());
    if (!dir) {
        return false;
    }

    bool success = true;
    struct dirent* entry = nullptr;

    while ((entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }

        std::string srcItem = fsSrcPath + "/" + name;
        std::string dstItem = (archiveDestPath == "/") ? ("/" + name) : (archiveDestPath + "/" + name);

        struct stat st;
        if (stat(srcItem.c_str(), &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            FS_Path dPath = fsMakePath(PATH_ASCII, dstItem.c_str());
            FSUSER_CreateDirectory(archive, dPath, 0);
            if (!copyFsDirToArchive(archiveHandle, srcItem, dstItem)) {
                success = false;
            }
        } else {
            FILE* srcFile = fopen(srcItem.c_str(), "rb");
            if (!srcFile) {
                success = false;
                continue;
            }

            fseek(srcFile, 0, SEEK_END);
            u64 fileSize = ftell(srcFile);
            fseek(srcFile, 0, SEEK_SET);

            FS_Path fPath = fsMakePath(PATH_ASCII, dstItem.c_str());
            FSUSER_DeleteFile(archive, fPath);
            FSUSER_CreateFile(archive, fPath, 0, fileSize);

            Handle dstFile = 0;
            if (R_FAILED(FSUSER_OpenFile(&dstFile, archive, fPath, FS_OPEN_WRITE, 0))) {
                fclose(srcFile);
                success = false;
                continue;
            }

            constexpr size_t BUFFER_SIZE = 0x10000;
            std::vector<u8> buffer(BUFFER_SIZE);
            u64 offset = 0;
            size_t bytesRead = 0;

            while ((bytesRead = fread(buffer.data(), 1, BUFFER_SIZE, srcFile)) > 0) {
                u32 bytesWritten = 0;
                FSFILE_Write(dstFile, &bytesWritten, offset, buffer.data(), bytesRead, FS_WRITE_FLUSH);
                offset += bytesWritten;
            }

            FSFILE_Close(dstFile);
            fclose(srcFile);
        }
    }

    closedir(dir);
    return success;
}

#endif

std::vector<InstalledTitle> SaveManager::scanInstalledTitles() {
    std::vector<InstalledTitle> outTitles;

#if defined(__3DS__) || defined(_3DS)
    FS_MediaType mediaTypes[] = { MEDIATYPE_SD, MEDIATYPE_GAME_CARD };

    for (auto mediaType : mediaTypes) {
        u32 count = 0;
        if (R_FAILED(AM_GetTitleCount(mediaType, &count)) || count == 0) {
            continue;
        }

        std::vector<u64> ids(count);
        u32 read = 0;
        if (R_FAILED(AM_GetTitleList(&read, mediaType, count, ids.data()))) {
            continue;
        }

        for (u32 i = 0; i < read; ++i) {
            u64 tid = ids[i];
            u32 tidHigh = static_cast<u32>((tid >> 32) & 0xFFFFFFFF);

            if (tidHigh != 0x00040000 && tidHigh != 0x00040002) {
                continue;
            }

            InstalledTitle title;
            title.titleId = tid;
            title.titleIdHex = formatTitleId(tid);
            title.mediaType = mediaType;
            title.name = getTitleNameFromSmdh(tid, mediaType);

            char prodCode[16] = {0};
            if (R_SUCCEEDED(AM_GetTitleProductCode(mediaType, tid, prodCode)) && prodCode[0] != '\0') {
                title.productCode = prodCode;
            }

            if (title.name.empty()) {
                title.name = !title.productCode.empty() ? title.productCode : ("Title " + title.titleIdHex);
            }

            title.hasSave = checkSaveExists(tid, mediaType);
            outTitles.push_back(title);
        }
    }
#endif

    return outTitles;
}

bool SaveManager::checkSaveExists(uint64_t titleId, uint8_t mediaType) {
#if defined(__3DS__) || defined(_3DS)
    u32 pathData[3] = {
        static_cast<u32>(titleId & 0xFFFFFFFF),
        static_cast<u32>((titleId >> 32) & 0xFFFFFFFF),
        mediaType
    };
    FS_Path aPath = { PATH_BINARY, 12, pathData };
    FS_Archive archive = 0;

    Result res = FSUSER_OpenArchive(&archive, ARCHIVE_USER_SAVEDATA, aPath);
    if (R_SUCCEEDED(res)) {
        FSUSER_CloseArchive(archive);
        return true;
    }
    return false;
#else
    (void)titleId;
    (void)mediaType;
    return false;
#endif
}

bool SaveManager::backupSave(uint64_t titleId, const std::string& outDirectory, uint8_t mediaType) {
#if defined(__3DS__) || defined(_3DS)
    u32 pathData[3] = {
        static_cast<u32>(titleId & 0xFFFFFFFF),
        static_cast<u32>((titleId >> 32) & 0xFFFFFFFF),
        mediaType
    };
    FS_Path aPath = { PATH_BINARY, 12, pathData };
    FS_Archive archive = 0;

    Result res = FSUSER_OpenArchive(&archive, ARCHIVE_USER_SAVEDATA, aPath);
    if (R_FAILED(res)) {
        return false;
    }

    bool success = copyArchiveDirToFs(archive, "/", outDirectory);
    FSUSER_CloseArchive(archive);
    return success;
#else
    (void)titleId;
    (void)outDirectory;
    (void)mediaType;
    return false;
#endif
}

bool SaveManager::injectSave(uint64_t titleId, const std::string& inDirectory, uint8_t mediaType) {
#if defined(__3DS__) || defined(_3DS)
    u32 pathData[3] = {
        static_cast<u32>(titleId & 0xFFFFFFFF),
        static_cast<u32>((titleId >> 32) & 0xFFFFFFFF),
        mediaType
    };
    FS_Path aPath = { PATH_BINARY, 12, pathData };
    FS_Archive archive = 0;

    Result res = FSUSER_OpenArchive(&archive, ARCHIVE_USER_SAVEDATA, aPath);
    if (R_FAILED(res)) {
        return false;
    }

    bool success = copyFsDirToArchive(archive, inDirectory, "/");
    if (success) {
        FSUSER_ControlArchive(archive, ARCHIVE_ACTION_COMMIT_SAVE_DATA, nullptr, 0, nullptr, 0);
    }

    FSUSER_CloseArchive(archive);
    return success;
#else
    (void)titleId;
    (void)inDirectory;
    (void)mediaType;
    return false;
#endif
}

} // namespace RomM3DS
