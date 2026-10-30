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

    if (j.contains("server_endpoint") && j["server_endpoint"].is_string()) {
        m_config.serverEndpoint = j["server_endpoint"].get<std::string>();
    } else {
        m_config.serverEndpoint.clear();
    }
    if (j.contains("api_key") && j["api_key"].is_string()) {
        m_config.apiKey = j["api_key"].get<std::string>();
    } else {
        m_config.apiKey.clear();
    }
    m_config.ignoredTitleIds.clear();
    if (j.contains("ignored_title_ids") && j["ignored_title_ids"].is_array()) {
        for (const auto& el : j["ignored_title_ids"]) {
            if (el.is_string()) m_config.ignoredTitleIds.push_back(el.get<std::string>());
        }
    }
    m_config.romDirectories.clear();
    if (j.contains("rom_directories") && j["rom_directories"].is_array()) {
        for (const auto& el : j["rom_directories"]) {
            if (el.is_string()) m_config.romDirectories.push_back(el.get<std::string>());
        }
    }
    m_config.detectedTimezoneOffsetSeconds = (j.contains("detected_timezone_offset_seconds") && j["detected_timezone_offset_seconds"].is_number())
        ? j["detected_timezone_offset_seconds"].get<long>() : 0L;
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
        {"api_key", m_config.apiKey},
        {"ignored_title_ids", m_config.ignoredTitleIds},
        {"rom_directories", m_config.romDirectories},
        {"detected_timezone_offset_seconds", m_config.detectedTimezoneOffsetSeconds}
    };

    file << j.dump(4, ' ', false, json::error_handler_t::replace);
    return file.good();
}

bool ConfigManager::hasValidConfig() const {
    return m_config.isValid();
}

bool ConfigManager::isTitleIgnored(const std::string& titleIdHex) const {
    for (const auto& id : m_config.ignoredTitleIds) {
        if (id == titleIdHex) {
            return true;
        }
    }
    return false;
}

void ConfigManager::setIgnoreTitle(const std::string& titleIdHex, bool ignore) {
    auto it = std::find(m_config.ignoredTitleIds.begin(), m_config.ignoredTitleIds.end(), titleIdHex);
    if (ignore) {
        if (it == m_config.ignoredTitleIds.end()) {
            m_config.ignoredTitleIds.push_back(titleIdHex);
        }
    } else {
        if (it != m_config.ignoredTitleIds.end()) {
            m_config.ignoredTitleIds.erase(it);
        }
    }
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
            if (!item.is_object()) continue;
            GameMapping mapping;
            if (item.contains("title_id") && item["title_id"].is_string()) {
                mapping.titleId = item["title_id"].get<std::string>();
            }
            if (item.contains("local_name") && item["local_name"].is_string()) {
                mapping.localName = item["local_name"].get<std::string>();
            }
            if (item.contains("remote_game_id") && item["remote_game_id"].is_number()) {
                mapping.remoteGameId = item["remote_game_id"].get<uint64_t>();
            }
            if (item.contains("remote_title") && item["remote_title"].is_string()) {
                mapping.remoteTitle = item["remote_title"].get<std::string>();
            }
            if (item.contains("custom_save_path") && item["custom_save_path"].is_string()) {
                mapping.customSavePath = item["custom_save_path"].get<std::string>();
            } else if (item.contains("save_path") && item["save_path"].is_string()) {
                mapping.customSavePath = item["save_path"].get<std::string>();
            }
            if (item.contains("custom_save_path_explicit") && item["custom_save_path_explicit"].is_boolean()) {
                mapping.customSavePathExplicit = item["custom_save_path_explicit"].get<bool>();
            }
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
        json jItem = {
            {"title_id", m.titleId},
            {"local_name", m.localName},
            {"remote_game_id", m.remoteGameId},
            {"remote_title", m.remoteTitle}
        };
        if (m.customSavePathExplicit && !m.customSavePath.empty()) {
            jItem["custom_save_path"] = m.customSavePath;
            jItem["custom_save_path_explicit"] = true;
        }
        jArray.push_back(jItem);
    }

    json root = {
        {"mappings", jArray}
    };

    file << root.dump(4, ' ', false, json::error_handler_t::replace);
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
