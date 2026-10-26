#include "network_manager.hpp"
#include "config_manager.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <cstdio>

#if defined(__3DS__) || defined(_3DS)
#include <3ds.h>
#include <malloc.h>
#endif

namespace RomM3DS {

using json = nlohmann::json;

namespace {

#if defined(__3DS__) || defined(_3DS)
static u32* s_socBuffer = nullptr;
static constexpr size_t SOC_BUFFERSIZE = 0x100000;
#endif
static bool s_networkInitialized = false;

size_t stringWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalBytes = size * nmemb;
    auto* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), totalBytes);
    return totalBytes;
}

size_t fileWriteCallback(void* ptr, size_t size, size_t nmemb, void* stream) {
    return fwrite(ptr, size, nmemb, static_cast<FILE*>(stream));
}

} // namespace

bool NetworkManager::initNetwork() {
    if (s_networkInitialized) {
        return true;
    }

#if defined(__3DS__) || defined(_3DS)
    s_socBuffer = static_cast<u32*>(memalign(0x1000, SOC_BUFFERSIZE));
    if (!s_socBuffer) {
        return false;
    }
    Result rc = socInit(s_socBuffer, SOC_BUFFERSIZE);
    if (R_FAILED(rc)) {
        free(s_socBuffer);
        s_socBuffer = nullptr;
        return false;
    }
#endif

    CURLcode code = curl_global_init(CURL_GLOBAL_ALL);
    if (code != CURLE_OK) {
#if defined(__3DS__) || defined(_3DS)
        socExit();
        if (s_socBuffer) {
            free(s_socBuffer);
            s_socBuffer = nullptr;
        }
#endif
        return false;
    }

    s_networkInitialized = true;
    return true;
}

void NetworkManager::exitNetwork() {
    if (!s_networkInitialized) {
        return;
    }

    curl_global_cleanup();

#if defined(__3DS__) || defined(_3DS)
    socExit();
    if (s_socBuffer) {
        free(s_socBuffer);
        s_socBuffer = nullptr;
    }
#endif

    s_networkInitialized = false;
}

bool NetworkManager::isNetworkInitialized() {
    return s_networkInitialized;
}

NetworkManager::NetworkManager(ConfigManager* configMgr)
    : m_configMgr(configMgr) {
}

NetworkManager::~NetworkManager() = default;

std::string NetworkManager::getBaseEndpoint() const {
    if (m_configMgr) {
        return m_configMgr->getConfig().serverEndpoint;
    }
    return "";
}

std::string NetworkManager::getApiKey() const {
    if (m_configMgr) {
        return m_configMgr->getConfig().apiKey;
    }
    return "";
}

std::string NetworkManager::buildUrl(const std::string& endpointPath) const {
    std::string base = getBaseEndpoint();
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    if (endpointPath.empty() || endpointPath.front() != '/') {
        return base + "/" + endpointPath;
    }
    return base + endpointPath;
}

NetworkResult NetworkManager::testConnection() {
    NetworkResult result;

    if (!s_networkInitialized) {
        result.errorMessage = "Network socket subsystem is not initialized.";
        return result;
    }

    std::string base = getBaseEndpoint();
    if (base.empty()) {
        result.errorMessage = "Server endpoint is empty.";
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.errorMessage = "Failed to initialize curl session.";
        return result;
    }

    std::string url = buildUrl("/api/heartbeat");
    std::string responseData;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");

    std::string apiKey = getApiKey();
    if (!apiKey.empty()) {
        std::string authHeader = "Authorization: Bearer " + apiKey;
        std::string xApiKeyHeader = "X-API-Key: " + apiKey;
        headers = curl_slist_append(headers, authHeader.c_str());
        headers = curl_slist_append(headers, xApiKeyHeader.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stringWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "romm3ds/0.1.0");

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.httpCode);
        result.responseBody = responseData;
        if (result.httpCode >= 200 && result.httpCode < 300) {
            result.success = true;
        } else if (result.httpCode == 401 || result.httpCode == 403) {
            result.errorMessage = "Authentication failed (Invalid API Key).";
        } else {
            result.errorMessage = "Server responded with HTTP " + std::to_string(result.httpCode);
        }
    } else {
        result.errorMessage = curl_easy_strerror(res);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}

NetworkResult NetworkManager::fetchLibrary(std::vector<RommGame>& outGames, const std::string& platform) {
    NetworkResult result;

    if (!s_networkInitialized) {
        result.errorMessage = "Network is not initialized.";
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.errorMessage = "Failed to initialize curl.";
        return result;
    }

    std::string path = "/api/roms";
    if (!platform.empty()) {
        path += "?platform_slug=" + platform;
    }
    std::string url = buildUrl(path);
    std::string responseData;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");

    std::string apiKey = getApiKey();
    if (!apiKey.empty()) {
        std::string authHeader = "Authorization: Bearer " + apiKey;
        headers = curl_slist_append(headers, authHeader.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stringWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "romm3ds/0.1.0");

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.httpCode);
        result.responseBody = responseData;
        if (result.httpCode >= 200 && result.httpCode < 300) {
            result.success = true;

            json parsed = json::parse(responseData, nullptr, false);
            if (!parsed.is_discarded()) {
                json items = parsed.is_array() ? parsed : (parsed.contains("items") ? parsed["items"] : json::array());
                for (const auto& item : items) {
                    RommGame game;
                    game.id = item.value("id", (uint64_t)0);
                    game.title = item.value("name", item.value("title", ""));
                    game.platform = item.value("platform_slug", item.value("platform", ""));
                    game.romPath = item.value("rom_path", "");
                    game.savePath = item.value("save_path", "");
                    game.updatedAt = item.value("updated_at", "");
                    outGames.push_back(game);
                }
            }
        } else {
            result.errorMessage = "HTTP Error " + std::to_string(result.httpCode);
        }
    } else {
        result.errorMessage = curl_easy_strerror(res);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}

NetworkResult NetworkManager::uploadSave(uint64_t gameId, const std::string& filePath) {
    NetworkResult result;

    if (!s_networkInitialized) {
        result.errorMessage = "Network is not initialized.";
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.errorMessage = "Failed to initialize curl.";
        return result;
    }

    std::string url = buildUrl("/api/roms/" + std::to_string(gameId) + "/saves");
    std::string responseData;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");

    std::string apiKey = getApiKey();
    if (!apiKey.empty()) {
        std::string authHeader = "Authorization: Bearer " + apiKey;
        headers = curl_slist_append(headers, authHeader.c_str());
    }

    curl_mime* mime = curl_mime_init(curl);
    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, filePath.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stringWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "romm3ds/0.1.0");

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.httpCode);
        result.responseBody = responseData;
        result.success = (result.httpCode >= 200 && result.httpCode < 300);
        if (!result.success) {
            result.errorMessage = "Upload failed with HTTP " + std::to_string(result.httpCode);
        }
    } else {
        result.errorMessage = curl_easy_strerror(res);
    }

    curl_mime_free(mime);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}

NetworkResult NetworkManager::downloadSave(uint64_t gameId, const std::string& destPath) {
    NetworkResult result;

    if (!s_networkInitialized) {
        result.errorMessage = "Network is not initialized.";
        return result;
    }

    FILE* fp = fopen(destPath.c_str(), "wb");
    if (!fp) {
        result.errorMessage = "Failed to open destination file for writing.";
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        result.errorMessage = "Failed to initialize curl.";
        return result;
    }

    std::string url = buildUrl("/api/roms/" + std::to_string(gameId) + "/saves/download");

    struct curl_slist* headers = nullptr;
    std::string apiKey = getApiKey();
    if (!apiKey.empty()) {
        std::string authHeader = "Authorization: Bearer " + apiKey;
        headers = curl_slist_append(headers, authHeader.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fileWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "romm3ds/0.1.0");

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);

    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.httpCode);
        result.success = (result.httpCode >= 200 && result.httpCode < 300);
        if (!result.success) {
            result.errorMessage = "Download failed with HTTP " + std::to_string(result.httpCode);
            remove(destPath.c_str());
        }
    } else {
        result.errorMessage = curl_easy_strerror(res);
        remove(destPath.c_str());
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}

} // namespace RomM3DS
