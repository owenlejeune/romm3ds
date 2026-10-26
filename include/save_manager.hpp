#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace RomM3DS {

struct InstalledTitle {
    uint64_t titleId = 0;
    std::string titleIdHex;
    std::string name;
    std::string productCode;
    uint8_t mediaType = 0;
    bool hasSave = false;
};

class SaveManager {
public:
    SaveManager();
    ~SaveManager();

    std::vector<InstalledTitle> scanInstalledTitles();
    bool checkSaveExists(uint64_t titleId, uint8_t mediaType = 1);
    bool backupSave(uint64_t titleId, const std::string& outDirectory, uint8_t mediaType = 1);
    bool injectSave(uint64_t titleId, const std::string& inDirectory, uint8_t mediaType = 1);

    static std::string formatTitleId(uint64_t titleId);
    static uint64_t parseTitleId(const std::string& hexStr);

private:
#if defined(__3DS__) || defined(_3DS)
    bool copyArchiveDirToFs(uint64_t archiveHandle, const std::string& archivePath, const std::string& fsDestPath);
    bool copyFsDirToArchive(uint64_t archiveHandle, const std::string& fsSrcPath, const std::string& archiveDestPath);
    std::string getTitleNameFromSmdh(uint64_t titleId, uint8_t mediaType);
#endif
};

} // namespace RomM3DS
