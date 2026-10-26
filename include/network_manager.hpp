#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace RomM3DS {

class ConfigManager;

struct RommGame {
    uint64_t id = 0;
    std::string title;
    std::string platform;
    std::string romPath;
    std::string savePath;
    std::string updatedAt;
};

struct RommPlatform {
    uint64_t id = 0;
    std::string name;
    std::string slug;
    int romCount = 0;
};

struct NetworkResult {
    bool success = false;
    long httpCode = 0;
    std::string errorMessage;
    std::string responseBody;
};

class NetworkManager {
public:
    static bool initNetwork();
    static void exitNetwork();
    static bool isNetworkInitialized();

    explicit NetworkManager(ConfigManager* configMgr = nullptr);
    ~NetworkManager();

    void setConfigManager(ConfigManager* configMgr) { m_configMgr = configMgr; }

    NetworkResult testConnection();
    NetworkResult fetchPlatforms(std::vector<RommPlatform>& outPlatforms);
    NetworkResult fetchLibrary(std::vector<RommGame>& outGames, const std::string& platform = "");
    NetworkResult uploadSave(uint64_t gameId, const std::string& filePath);
    NetworkResult downloadSave(uint64_t gameId, const std::string& destPath);

private:
    std::string buildUrl(const std::string& endpointPath) const;
    std::string getBaseEndpoint() const;
    std::string getApiKey() const;

    ConfigManager* m_configMgr = nullptr;
};

} // namespace RomM3DS
