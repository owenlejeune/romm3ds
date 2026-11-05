#include "config_manager.hpp"
#include "common.hpp"

#include <nlohmann/json.hpp>
#include <mbedtls/gcm.h>
#include <mbedtls/sha256.h>
#include <mbedtls/base64.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sys/stat.h>

#if defined(__3DS__) || defined(_3DS)
#include <3ds.h>
#endif

namespace RomM3DS {

using json = nlohmann::json;

namespace {

constexpr const char* ENC_PREFIX = "v1:";
constexpr size_t GCM_IV_LEN = 12;
constexpr size_t GCM_TAG_LEN = 16;

void deriveConsoleKey(uint8_t outKey[32]) {
    uint8_t material[64];
    std::memset(material, 0, sizeof(material));

    const char* domainSalt = "romm3ds-v1-console-bound-key-0x0F17E";
    std::memcpy(material, domainSalt, std::strlen(domainSalt));

#if defined(__3DS__) || defined(_3DS)
    u64 consoleHash = 0;
    if (R_FAILED(CFGU_GenHashConsoleUnique(0x000F17E0, &consoleHash))) {
        consoleHash = 0x524F4D4D33445331ULL;
    }
    u32 deviceId = 0;
    s32 amInternalRes = 0;
    if (R_FAILED(AM_GetDeviceId(&amInternalRes, &deviceId))) {
        deviceId = 0x000F17E0U;
    }
    std::memcpy(material + 40, &consoleHash, sizeof(consoleHash));
    std::memcpy(material + 48, &deviceId, sizeof(deviceId));
#endif

    mbedtls_sha256(material, sizeof(material), outKey, 0);
}

void generateRandomIv(uint8_t iv[GCM_IV_LEN]) {
#if defined(__3DS__) || defined(_3DS)
    uint64_t ticks = svcGetSystemTick();
    uint64_t osTime = osGetTime();
    u64 consoleHash = 0;
    CFGU_GenHashConsoleUnique(static_cast<u32>(ticks & 0xFFFFFFFFU), &consoleHash);
#else
    uint64_t ticks = static_cast<uint64_t>(std::clock());
    uint64_t osTime = static_cast<uint64_t>(std::time(nullptr));
    uint64_t consoleHash = 0;
#endif
    static uint64_t s_counter = 0;
    ++s_counter;

    uint8_t seed[32];
    std::memset(seed, 0, sizeof(seed));
    std::memcpy(seed, &ticks, sizeof(ticks));
    std::memcpy(seed + 8, &osTime, sizeof(osTime));
    std::memcpy(seed + 16, &s_counter, sizeof(s_counter));
    std::memcpy(seed + 24, &consoleHash, sizeof(consoleHash));

    uint8_t digest[32];
    mbedtls_sha256(seed, sizeof(seed), digest, 0);
    std::memcpy(iv, digest, GCM_IV_LEN);
}

std::string encryptApiKey(const std::string& plain) {
    if (plain.empty()) {
        return "";
    }

    uint8_t key[32];
    deriveConsoleKey(key);

    uint8_t iv[GCM_IV_LEN];
    generateRandomIv(iv);

    uint8_t tag[GCM_TAG_LEN];
    std::vector<uint8_t> ciphertext(plain.size());

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(
            &gcm, MBEDTLS_GCM_ENCRYPT,
            plain.size(),
            iv, GCM_IV_LEN,
            reinterpret_cast<const uint8_t*>("romm3ds-apikey"), 14,
            reinterpret_cast<const uint8_t*>(plain.data()),
            ciphertext.data(),
            GCM_TAG_LEN, tag);
    }
    mbedtls_gcm_free(&gcm);
    std::memset(key, 0, sizeof(key));

    if (rc != 0) {
        return "";
    }

    std::vector<uint8_t> packed(GCM_IV_LEN + GCM_TAG_LEN + ciphertext.size());
    std::memcpy(packed.data(), iv, GCM_IV_LEN);
    std::memcpy(packed.data() + GCM_IV_LEN, tag, GCM_TAG_LEN);
    if (!ciphertext.empty()) {
        std::memcpy(packed.data() + GCM_IV_LEN + GCM_TAG_LEN, ciphertext.data(), ciphertext.size());
    }

    size_t olen = 0;
    mbedtls_base64_encode(nullptr, 0, &olen, packed.data(), packed.size());
    std::vector<unsigned char> b64(olen + 1, 0);
    if (mbedtls_base64_encode(b64.data(), b64.size(), &olen, packed.data(), packed.size()) != 0) {
        return "";
    }

    return std::string(ENC_PREFIX) + std::string(reinterpret_cast<char*>(b64.data()), olen);
}

std::string decryptApiKey(const std::string& encoded) {
    const size_t prefixLen = std::strlen(ENC_PREFIX);
    if (encoded.size() <= prefixLen || std::strncmp(encoded.c_str(), ENC_PREFIX, prefixLen) != 0) {
        return "";
    }

    std::string b64 = encoded.substr(prefixLen);
    size_t decodedLen = 0;
    mbedtls_base64_decode(nullptr, 0, &decodedLen,
                          reinterpret_cast<const unsigned char*>(b64.data()), b64.size());
    if (decodedLen <= GCM_IV_LEN + GCM_TAG_LEN) {
        return "";
    }

    std::vector<uint8_t> packed(decodedLen);
    if (mbedtls_base64_decode(packed.data(), packed.size(), &decodedLen,
                              reinterpret_cast<const unsigned char*>(b64.data()), b64.size()) != 0) {
        return "";
    }
    if (decodedLen <= GCM_IV_LEN + GCM_TAG_LEN) {
        return "";
    }

    const uint8_t* iv = packed.data();
    const uint8_t* tag = packed.data() + GCM_IV_LEN;
    const uint8_t* ciphertext = packed.data() + GCM_IV_LEN + GCM_TAG_LEN;
    const size_t cipherLen = decodedLen - (GCM_IV_LEN + GCM_TAG_LEN);

    uint8_t key[32];
    deriveConsoleKey(key);

    std::vector<uint8_t> plain(cipherLen);
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);

    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(
            &gcm, cipherLen,
            iv, GCM_IV_LEN,
            reinterpret_cast<const uint8_t*>("romm3ds-apikey"), 14,
            tag, GCM_TAG_LEN,
            ciphertext, plain.data());
    }
    mbedtls_gcm_free(&gcm);
    std::memset(key, 0, sizeof(key));

    if (rc != 0) {
        return "";
    }

    return std::string(reinterpret_cast<char*>(plain.data()), plain.size());
}

void parseMappingsArray(const json& arr, std::vector<GameMapping>& outMappings) {
    if (!arr.is_array()) return;
    for (const auto& item : arr) {
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
        if (!mapping.titleId.empty()) {
            bool updated = false;
            for (auto& existing : outMappings) {
                if (existing.titleId == mapping.titleId) {
                    existing = mapping;
                    updated = true;
                    break;
                }
            }
            if (!updated) {
                outMappings.push_back(mapping);
            }
        }
    }
}

json serializeMappingsArray(const std::vector<GameMapping>& mappings) {
    json jArray = json::array();
    for (const auto& m : mappings) {
        json jItem = {
            {"title_id", m.titleId},
            {"local_name", m.localName},
            {"remote_game_id", m.remoteGameId},
            {"remote_title", m.remoteTitle}
        };
        if (m.customSavePathExplicit) {
            jItem["custom_save_path"] = m.customSavePath;
            jItem["custom_save_path_explicit"] = true;
        }
        jArray.push_back(jItem);
    }
    return jArray;
}

} // namespace

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
    file.close();
    if (j.is_discarded() || !j.is_object()) {
        return false;
    }

    if (j.contains("server_endpoint") && j["server_endpoint"].is_string()) {
        m_config.serverEndpoint = j["server_endpoint"].get<std::string>();
    } else {
        m_config.serverEndpoint.clear();
    }

    bool hadPlaintextKey = false;
    m_config.apiKey.clear();
    if (j.contains("api_key_enc") && j["api_key_enc"].is_string()) {
        m_config.apiKey = decryptApiKey(j["api_key_enc"].get<std::string>());
    }
    if (m_config.apiKey.empty() && j.contains("api_key") && j["api_key"].is_string()) {
        std::string plain = j["api_key"].get<std::string>();
        if (!plain.empty()) {
            m_config.apiKey = plain;
            hadPlaintextKey = true;
        }
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

    if (j.contains("mappings") && j["mappings"].is_array()) {
        m_mappings.clear();
        parseMappingsArray(j["mappings"], m_mappings);
    }

    // Automatically migrate legacy mapping.json if it exists
    bool migratedLegacyMappings = false;
    if (path.empty()) {
        std::ifstream legacyMapFile(RomM3DS::MAPPING_PATH);
        if (legacyMapFile.is_open()) {
            json mj = json::parse(legacyMapFile, nullptr, false);
            legacyMapFile.close();
            if (!mj.is_discarded() && mj.is_object() && mj.contains("mappings")) {
                parseMappingsArray(mj["mappings"], m_mappings);
                migratedLegacyMappings = true;
            }
            remove(RomM3DS::MAPPING_PATH);
        }
    }

    if (hadPlaintextKey || migratedLegacyMappings) {
        saveConfig(filePath);
    }

    return true;
}

bool ConfigManager::saveConfig(const std::string& path) {
    const std::string& filePath = path.empty() ? RomM3DS::CONFIG_PATH : path;
    ensureDirectories(filePath);

    std::ofstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    std::string encryptedKey = encryptApiKey(m_config.apiKey);

    json j = {
        {"server_endpoint", m_config.serverEndpoint},
        {"api_key_enc", encryptedKey},
        {"ignored_title_ids", m_config.ignoredTitleIds},
        {"rom_directories", m_config.romDirectories},
        {"detected_timezone_offset_seconds", m_config.detectedTimezoneOffsetSeconds},
        {"mappings", serializeMappingsArray(m_mappings)}
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
    if (!path.empty()) {
        std::ifstream file(path);
        if (!file.is_open()) return false;
        json j = json::parse(file, nullptr, false);
        if (j.is_discarded() || !j.is_object()) return false;
        if (j.contains("mappings") && j["mappings"].is_array()) {
            m_mappings.clear();
            parseMappingsArray(j["mappings"], m_mappings);
            return true;
        }
        return false;
    }

    // Mappings are stored in config.json; also check if legacy mapping.json needs migration
    std::ifstream legacyMapFile(RomM3DS::MAPPING_PATH);
    if (legacyMapFile.is_open()) {
        json mj = json::parse(legacyMapFile, nullptr, false);
        legacyMapFile.close();
        if (!mj.is_discarded() && mj.is_object() && mj.contains("mappings")) {
            parseMappingsArray(mj["mappings"], m_mappings);
        }
        remove(RomM3DS::MAPPING_PATH);
        saveConfig();
        return true;
    }

    std::ifstream cfgFile(RomM3DS::CONFIG_PATH);
    if (!cfgFile.is_open()) {
        return !m_mappings.empty();
    }
    json cj = json::parse(cfgFile, nullptr, false);
    if (!cj.is_discarded() && cj.is_object() && cj.contains("mappings") && cj["mappings"].is_array()) {
        m_mappings.clear();
        parseMappingsArray(cj["mappings"], m_mappings);
        return true;
    }
    return !m_mappings.empty();
}

bool ConfigManager::saveMappings(const std::string& path) {
    if (!path.empty()) {
        ensureDirectories(path);
        std::ofstream file(path);
        if (!file.is_open()) return false;
        json root = { {"mappings", serializeMappingsArray(m_mappings)} };
        file << root.dump(4, ' ', false, json::error_handler_t::replace);
        return file.good();
    }
    return saveConfig();
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
