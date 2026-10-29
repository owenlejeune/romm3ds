#include "zip_helper.hpp"
#include "config_manager.hpp"

#include <cstdio>
#include <cstring>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>

#include <minizip/unzip.h>
#include <minizip/zip.h>

namespace RomM3DS {

bool ZipHelper::isZipFile(const std::string& filePath) {
    if (filePath.empty()) return false;

    // Check extension
    size_t dotPos = filePath.find_last_of('.');
    if (dotPos != std::string::npos) {
        std::string ext = filePath.substr(dotPos);
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".zip") return true;
    }

    // Check magic bytes PK\x03\x04
    FILE* f = fopen(filePath.c_str(), "rb");
    if (!f) return false;

    uint8_t magic[4] = {0};
    size_t n = fread(magic, 1, 4, f);
    fclose(f);

    return (n == 4 && magic[0] == 0x50 && magic[1] == 0x4B && magic[2] == 0x03 && magic[3] == 0x04);
}

void ZipHelper::getDirectoryStats(const std::string& dirPath, int& outFiles, int& outDirs) {
    outFiles = 0;
    outDirs = 0;

    DIR* d = opendir(dirPath.c_str());
    if (!d) return;

    struct dirent* ent = nullptr;
    while ((ent = readdir(d)) != nullptr) {
        if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0) continue;
        if (ent->d_name[0] == '.') continue; // skip hidden files

        std::string full = dirPath + "/" + ent->d_name;
        struct stat st;
        if (stat(full.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                outDirs++;
            } else if (S_ISREG(st.st_mode)) {
                outFiles++;
            }
        }
    }
    closedir(d);
}

void ZipHelper::unnestDirectoryIfSingle(const std::string& destDir, LogCallback log) {
    auto emit = [&](const std::string& m) { if (log) log(m); };

    // Loop up to 5 times to handle deeply nested folders like Checkpoint/saves/Title/...
    for (int depth = 0; depth < 5; ++depth) {
        int files = 0;
        int dirs = 0;
        std::string singleSubdir;

        DIR* d = opendir(destDir.c_str());
        if (!d) break;

        struct dirent* ent = nullptr;
        while ((ent = readdir(d)) != nullptr) {
            if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0) continue;
            if (ent->d_name[0] == '.') continue;

            std::string full = destDir + "/" + ent->d_name;
            struct stat st;
            if (stat(full.c_str(), &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    dirs++;
                    singleSubdir = full;
                } else if (S_ISREG(st.st_mode)) {
                    files++;
                }
            }
        }
        closedir(d);

        if (files == 0 && dirs == 1 && !singleSubdir.empty()) {
            emit("Flattening nested save directory: " + singleSubdir);
            DIR* sd = opendir(singleSubdir.c_str());
            if (!sd) break;

            std::vector<std::string> subItems;
            while ((ent = readdir(sd)) != nullptr) {
                if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0) continue;
                subItems.push_back(ent->d_name);
            }
            closedir(sd);

            for (const auto& item : subItems) {
                std::string oldPath = singleSubdir + "/" + item;
                std::string newPath = destDir + "/" + item;
                rename(oldPath.c_str(), newPath.c_str());
            }
            rmdir(singleSubdir.c_str());
        } else {
            break;
        }
    }
}

bool ZipHelper::extractZip(const std::string& zipFilePath, const std::string& destDirectory,
                           LogCallback log, bool autoUnnest) {
    auto emit = [&](const std::string& m) { if (log) log(m); };

    unzFile uf = unzOpen(zipFilePath.c_str());
    if (!uf) {
        emit("[ERROR] Could not open zip archive: " + zipFilePath);
        return false;
    }

    ConfigManager::ensureDirectories(destDirectory + "/");

    if (unzGoToFirstFile(uf) != UNZ_OK) {
        emit("[ERROR] Zip file is empty or corrupted: " + zipFilePath);
        unzClose(uf);
        return false;
    }

    emit("Extracting zip archive: " + zipFilePath);
    int extractedFiles = 0;

    do {
        char szFileName[512] = {0};
        unz_file_info fileInfo;
        std::memset(&fileInfo, 0, sizeof(fileInfo));

        int res = unzGetCurrentFileInfo(uf, &fileInfo, szFileName, sizeof(szFileName), nullptr, 0, nullptr, 0);
        if (res != UNZ_OK) {
            emit("[WARN] Could not read file info from zip entry.");
            continue;
        }

        std::string entryName = szFileName;
        if (entryName.empty()) continue;

        // Normalize slashes
        for (char& c : entryName) {
            if (c == '\\') c = '/';
        }

        // Skip macOS metadata files inside zip archives
        if (entryName.find("__MACOSX/") != std::string::npos ||
            entryName.find("/._") != std::string::npos ||
            (entryName.length() >= 2 && entryName.substr(0, 2) == "._")) {
            continue;
        }

        std::string destPath = destDirectory + "/" + entryName;

        // If entry ends with '/', it's a directory
        if (entryName.back() == '/') {
            ConfigManager::ensureDirectories(destPath);
            continue;
        }

        ConfigManager::ensureDirectories(destPath);

        if (unzOpenCurrentFile(uf) != UNZ_OK) {
            emit("[ERROR] Failed to open entry in zip: " + entryName);
            continue;
        }

        FILE* out = fopen(destPath.c_str(), "wb");
        if (!out) {
            emit("[ERROR] Failed to create output file: " + destPath);
            unzCloseCurrentFile(uf);
            continue;
        }

        constexpr size_t BUF_SIZE = 32 * 1024;
        std::vector<uint8_t> buffer(BUF_SIZE);
        int bytesRead = 0;
        bool writeError = false;

        while ((bytesRead = unzReadCurrentFile(uf, buffer.data(), BUF_SIZE)) > 0) {
            if (fwrite(buffer.data(), 1, bytesRead, out) != static_cast<size_t>(bytesRead)) {
                writeError = true;
                break;
            }
        }

        fclose(out);
        unzCloseCurrentFile(uf);

        if (writeError || bytesRead < 0) {
            emit("[ERROR] Error extracting entry: " + entryName);
        } else {
            extractedFiles++;
            emit("  Extracted: " + entryName + " (" + std::to_string(fileInfo.uncompressed_size) + " B)");
        }

    } while (unzGoToNextFile(uf) == UNZ_OK);

    unzClose(uf);

    if (autoUnnest) {
        unnestDirectoryIfSingle(destDirectory, log);
    }

    emit("[OK] Zip extraction completed (" + std::to_string(extractedFiles) + " files).");
    return (extractedFiles > 0);
}

static void collectFilesRecursive(const std::string& baseDir, const std::string& subRel,
                                  std::vector<std::string>& outRelPaths) {
    std::string currentPath = subRel.empty() ? baseDir : (baseDir + "/" + subRel);
    DIR* d = opendir(currentPath.c_str());
    if (!d) return;

    struct dirent* ent = nullptr;
    while ((ent = readdir(d)) != nullptr) {
        if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0) continue;
        if (ent->d_name[0] == '.') continue; // skip hidden files

        std::string relItem = subRel.empty() ? ent->d_name : (subRel + "/" + ent->d_name);
        std::string fullItem = baseDir + "/" + relItem;

        struct stat st;
        if (stat(fullItem.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                collectFilesRecursive(baseDir, relItem, outRelPaths);
            } else if (S_ISREG(st.st_mode)) {
                outRelPaths.push_back(relItem);
            }
        }
    }
    closedir(d);
}

bool ZipHelper::createZipFromDirectory(const std::string& zipFilePath, const std::string& sourceDirectory,
                                       LogCallback log) {
    auto emit = [&](const std::string& m) { if (log) log(m); };

    std::vector<std::string> relFiles;
    collectFilesRecursive(sourceDirectory, "", relFiles);

    if (relFiles.empty()) {
        emit("[WARN] No files to archive in: " + sourceDirectory);
        return false;
    }

    ConfigManager::ensureDirectories(zipFilePath);
    // Remove if existing file
    remove(zipFilePath.c_str());

    zipFile zf = zipOpen(zipFilePath.c_str(), APPEND_STATUS_CREATE);
    if (!zf) {
        emit("[ERROR] Could not create zip archive: " + zipFilePath);
        return false;
    }

    emit("Creating zip archive: " + zipFilePath + " (" + std::to_string(relFiles.size()) + " files)");

    bool allOk = true;
    for (const auto& relPath : relFiles) {
        std::string fullPath = sourceDirectory + "/" + relPath;
        FILE* src = fopen(fullPath.c_str(), "rb");
        if (!src) {
            emit("[WARN] Could not open file for archiving: " + fullPath);
            continue;
        }

        zip_fileinfo zi;
        std::memset(&zi, 0, sizeof(zi));

        int resOpen = zipOpenNewFileInZip(zf, relPath.c_str(), &zi,
                                          nullptr, 0, nullptr, 0, nullptr,
                                          Z_DEFLATED, Z_DEFAULT_COMPRESSION);
        if (resOpen != ZIP_OK) {
            emit("[ERROR] zipOpenNewFileInZip failed for: " + relPath);
            fclose(src);
            allOk = false;
            continue;
        }

        constexpr size_t BUF_SIZE = 32 * 1024;
        std::vector<uint8_t> buffer(BUF_SIZE);
        size_t bytesRead = 0;
        bool writeError = false;

        while ((bytesRead = fread(buffer.data(), 1, BUF_SIZE, src)) > 0) {
            if (zipWriteInFileInZip(zf, buffer.data(), bytesRead) != ZIP_OK) {
                writeError = true;
                break;
            }
        }

        fclose(src);
        zipCloseFileInZip(zf);

        if (writeError) {
            emit("[ERROR] Failed writing file into zip: " + relPath);
            allOk = false;
        } else {
            emit("  Archived: " + relPath);
        }
    }

    zipClose(zf, nullptr);
    if (allOk) {
        emit("[OK] Zip archive created successfully.");
    }
    return allOk;
}

} // namespace RomM3DS
