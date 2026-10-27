#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <ctime>

namespace RomM3DS {

struct InstalledTitle {
    uint64_t titleId = 0;
    std::string titleIdHex;
    std::string name;
    std::string productCode;
    std::string console = "3DS";
    uint8_t mediaType = 0;
    bool hasSave = false;
    time_t saveTime = 0;

    // For file-based ROMs (mGBA, TWiLight Menu++, etc.)
    bool isFileRom = false;
    std::string romPath;
    std::string savePath;
};

class SaveManager {
public:
    SaveManager();
    ~SaveManager();

    std::vector<InstalledTitle> scanInstalledTitles(const std::vector<std::string>& customRomDirs = {});
    std::vector<InstalledTitle> scanRomFiles(const std::vector<std::string>& customRomDirs = {});

    bool checkSaveExists(uint64_t titleId, uint8_t mediaType = 1);
    bool checkSaveExists(const InstalledTitle& title);
    time_t getSaveTime(const InstalledTitle& title);

    bool backupSave(uint64_t titleId, const std::string& outDirectory, uint8_t mediaType = 1);
    bool backupSave(const InstalledTitle& title, const std::string& outDirectory);

    bool injectSave(uint64_t titleId, const std::string& inDirectory, uint8_t mediaType = 1);
    bool injectSave(const InstalledTitle& title, const std::string& inDirectory);

    static std::string formatTitleId(uint64_t titleId);
    static uint64_t parseTitleId(const std::string& hexStr);
    static std::string determineConsole(const std::string& productCode, uint64_t titleId);
    static std::string findSaveFileForRom(const std::string& romPath, const std::string& console);
    static uint64_t hashRomPath(const std::string& path);

private:
#if defined(__3DS__) || defined(_3DS)
    bool copyArchiveDirToFs(uint64_t archiveHandle, const std::string& archivePath, const std::string& fsDestPath);
    bool copyFsDirToArchive(uint64_t archiveHandle, const std::string& fsSrcPath, const std::string& archiveDestPath);
    std::string getTitleNameFromSmdh(uint64_t titleId, uint8_t mediaType);
#endif
};

} // namespace RomM3DS
