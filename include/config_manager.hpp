#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace RomM3DS {

struct AppConfig {
    std::string serverEndpoint;
    std::string apiKey;
    std::vector<std::string> ignoredTitleIds;
    std::vector<std::string> romDirectories;
    long detectedTimezoneOffsetSeconds = 0;

    bool isValid() const {
        return !serverEndpoint.empty() && !apiKey.empty();
    }
};

struct GameMapping {
    std::string titleId;
    std::string localName;
    uint64_t remoteGameId = 0;
    std::string remoteTitle;
    std::string customSavePath;
};

class ConfigManager {
public:
    ConfigManager();

    bool loadConfig(const std::string& path = "");
    bool saveConfig(const std::string& path = "");
    bool hasValidConfig() const;

    const AppConfig& getConfig() const { return m_config; }
    void setConfig(const AppConfig& config) { m_config = config; }
    void setEndpoint(const std::string& endpoint) { m_config.serverEndpoint = endpoint; }
    void setApiKey(const std::string& apiKey) { m_config.apiKey = apiKey; }

    long getDetectedTimezoneOffsetSeconds() const { return m_config.detectedTimezoneOffsetSeconds; }
    void setDetectedTimezoneOffsetSeconds(long offset) { m_config.detectedTimezoneOffsetSeconds = offset; }

    const std::vector<std::string>& getRomDirectories() const { return m_config.romDirectories; }
    void setRomDirectories(const std::vector<std::string>& dirs) { m_config.romDirectories = dirs; }

    bool isTitleIgnored(const std::string& titleIdHex) const;
    void setIgnoreTitle(const std::string& titleIdHex, bool ignore);
    const std::vector<std::string>& getIgnoredTitleIds() const { return m_config.ignoredTitleIds; }

    bool loadMappings(const std::string& path = "");
    bool saveMappings(const std::string& path = "");

    const std::vector<GameMapping>& getMappings() const { return m_mappings; }
    void setMappings(const std::vector<GameMapping>& mappings) { m_mappings = mappings; }
    void addOrUpdateMapping(const GameMapping& mapping);
    const GameMapping* findMappingByTitleId(const std::string& titleId) const;

    static bool ensureDirectories(const std::string& filePath);

private:
    AppConfig m_config;
    std::vector<GameMapping> m_mappings;
};

} // namespace RomM3DS
