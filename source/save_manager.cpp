#include "save_manager.hpp"
#include "config_manager.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <set>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fstream>

#if defined(__3DS__) || defined(_3DS)
#include <3ds.h>
#include <3ds/util/utf.h>

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
std::string decodeUtf16(const u16* src, size_t maxUnits, bool sanitize = false) {
    if (!src || src[0] == 0) {
        return "";
    }

    std::vector<u16> temp(maxUnits + 1, 0);
    size_t actualLen = 0;
    while (actualLen < maxUnits && src[actualLen] != 0) {
        temp[actualLen] = src[actualLen];
        actualLen++;
    }
    temp[actualLen] = 0;
    if (actualLen == 0) {
        return "";
    }

    std::vector<uint8_t> outBuf(actualLen * 4 + 1, 0);
    ssize_t units = utf16_to_utf8(outBuf.data(), temp.data(), outBuf.size() - 1);
    if (units <= 0) {
        return "";
    }
    outBuf[units] = '\0';
    std::string out(reinterpret_cast<char*>(outBuf.data()));

    if (sanitize) {
        std::string cleaned;
        bool inSpace = false;
        for (char c : out) {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                if (!inSpace) {
                    cleaned += ' ';
                    inSpace = true;
                }
            } else {
                cleaned += c;
                inSpace = false;
            }
        }
        size_t first = cleaned.find_first_not_of(" ");
        if (first == std::string::npos) {
            return "";
        }
        size_t last = cleaned.find_last_not_of(" ");
        return cleaned.substr(first, last - first + 1);
    }

    return out;
}

std::string extractTitleString(const smdh_title_s& titleEntry) {
    std::string name = decodeUtf16(titleEntry.longDescription, 128, true);
    if (name.empty()) {
        name = decodeUtf16(titleEntry.shortDescription, 64, true);
    }
    return name;
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

std::string SaveManager::determineConsole(const std::string& productCode, uint64_t titleId) {
    if (!productCode.empty()) {
        std::string p = productCode;
        for (char& c : p) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }

        if (p.rfind("CTR", 0) == 0 || p.rfind("KTR", 0) == 0) return "3DS";
        if (p.rfind("TWL", 0) == 0 || p.rfind("NTR", 0) == 0 || p.rfind("DS", 0) == 0) return "DS";
        if (p.rfind("AGB", 0) == 0) return "GBA";
        if (p.rfind("DMG", 0) == 0) return "GB";
        if (p.rfind("CGB", 0) == 0) return "GBC";
        if (p.rfind("NES", 0) == 0 || p.rfind("FAM", 0) == 0 || p.rfind("FC", 0) == 0) return "NES";
        if (p.rfind("SNS", 0) == 0 || p.rfind("SHVC", 0) == 0 || p.rfind("SFC", 0) == 0 || p.rfind("SNES", 0) == 0) return "SNES";
        if (p.rfind("GG", 0) == 0 || p.rfind("SEG", 0) == 0) return "GG";
        if (p.rfind("PCE", 0) == 0 || p.rfind("TGX", 0) == 0) return "PCE";
    }

    uint32_t tidHigh = static_cast<uint32_t>((titleId >> 32) & 0xFFFFFFFF);
    if (tidHigh == 0x00048004 || tidHigh == 0x00048005 || tidHigh == 0x0004800F) {
        return "DS";
    }

    return "3DS";
}

#if defined(__3DS__) || defined(_3DS)

std::string SaveManager::getTitleNameFromSmdh(uint64_t titleId, uint8_t mediaType) {
    u32 archPath[4] = {
        static_cast<u32>(titleId & 0xFFFFFFFF),
        static_cast<u32>((titleId >> 32) & 0xFFFFFFFF),
        static_cast<u32>(mediaType),
        0x00000000
    };
    static const u32 filePath[5] = {0x00000000, 0x00000000, 0x00000002, 0x6E6F6369, 0x00000000};

    FS_Path apath = { PATH_BINARY, sizeof(archPath), archPath };
    FS_Path fpath = { PATH_BINARY, sizeof(filePath), filePath };

    Handle file = 0;
    Result res = FSUSER_OpenFileDirectly(&file, ARCHIVE_SAVEDATA_AND_CONTENT, apath, fpath, FS_OPEN_READ, 0);
    if (R_FAILED(res)) {
        return "";
    }

    smdh_s smdh;
    std::memset(&smdh, 0, sizeof(smdh));
    u32 bytesRead = 0;
    res = FSFILE_Read(file, &bytesRead, 0, &smdh, sizeof(smdh_s));
    FSFILE_Close(file);

    if (R_SUCCEEDED(res) && bytesRead >= 0x2040 && std::memcmp(smdh.magic, "SMDH", 4) == 0) {
        u8 sysLang = 1;
        if (R_SUCCEEDED(CFGU_GetSystemLanguage(&sysLang)) && sysLang < 16) {
            std::string name = extractTitleString(smdh.titles[sysLang]);
            if (!name.empty()) return name;
        }

        std::string enName = extractTitleString(smdh.titles[1]);
        if (!enName.empty()) return enName;

        std::string jpName = extractTitleString(smdh.titles[0]);
        if (!jpName.empty()) return jpName;

        for (int i = 0; i < 16; ++i) {
            std::string anyName = extractTitleString(smdh.titles[i]);
            if (!anyName.empty()) return anyName;
        }
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
            std::string entryName = decodeUtf16(entries[i].name, 256);
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

uint64_t SaveManager::hashRomPath(const std::string& path) {
    uint64_t hash = 14695981039346656037ULL;
    for (char c : path) {
        hash ^= static_cast<uint8_t>(std::tolower(static_cast<unsigned char>(c)));
        hash *= 1099511628211ULL;
    }
    return hash;
}

namespace {

std::vector<std::string> getMgbaSaveDirectories() {
    static std::vector<std::string> cachedDirs;
    static bool resolved = false;
    if (resolved) return cachedDirs;

    std::vector<std::string> dirs;

    const std::vector<std::string> configPaths = {
        "sdmc:/3ds/mgba/config.ini",
        "sdmc:/3ds/mGBA/config.ini",
        "sdmc:/mgba/config.ini",
        "sdmc:/mGBA/config.ini"
    };

    for (const auto& cp : configPaths) {
        std::ifstream file(cp);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                size_t eqPos = line.find('=');
                if (eqPos != std::string::npos) {
                    std::string key = line.substr(0, eqPos);
                    while (!key.empty() && std::isspace(static_cast<unsigned char>(key.front()))) key.erase(key.begin());
                    while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) key.pop_back();

                    if (key == "savegamePath") {
                        std::string val = line.substr(eqPos + 1);
                        while (!val.empty() && std::isspace(static_cast<unsigned char>(val.front()))) val.erase(val.begin());
                        while (!val.empty() && std::isspace(static_cast<unsigned char>(val.back()))) val.pop_back();
                        if (!val.empty()) {
                            if (val.front() == '"' && val.back() == '"') {
                                val = val.substr(1, val.length() - 2);
                            }
                            std::string fullDir;
                            if (val.rfind("sdmc:", 0) == 0) {
                                fullDir = val;
                            } else if (val.front() == '/') {
                                fullDir = "sdmc:" + val;
                            } else {
                                size_t cSlash = cp.find_last_of('/');
                                std::string baseDir = (cSlash != std::string::npos) ? cp.substr(0, cSlash) : "sdmc:/3ds/mgba";
                                fullDir = baseDir + "/" + val;
                            }
                            if (!fullDir.empty() && fullDir.back() != '/') {
                                fullDir += '/';
                            }
                            if (std::find(dirs.begin(), dirs.end(), fullDir) == dirs.end()) {
                                dirs.push_back(fullDir);
                            }
                        }
                    }
                }
            }
        }
    }

    const std::vector<std::string> defaults = {
        "sdmc:/3ds/mgba/saves/",
        "sdmc:/3ds/mGBA/saves/",
        "sdmc:/3ds/mgba/",
        "sdmc:/3ds/mGBA/",
        "sdmc:/mgba/saves/",
        "sdmc:/mgba/",
        "sdmc:/mGBA/saves/",
        "sdmc:/mGBA/"
    };
    for (const auto& def : defaults) {
        if (std::find(dirs.begin(), dirs.end(), def) == dirs.end()) {
            dirs.push_back(def);
        }
    }

    cachedDirs = dirs;
    resolved = true;
    return cachedDirs;
}

} // namespace

std::string SaveManager::findSaveFileForRom(const std::string& romPath, const std::string& console) {
    size_t slashPos = romPath.find_last_of("/\\");
    std::string dir = (slashPos != std::string::npos) ? romPath.substr(0, slashPos) : ".";
    std::string filename = (slashPos != std::string::npos) ? romPath.substr(slashPos + 1) : romPath;

    size_t dotPos = filename.find_last_of('.');
    std::string baseName = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;

    std::vector<std::string> candidates;

    if (console == "GBA") {
        // mGBA on 3DS places saves directly alongside the ROM by default
        candidates.push_back(dir + "/" + baseName + ".sav");
        candidates.push_back(dir + "/saves/" + baseName + ".sav");
        candidates.push_back(dir + "/" + baseName + ".SAV");
        candidates.push_back(dir + "/saves/" + baseName + ".SAV");

        std::vector<std::string> mgbaDirs = getMgbaSaveDirectories();
        for (const auto& md : mgbaDirs) {
            candidates.push_back(md + baseName + ".sav");
            candidates.push_back(md + baseName + ".SAV");
        }
    } else if (console == "DS") {
        candidates.push_back(dir + "/saves/" + baseName + ".sav");
        candidates.push_back(dir + "/" + baseName + ".sav");
        candidates.push_back(dir + "/saves/" + baseName + ".SAV");
        candidates.push_back(dir + "/" + baseName + ".SAV");
        candidates.push_back(dir + "/saves/" + baseName + ".dsv");
        candidates.push_back(dir + "/" + baseName + ".dsv");
    } else {
        candidates.push_back(dir + "/" + baseName + ".sav");
        candidates.push_back(dir + "/saves/" + baseName + ".sav");
        candidates.push_back(dir + "/" + baseName + ".SAV");
        candidates.push_back(dir + "/saves/" + baseName + ".SAV");
    }

    if (console != "GBA") {
        candidates.push_back(dir + "/" + baseName + ".srm");
        candidates.push_back(dir + "/saves/" + baseName + ".srm");
        candidates.push_back(dir + "/" + baseName + ".SRM");
        candidates.push_back(dir + "/saves/" + baseName + ".SRM");
    }

    std::string bestCand;
    time_t bestTime = 0;
    for (const auto& cand : candidates) {
        struct stat st;
        if (stat(cand.c_str(), &st) == 0 && st.st_size > 0) {
            if (bestCand.empty() || st.st_mtime > bestTime) {
                bestCand = cand;
                bestTime = st.st_mtime;
            }
        }
    }

    return bestCand;
}

namespace {

void scanDirectoryRecursive(
    const std::string& dirPath,
    int depth,
    int maxDepth,
    std::vector<InstalledTitle>& outTitles,
    std::set<std::string>& seenRomPaths)
{
    if (depth > maxDepth) return;

    DIR* dir = opendir(dirPath.c_str());
    if (!dir) return;

    struct dirent* entry = nullptr;
    std::vector<std::string> subdirs;

    while ((entry = readdir(dir)) != nullptr) {
        const char* name = entry->d_name;
        if (!name || name[0] == '\0') continue;
        if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) continue;
        if (name[0] == '.') continue;

        std::string fullPath = dirPath;
        if (fullPath.back() != '/') fullPath += '/';
        fullPath += name;

        struct stat st;
        if (stat(fullPath.c_str(), &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            std::string lowerName = name;
            for (char& c : lowerName) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (lowerName == "saves" || lowerName == "save" || lowerName == "cheats" ||
                lowerName == "bios" || lowerName == "screenshots" || lowerName == "borders" ||
                lowerName == "patch" || lowerName == "temp") {
                continue;
            }
            subdirs.push_back(fullPath);
        } else if (S_ISREG(st.st_mode) && st.st_size > 0) {
            std::string filename = name;
            size_t dotPos = filename.find_last_of('.');
            if (dotPos == std::string::npos) continue;

            std::string ext = filename.substr(dotPos);
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            std::string console;
            if (ext == ".gba" || ext == ".agb") {
                console = "GBA";
            } else if (ext == ".nds" || ext == ".dsi") {
                console = "DS";
            } else if (ext == ".gb") {
                console = "GB";
            } else if (ext == ".gbc") {
                console = "GBC";
            } else if (ext == ".nes") {
                console = "NES";
            } else if (ext == ".sfc" || ext == ".snes") {
                console = "SNES";
            }

            if (console.empty()) continue;

            if (seenRomPaths.count(fullPath) > 0) continue;
            seenRomPaths.insert(fullPath);

            InstalledTitle title;
            title.isFileRom = true;
            title.romPath = fullPath;
            title.console = console;
            title.mediaType = 1; // SD
            title.name = filename.substr(0, dotPos);

            uint64_t hash = SaveManager::hashRomPath(fullPath);
            title.titleId = hash;
            title.titleIdHex = SaveManager::formatTitleId(hash);

            FILE* f = fopen(fullPath.c_str(), "rb");
            if (f) {
                if (console == "GBA") {
                    if (fseek(f, 0xAC, SEEK_SET) == 0) {
                        char code[5] = {0};
                        if (fread(code, 1, 4, f) == 4) {
                            bool valid = true;
                            for (int c = 0; c < 4; ++c) {
                                if (!std::isalnum(static_cast<unsigned char>(code[c]))) {
                                    valid = false;
                                    break;
                                }
                            }
                            if (valid) {
                                title.productCode = std::string("AGB-") + code;
                            }
                        }
                    }
                } else if (console == "DS") {
                    if (fseek(f, 0x0C, SEEK_SET) == 0) {
                        char code[5] = {0};
                        if (fread(code, 1, 4, f) == 4) {
                            bool valid = true;
                            for (int c = 0; c < 4; ++c) {
                                if (!std::isalnum(static_cast<unsigned char>(code[c]))) {
                                    valid = false;
                                    break;
                                }
                            }
                            if (valid) {
                                title.productCode = std::string("NTR-") + code;
                            }
                        }
                    }
                }
                fclose(f);
            }

            title.savePath = SaveManager::findSaveFileForRom(fullPath, console);
            if (!title.savePath.empty()) {
                title.hasSave = true;
                struct stat st;
                if (stat(title.savePath.c_str(), &st) == 0) {
                    title.saveTime = st.st_mtime;
                }
            } else {
                title.hasSave = false;
                std::string baseDir = dirPath;
                if (console == "DS") {
                    title.savePath = baseDir + "/saves/" + title.name + ".sav";
                } else {
                    title.savePath = baseDir + "/" + title.name + ".sav";
                }
            }

            outTitles.push_back(title);
        }
    }

    closedir(dir);

    for (const auto& sdir : subdirs) {
        scanDirectoryRecursive(sdir, depth + 1, maxDepth, outTitles, seenRomPaths);
    }
}

} // namespace

std::vector<InstalledTitle> SaveManager::scanRomFiles(const std::vector<std::string>& customRomDirs) {
    std::vector<InstalledTitle> titles;
    std::set<std::string> seenRomPaths;

    std::vector<std::string> dirsToScan;
    for (const auto& d : customRomDirs) {
        if (!d.empty() && std::find(dirsToScan.begin(), dirsToScan.end(), d) == dirsToScan.end()) {
            dirsToScan.push_back(d);
        }
    }

    const std::vector<std::string> defaults = {
        "sdmc:/roms",
        "sdmc:/mGBA",
        "sdmc:/gba",
        "sdmc:/nds"
    };

    for (const auto& def : defaults) {
        if (std::find(dirsToScan.begin(), dirsToScan.end(), def) == dirsToScan.end()) {
            dirsToScan.push_back(def);
        }
    }

    for (const auto& dirPath : dirsToScan) {
        scanDirectoryRecursive(dirPath, 0, 4, titles, seenRomPaths);
    }

    return titles;
}

std::vector<InstalledTitle> SaveManager::scanInstalledTitles(const std::vector<std::string>& customRomDirs) {
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

            if (tidHigh != 0x00040000 && tidHigh != 0x00040002 && tidHigh != 0x00048004) {
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

            title.console = determineConsole(title.productCode, tid);

            if (title.name.empty()) {
                title.name = !title.productCode.empty() ? title.productCode : ("Title " + title.titleIdHex);
            }

            title.hasSave = checkSaveExists(tid, mediaType);
            if (title.hasSave) {
                title.saveTime = getSaveTime(title);
            }
            outTitles.push_back(title);
        }
    }
#endif

    // Scan ROM files on SD card for mGBA, TWiLight Menu++, and standalone emulators
    std::vector<InstalledTitle> romTitles = scanRomFiles(customRomDirs);
    outTitles.insert(outTitles.end(), romTitles.begin(), romTitles.end());

    return outTitles;
}

#if defined(__3DS__) || defined(_3DS)
static Result openSaveArchive(FS_Archive* archive, uint64_t titleId, uint8_t mediaType) {
    if (mediaType == MEDIATYPE_NAND) {
        u32 pathData[2] = {
            static_cast<u32>(mediaType),
            static_cast<u32>(0x00020000 | (static_cast<u32>(titleId) >> 8))
        };
        FS_Path path = { PATH_BINARY, 8, pathData };
        return FSUSER_OpenArchive(archive, ARCHIVE_SYSTEM_SAVEDATA, path);
    } else {
        u32 pathData[3] = {
            static_cast<u32>(mediaType),
            static_cast<u32>(titleId & 0xFFFFFFFF),
            static_cast<u32>((titleId >> 32) & 0xFFFFFFFF)
        };
        FS_Path path = { PATH_BINARY, 12, pathData };
        return FSUSER_OpenArchive(archive, ARCHIVE_USER_SAVEDATA, path);
    }
}
#endif

bool SaveManager::checkSaveExists(uint64_t titleId, uint8_t mediaType) {
#if defined(__3DS__) || defined(_3DS)
    FS_Archive archive = 0;
    Result res = openSaveArchive(&archive, titleId, mediaType);
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

bool SaveManager::checkSaveExists(const InstalledTitle& title) {
    if (!title.isFileRom) {
        return checkSaveExists(title.titleId, title.mediaType);
    }

    if (!title.savePath.empty()) {
        struct stat st;
        if (stat(title.savePath.c_str(), &st) == 0 && st.st_size > 0) {
            return true;
        }
    }

    std::string found = findSaveFileForRom(title.romPath, title.console);
    return !found.empty();
}

time_t SaveManager::getSaveTime(const InstalledTitle& title) {
    if (title.isFileRom) {
        std::string savePath = title.savePath;
        if (savePath.empty()) {
            savePath = findSaveFileForRom(title.romPath, title.console);
        }
        if (!savePath.empty()) {
            struct stat st;
            if (stat(savePath.c_str(), &st) == 0 && st.st_size > 0) {
                return st.st_mtime;
            }
        }
        return 0;
    }

    std::string stagedDir = "sdmc:/3ds/romm3ds/saves/" + formatTitleId(title.titleId);
    DIR* d = opendir(stagedDir.c_str());
    if (d) {
        struct dirent* ent = nullptr;
        time_t newest = 0;
        while ((ent = readdir(d)) != nullptr) {
            if (std::strcmp(ent->d_name, ".") != 0 && std::strcmp(ent->d_name, "..") != 0 && ent->d_name[0] != '.') {
                std::string full = stagedDir + "/" + ent->d_name;
                struct stat st;
                if (stat(full.c_str(), &st) == 0 && st.st_mtime > newest) {
                    newest = st.st_mtime;
                }
            }
        }
        closedir(d);
        if (newest > 0) {
            return newest;
        }
    }

    char highHex[16];
    char lowHex[16];
    snprintf(highHex, sizeof(highHex), "%08lx", (unsigned long)((title.titleId >> 32) & 0xFFFFFFFF));
    snprintf(lowHex, sizeof(lowHex), "%08lx", (unsigned long)(title.titleId & 0xFFFFFFFF));

    DIR* root = opendir("sdmc:/Nintendo 3DS");
    if (root) {
        struct dirent* e0 = nullptr;
        while ((e0 = readdir(root)) != nullptr) {
            if (e0->d_name[0] == '.') continue;
            std::string p0 = std::string("sdmc:/Nintendo 3DS/") + e0->d_name;
            DIR* d0 = opendir(p0.c_str());
            if (!d0) continue;
            struct dirent* e1 = nullptr;
            while ((e1 = readdir(d0)) != nullptr) {
                if (e1->d_name[0] == '.') continue;
                std::string dataDir = p0 + "/" + e1->d_name + "/title/" + highHex + "/" + lowHex + "/data";
                DIR* dd = opendir(dataDir.c_str());
                if (dd) {
                    struct dirent* de = nullptr;
                    time_t newestData = 0;
                    while ((de = readdir(dd)) != nullptr) {
                        if (de->d_name[0] == '.') continue;
                        std::string df = dataDir + "/" + de->d_name;
                        struct stat dst;
                        if (stat(df.c_str(), &dst) == 0 && dst.st_mtime > newestData) {
                            newestData = dst.st_mtime;
                        }
                    }
                    closedir(dd);
                    if (newestData > 0) {
                        closedir(d0);
                        closedir(root);
                        return newestData;
                    }
                }
            }
            closedir(d0);
        }
        closedir(root);
    }

    return 0;
}

static std::string detectGbaSaveType(const std::string& romPath) {
    FILE* f = fopen(romPath.c_str(), "rb");
    if (!f) return "";
    std::vector<char> buf(512 * 1024);
    std::string prevTail;
    size_t bytesRead = 0;
    while ((bytesRead = fread(buf.data(), 1, buf.size(), f)) > 0) {
        std::string chunk = prevTail + std::string(buf.data(), bytesRead);
        if (chunk.find("FLASH1M_V") != std::string::npos) { fclose(f); return "FLASH1M"; }
        if (chunk.find("FLASH512_V") != std::string::npos || chunk.find("FLASH_V") != std::string::npos) { fclose(f); return "FLASH512"; }
        if (chunk.find("SRAM_V") != std::string::npos || chunk.find("SRAM_F_V") != std::string::npos) { fclose(f); return "SRAM"; }
        if (chunk.find("EEPROM_V") != std::string::npos) { fclose(f); return "EEPROM"; }
        if (chunk.length() > 64) {
            prevTail = chunk.substr(chunk.length() - 64);
        } else {
            prevTail = chunk;
        }
    }
    fclose(f);
    return "";
}

bool SaveManager::backupSave(uint64_t titleId, const std::string& outDirectory, uint8_t mediaType, LogCallback log) {
    auto emit = [&](const std::string& m) { if (log) log(m); };
#if defined(__3DS__) || defined(_3DS)
    FS_Archive archive = 0;
    Result res = openSaveArchive(&archive, titleId, mediaType);
    if (R_FAILED(res)) {
        emit("[ERROR] Failed to open 3DS save archive (Result 0x" + formatTitleId(res) + ")");
        return false;
    }

    emit("Backing up 3DS save archive to: " + outDirectory);
    bool success = copyArchiveDirToFs(archive, "/", outDirectory);
    FSUSER_CloseArchive(archive);
    emit(success ? "[OK] Archive backup completed." : "[ERROR] Failed copying archive files.");
    return success;
#else
    (void)titleId;
    (void)outDirectory;
    (void)mediaType;
    emit("[ERROR] 3DS archive backup not supported on this platform.");
    return false;
#endif
}

bool SaveManager::backupSave(const InstalledTitle& title, const std::string& outDirectory, LogCallback log) {
    auto emit = [&](const std::string& m) { if (log) log(m); };
    if (!title.isFileRom) {
        return backupSave(title.titleId, outDirectory, title.mediaType, log);
    }

    std::string saveFile = title.savePath;
    struct stat sst;
    if (saveFile.empty() || stat(saveFile.c_str(), &sst) != 0 || sst.st_size == 0) {
        saveFile = findSaveFileForRom(title.romPath, title.console);
    }

    if (saveFile.empty()) {
        emit("[ERROR] No local save file found for " + title.name);
        return false;
    }

    size_t slashPos = saveFile.find_last_of("/\\");
    std::string fileName = (slashPos != std::string::npos) ? saveFile.substr(slashPos + 1) : saveFile;
    std::string destPath = outDirectory + "/" + fileName;

    ConfigManager::ensureDirectories(destPath);
    emit("Copying save file: " + saveFile + " -> " + destPath);

    FILE* src = fopen(saveFile.c_str(), "rb");
    if (!src) {
        emit("[ERROR] Could not open source save: " + saveFile);
        return false;
    }

    FILE* dst = fopen(destPath.c_str(), "wb");
    if (!dst) {
        fclose(src);
        emit("[ERROR] Could not open destination for write: " + destPath);
        return false;
    }

    std::vector<uint8_t> buffer(64 * 1024);
    size_t bytesRead = 0;
    bool ok = true;
    while ((bytesRead = fread(buffer.data(), 1, buffer.size(), src)) > 0) {
        if (fwrite(buffer.data(), 1, bytesRead, dst) != bytesRead) {
            ok = false;
            break;
        }
    }

    fclose(src);
    fclose(dst);
    emit(ok ? "[OK] Local save copied to staging." : "[ERROR] Write error during save copy.");
    return ok;
}

bool SaveManager::injectSave(uint64_t titleId, const std::string& inDirectory, uint8_t mediaType, LogCallback log) {
    auto emit = [&](const std::string& m) { if (log) log(m); };
#if defined(__3DS__) || defined(_3DS)
    FS_Archive archive = 0;
    Result res = openSaveArchive(&archive, titleId, mediaType);
    if (R_FAILED(res)) {
        emit("[ERROR] Failed to open 3DS save archive for injection (Result 0x" + formatTitleId(res) + ")");
        return false;
    }

    emit("Writing save files into 3DS archive...");
    bool success = copyFsDirToArchive(archive, inDirectory, "/");
    if (success) {
        FSUSER_ControlArchive(archive, ARCHIVE_ACTION_COMMIT_SAVE_DATA, nullptr, 0, nullptr, 0);
        emit("[OK] Committed save data to 3DS archive.");
    } else {
        emit("[ERROR] Failed writing save files to 3DS archive.");
    }

    FSUSER_CloseArchive(archive);
    return success;
#else
    (void)titleId;
    (void)inDirectory;
    (void)mediaType;
    emit("[ERROR] 3DS archive injection not supported on this platform.");
    return false;
#endif
}

bool SaveManager::injectSave(const InstalledTitle& title, const std::string& inDirectory, LogCallback log) {
    auto emit = [&](const std::string& m) { if (log) log(m); };
    if (!title.isFileRom) {
        return injectSave(title.titleId, inDirectory, title.mediaType, log);
    }

    std::string srcFile;
    struct stat inSt;
    if (stat(inDirectory.c_str(), &inSt) == 0 && !S_ISDIR(inSt.st_mode)) {
        srcFile = inDirectory;
    } else {
        DIR* d = opendir(inDirectory.c_str());
        if (d) {
            struct dirent* ent = nullptr;
            time_t newestTime = 0;
            while ((ent = readdir(d)) != nullptr) {
                if (std::strcmp(ent->d_name, ".") != 0 && std::strcmp(ent->d_name, "..") != 0 && ent->d_name[0] != '.') {
                    std::string en = ent->d_name;
                    std::string full = inDirectory + "/" + en;
                    if (en.length() >= 4 && (en.rfind(".sav") == en.length() - 4 ||
                                             en.rfind(".SAV") == en.length() - 4 ||
                                             en.rfind(".dsv") == en.length() - 4 ||
                                             en.rfind(".srm") == en.length() - 4 ||
                                             en.rfind(".SRM") == en.length() - 4)) {
                        struct stat st;
                        if (stat(full.c_str(), &st) == 0) {
                            if (srcFile.empty() || st.st_mtime > newestTime) {
                                srcFile = full;
                                newestTime = st.st_mtime;
                            }
                        }
                    }
                }
            }
            closedir(d);
        }
    }

    if (srcFile.empty()) {
        emit("[ERROR] No downloaded save file found in staging: " + inDirectory);
        return false;
    }

    emit("Source save file: " + srcFile);

    std::string targetSavePath = title.savePath;
    size_t slashPos = title.romPath.find_last_of("/\\");
    std::string dir = (slashPos != std::string::npos) ? title.romPath.substr(0, slashPos) : ".";
    std::string filename = (slashPos != std::string::npos) ? title.romPath.substr(slashPos + 1) : title.romPath;
    size_t dotPos = filename.find_last_of('.');
    std::string baseName = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;

    if (targetSavePath.empty()) {
        if (title.console == "DS") {
            targetSavePath = dir + "/saves/" + baseName + ".sav";
        } else {
            targetSavePath = dir + "/" + baseName + ".sav";
        }
    }

    // TWiLight Menu / nds-bootstrap and mGBA only read .sav files.
    // If targetSavePath ends with .srm or .dsv, normalize extension to .sav.
    if (title.console == "DS" || title.console == "GBA") {
        size_t dot = targetSavePath.find_last_of('.');
        if (dot != std::string::npos) {
            std::string ext = targetSavePath.substr(dot);
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext == ".srm" || ext == ".dsv") {
                targetSavePath = targetSavePath.substr(0, dot) + ".sav";
            }
        }
    }

    FILE* src = fopen(srcFile.c_str(), "rb");
    if (!src) {
        emit("[ERROR] Could not open source file: " + srcFile);
        return false;
    }

    fseek(src, 0, SEEK_END);
    long srcSize = ftell(src);
    fseek(src, 0, SEEK_SET);

    size_t readOffset = 0;
    size_t writeLimit = (srcSize > 0) ? static_cast<size_t>(srcSize) : 0;
    bool isEeprom = false;

    if (title.console == "GBA" && srcSize > 0) {
        std::string gbaType = detectGbaSaveType(title.romPath);
        if (!gbaType.empty()) {
            emit("GBA hardware save type in ROM: " + gbaType);
        }

        size_t expectedSize = 0;
        if (gbaType == "FLASH1M") expectedSize = 131072;
        else if (gbaType == "FLASH512") expectedSize = 65536;
        else if (gbaType == "SRAM") expectedSize = 32768;
        else if (gbaType == "EEPROM") expectedSize = 8192;

        if (expectedSize == 0 && !title.savePath.empty()) {
            struct stat est;
            if (stat(title.savePath.c_str(), &est) == 0 && est.st_size > 0) {
                expectedSize = static_cast<size_t>(est.st_size);
                emit("Existing local save size: " + std::to_string(expectedSize) + " B");
            }
        }

        if (srcSize == 139264) { // 0x22000 (Libretro VBA-M / VBA-Next .srm)
            emit("Detected Libretro 136KB (.srm) container.");
            if (expectedSize == 131072) {
                readOffset = 0;
                writeLimit = 131072;
                emit("Extracted 128KB Flash payload from offset 0.");
            } else if (expectedSize == 65536) {
                readOffset = 0;
                writeLimit = 65536;
                emit("Extracted 64KB Flash payload from offset 0.");
            } else if (expectedSize == 32768) {
                readOffset = 0;
                writeLimit = 32768;
                emit("Extracted 32KB SRAM payload from offset 0.");
            } else if (expectedSize == 8192 || expectedSize == 512 || gbaType == "EEPROM") {
                readOffset = 0x20000;
                writeLimit = (expectedSize > 0) ? expectedSize : 8192;
                isEeprom = true;
                emit("Extracted EEPROM payload (" + std::to_string(writeLimit) + " B) from offset 0x20000.");
            } else {
                std::vector<uint8_t> probe(139264);
                if (fread(probe.data(), 1, 139264, src) == 139264) {
                    auto scan_section = [&](size_t offset, size_t len) {
                        for (size_t i = 0; i < len; ++i) {
                            if (probe[offset + i] != 0xFF) return true;
                        }
                        return false;
                    };
                    if (scan_section(0, 0x8000) && !scan_section(0x8000, 0x1A000)) {
                        readOffset = 0;
                        writeLimit = 0x8000;
                    } else if (scan_section(0, 0x10000) && !scan_section(0x10000, 0x10000)) {
                        readOffset = 0;
                        writeLimit = 0x10000;
                    } else if (scan_section(0, 0x20000)) {
                        readOffset = 0;
                        writeLimit = 0x20000;
                    } else if (scan_section(0x20000, 512) && !scan_section(0x20000 + 512, 0x2000 - 512)) {
                        readOffset = 0x20000;
                        writeLimit = 512;
                        isEeprom = true;
                    } else if (scan_section(0x20000, 0x2000)) {
                        readOffset = 0x20000;
                        writeLimit = 0x2000;
                        isEeprom = true;
                    } else {
                        readOffset = 0;
                        writeLimit = 131072;
                    }
                }
                fseek(src, 0, SEEK_SET);
                emit("Heuristic extracted " + std::to_string(writeLimit) + " B payload from offset " + std::to_string(readOffset) + ".");
            }
        } else if (srcSize > 131072) {
            readOffset = 0;
            writeLimit = 131072;
            emit("Trimmed " + std::to_string(srcSize) + " B -> 131072 B (128KB Flash).");
        } else if (expectedSize > 0 && (size_t)srcSize > expectedSize) {
            readOffset = 0;
            writeLimit = expectedSize;
            emit("Trimmed " + std::to_string(srcSize) + " B -> expected " + std::to_string(expectedSize) + " B.");
        } else {
            readOffset = 0;
            writeLimit = static_cast<size_t>(srcSize);
            emit("Using raw save payload (" + std::to_string(writeLimit) + " B).");
        }
    }

    std::vector<uint8_t> payload(writeLimit);
    if (fseek(src, readOffset, SEEK_SET) != 0) {
        fclose(src);
        emit("[ERROR] Seek failed in source save.");
        return false;
    }
    size_t bytesRead = fread(payload.data(), 1, writeLimit, src);
    fclose(src);
    if (bytesRead != writeLimit) {
        emit("[ERROR] Incomplete read: " + std::to_string(bytesRead) + "/" + std::to_string(writeLimit));
        return false;
    }

    if (isEeprom) {
        emit("Byte-swapping 64-bit EEPROM chunks for mGBA...");
        for (size_t i = 0; i + 8 <= payload.size(); i += 8) {
            for (int j = 0; j < 4; ++j) {
                std::swap(payload[i + j], payload[i + 7 - j]);
            }
        }
    }

    auto writePayloadToFile = [&](const std::string& path) -> bool {
        ConfigManager::ensureDirectories(path);
        FILE* out = fopen(path.c_str(), "wb");
        if (!out) {
            emit("[ERROR] Could not open destination: " + path);
            return false;
        }
        bool written = (fwrite(payload.data(), 1, payload.size(), out) == payload.size());
        fclose(out);
        if (written) {
            emit("[OK] Wrote " + path + " (" + std::to_string(payload.size()) + " B)");
        } else {
            emit("[ERROR] Failed writing to " + path);
        }
        return written;
    };

    bool ok = writePayloadToFile(targetSavePath);

    if (ok && title.console == "GBA") {
        std::set<std::string> destinations;
        destinations.insert(targetSavePath);

        // 1. Primary mGBA location: always write directly alongside ROM
        std::string romDirSave = dir + "/" + baseName + ".sav";
        if (destinations.find(romDirSave) == destinations.end()) {
            writePayloadToFile(romDirSave);
            destinations.insert(romDirSave);
        }

        // 2. If dir/saves/ directory exists, also write there for TWiLight Menu
        struct stat sdir;
        std::string savesSubDir = dir + "/saves";
        if (stat(savesSubDir.c_str(), &sdir) == 0 && S_ISDIR(sdir.st_mode)) {
            std::string subSave = savesSubDir + "/" + baseName + ".sav";
            if (destinations.find(subSave) == destinations.end()) {
                writePayloadToFile(subSave);
                destinations.insert(subSave);
            }
        }

        // 3. Write to any existing mGBA save directories (e.g. sdmc:/3ds/mgba/saves/)
        std::vector<std::string> mgbaDirs = getMgbaSaveDirectories();
        for (const auto& md : mgbaDirs) {
            struct stat mdst;
            std::string cand = md + baseName + ".sav";
            if ((stat(md.c_str(), &mdst) == 0 && S_ISDIR(mdst.st_mode)) || stat(cand.c_str(), &mdst) == 0) {
                if (destinations.find(cand) == destinations.end()) {
                    writePayloadToFile(cand);
                    destinations.insert(cand);
                }
            }
        }

        // 4. Overwrite any stale .srm files on the SD card so they cannot shadow .sav
        std::vector<std::string> staleSrms = { dir + "/" + baseName + ".srm", dir + "/saves/" + baseName + ".srm" };
        for (const auto& srmp : staleSrms) {
            struct stat sst;
            if (stat(srmp.c_str(), &sst) == 0) {
                emit("Updating local .srm with new save: " + srmp);
                writePayloadToFile(srmp);
            }
        }

        // 5. In case mGBA has an old savestate (.ss0 / .state) that autoloads, backup stale states
        std::vector<std::string> stateExts = {".ss0", ".ss1", ".ss2", ".state"};
        std::vector<std::string> stateDirs = {dir};
        for (const auto& md : mgbaDirs) {
            stateDirs.push_back(md);
            stateDirs.push_back(md + "states/");
        }
        for (const auto& sdirPath : stateDirs) {
            for (const auto& sext : stateExts) {
                std::string sPath = sdirPath;
                if (!sPath.empty() && sPath.back() != '/') sPath += '/';
                std::string stateFile = sPath + baseName + sext;
                struct stat sst;
                if (stat(stateFile.c_str(), &sst) == 0) {
                    std::string bakFile = stateFile + ".bak";
                    remove(bakFile.c_str());
                    if (rename(stateFile.c_str(), bakFile.c_str()) == 0) {
                        emit("Backed up savestate: " + stateFile + " -> .bak");
                    }
                }
            }
        }
    }

    return ok;
}

} // namespace RomM3DS
