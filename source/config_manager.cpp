#include "config_manager.hpp"
#include "common.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sys/stat.h>

namespace RomM3DS {

using json = nlohmann::json;

ConfigManager::ConfigManager() = default;

bool ConfigManager::ensureDirectories(const std::string& filePath) {
    size_t pos = 0;
    while ((pos = filePath.find('/', pos + 1)) != std::string::npos) {
        std::string sub = filePath.substr(0, pos);
        if (sub.empty() || sub == "sdmc:") {
            continue;
        }
        mkdir(sub.c_str(), 0777);
    }
    return true;
}

bool ConfigManager::loadConfig(const std::string& path) {
    const std::string& filePath = path.empty() ? RomM3DS::CONFIG_PATH : path;
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    json j = json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return false;
    }

    m_config.serverEndpoint = j.value("server_endpoint", "");
    m_config.apiKey = j.value("api_key", "");
    return true;
}

bool ConfigManager::saveConfig(const std::string& path) {
    const std::string& filePath = path.empty() ? RomM3DS::CONFIG_PATH : path;
    ensureDirectories(filePath);

    std::ofstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    json j = {
        {"server_endpoint", m_config.serverEndpoint},
        {"api_key", m_config.apiKey}
    };

    file << j.dump(4);
    return file.good();
}

bool ConfigManager::hasValidConfig() const {
    return m_config.isValid();
}

bool ConfigManager::loadMappings(const std::string& path) {
    const std::string& filePath = path.empty() ? RomM3DS::MAPPING_PATH : path;
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    json j = json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return false;
    }

    m_mappings.clear();
    if (j.contains("mappings") && j["mappings"].is_array()) {
        for (const auto& item : j["mappings"]) {
            GameMapping mapping;
            mapping.titleId = item.value("title_id", "");
            mapping.localName = item.value("local_name", "");
            mapping.remoteGameId = item.value("remote_game_id", (uint64_t)0);
            mapping.remoteTitle = item.value("remote_title", "");
            m_mappings.push_back(mapping);
        }
    }
    return true;
}

bool ConfigManager::saveMappings(const std::string& path) {
    const std::string& filePath = path.empty() ? RomM3DS::MAPPING_PATH : path;
    ensureDirectories(filePath);

    std::ofstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    json jArray = json::array();
    for (const auto& m : m_mappings) {
        jArray.push_back({
            {"title_id", m.titleId},
            {"local_name", m.localName},
            {"remote_game_id", m.remoteGameId},
            {"remote_title", m.remoteTitle}
        });
    }

    json root = {
        {"mappings", jArray}
    };

    file << root.dump(4);
    return file.good();
}

void ConfigManager::addOrUpdateMapping(const GameMapping& mapping) {
    for (auto& existing : m_mappings) {
        if (existing.titleId == mapping.titleId) {
            existing = mapping;
            return;
        }
    }
    m_mappings.push_back(mapping);
}

const GameMapping* ConfigManager::findMappingByTitleId(const std::string& titleId) const {
    for (const auto& mapping : m_mappings) {
        if (mapping.titleId == titleId) {
            return &mapping;
        }
    }
    return nullptr;
}

} // namespace RomM3DS
