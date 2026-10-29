#include "network_manager.hpp"
#include "config_manager.hpp"
#include "sync_manager.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <sys/stat.h>

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

size_t headerCallback(char* buffer, size_t size, size_t nmemb, void* userp) {
    (void)userp;
    size_t totalBytes = size * nmemb;
    if (totalBytes >= 5) {
        if (strncasecmp(buffer, "Date:", 5) == 0) {
            std::string line(buffer + 5, totalBytes - 5);
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(0, 1);
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
            time_t serverTime = curl_getdate(line.c_str(), nullptr);
            if (serverTime > 0) {
                SyncManager::updateServerTimeOffset(serverTime);
            }
        }
    }
    return totalBytes;
}

size_t stringWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalBytes = size * nmemb;
    auto* str = static_cast<std::string*>(userp);
    str->append(static_cast<char*>(contents), totalBytes);
    return totalBytes;
}

size_t fileWriteCallback(void* ptr, size_t size, size_t nmemb, void* stream) {
    return fwrite(ptr, size, nmemb, static_cast<FILE*>(stream));
}

std::string getSafeString(const json& j, const std::string& key, const std::string& fallback = "") {
    if (j.contains(key) && !j[key].is_null()) {
        if (j[key].is_string()) {
            return j[key].get<std::string>();
        }
    }
    return fallback;
}

uint64_t getSafeUint64(const json& j, const std::string& key, uint64_t fallback = 0) {
    if (j.contains(key) && !j[key].is_null()) {
        if (j[key].is_number_unsigned()) {
            return j[key].get<uint64_t>();
        } else if (j[key].is_number_integer()) {
            int64_t val = j[key].get<int64_t>();
            return (val >= 0) ? static_cast<uint64_t>(val) : fallback;
        } else if (j[key].is_string()) {
            const std::string& s = j[key].get_ref<const std::string&>();
            if (!s.empty()) {
                char* end = nullptr;
                unsigned long long v = std::strtoull(s.c_str(), &end, 10);
                if (end != s.c_str()) {
                    return static_cast<uint64_t>(v);
                }
            }
        }
    }
    return fallback;
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
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
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

NetworkResult NetworkManager::fetchPlatforms(std::vector<RommPlatform>& outPlatforms) {
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

    std::string url = buildUrl("/api/platforms");
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
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
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
                json items = parsed.is_array() ? parsed :
                             (parsed.contains("items") ? parsed["items"] :
                             (parsed.contains("platforms") ? parsed["platforms"] : json::array()));
                for (const auto& item : items) {
                    if (!item.is_object()) continue;
                    RommPlatform p;
                    p.id = getSafeUint64(item, "id", 0);
                    p.name = getSafeString(item, "name", getSafeString(item, "display_name", getSafeString(item, "custom_name", "")));
                    p.slug = getSafeString(item, "slug", getSafeString(item, "platform_slug", ""));
                    p.romCount = static_cast<int>(getSafeUint64(item, "rom_count", getSafeUint64(item, "roms_count", 0)));
                    if (p.name.empty()) {
                        p.name = p.slug;
                    }
                    if (!p.slug.empty() || !p.name.empty()) {
                        outPlatforms.push_back(p);
                    }
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

NetworkResult NetworkManager::fetchLibrary(std::vector<RommGame>& outGames, uint64_t platformId, const std::string& platformSlug) {
    NetworkResult result;

    if (!s_networkInitialized) {
        result.errorMessage = "Network is not initialized.";
        return result;
    }

    size_t offset = 0;
    const size_t limit = 100;
    int total = -1;

    std::string baseQuery;
    if (platformId > 0) {
        baseQuery += "platform_id=" + std::to_string(platformId);
    } else if (!platformSlug.empty()) {
        baseQuery += "platform_slug=" + platformSlug;
    }

    std::string apiKey = getApiKey();

    while (true) {
        CURL* curl = curl_easy_init();
        if (!curl) {
            result.errorMessage = "Failed to initialize curl.";
            return result;
        }

        std::string pageQuery = baseQuery;
        if (!pageQuery.empty()) pageQuery += "&";
        pageQuery += "limit=" + std::to_string(limit) + "&offset=" + std::to_string(offset);

        std::string url = buildUrl("/api/roms?" + pageQuery);
        std::string responseData;

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Accept: application/json");

        if (!apiKey.empty()) {
            std::string authHeader = "Authorization: Bearer " + apiKey;
            std::string xApiKeyHeader = "X-API-Key: " + apiKey;
            headers = curl_slist_append(headers, authHeader.c_str());
            headers = curl_slist_append(headers, xApiKeyHeader.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stringWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseData);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "romm3ds/0.1.0");

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            result.errorMessage = curl_easy_strerror(res);
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            break;
        }

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.httpCode);
        result.responseBody = responseData;
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (result.httpCode < 200 || result.httpCode >= 300) {
            result.errorMessage = "HTTP Error " + std::to_string(result.httpCode);
            break;
        }

        result.success = true;

        json parsed = json::parse(responseData, nullptr, false);
        if (parsed.is_discarded()) {
            break;
        }

        json items = parsed.is_array() ? parsed : (parsed.contains("items") ? parsed["items"] : json::array());
        if (items.empty()) {
            break;
        }

        for (const auto& item : items) {
            if (!item.is_object()) continue;
            RommGame game;
            game.id = getSafeUint64(item, "id", 0);
            game.platformId = getSafeUint64(item, "platform_id", 0);
            game.title = getSafeString(item, "name", getSafeString(item, "title", ""));
            game.platform = getSafeString(item, "platform_slug", getSafeString(item, "platform", ""));
            game.romPath = getSafeString(item, "fs_name", getSafeString(item, "rom_path", ""));
            game.savePath = getSafeString(item, "save_path", "");
            game.updatedAt = getSafeString(item, "updated_at", getSafeString(item, "updatedAt", ""));
            outGames.push_back(game);
        }

        if (parsed.is_object() && parsed.contains("total")) {
            total = static_cast<int>(getSafeUint64(parsed, "total", (uint64_t)-1));
        }

        offset += items.size();
        if (total >= 0 && static_cast<int>(offset) >= total) {
            break;
        }
        if (items.size() < limit) {
            break;
        }
    }

    return result;
}

NetworkResult NetworkManager::fetchSaves(std::vector<RommSave>& outSaves, uint64_t romId) {
    NetworkResult result;
    outSaves.clear();

    if (!s_networkInitialized) {
        result.errorMessage = "Network is not initialized.";
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.errorMessage = "Failed to initialize curl.";
        return result;
    }

    std::string endpoint = "/api/saves";
    if (romId != 0) {
        endpoint += "?rom_id=" + std::to_string(romId) + "&limit=1000";
    } else {
        endpoint += "?limit=1000";
    }
    std::string url = buildUrl(endpoint);
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
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
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
        result.success = (result.httpCode >= 200 && result.httpCode < 300);

        if (result.success && !responseData.empty()) {
            json parsed = json::parse(responseData, nullptr, false);
            if (!parsed.is_discarded()) {
                json items = parsed.is_array() ? parsed : (parsed.contains("items") ? parsed["items"] : json::array());
                for (const auto& item : items) {
                    if (!item.is_object()) continue;
                    RommSave s;
                    s.id = getSafeUint64(item, "id", 0);
                    s.romId = getSafeUint64(item, "rom_id", getSafeUint64(item, "romId", 0));
                    s.fileName = getSafeString(item, "file_name", getSafeString(item, "filename", ""));
                    std::string up = getSafeString(item, "updated_at");
                    if (up.empty()) up = getSafeString(item, "updatedAt");
                    if (up.empty()) up = getSafeString(item, "created_at");
                    if (up.empty()) up = getSafeString(item, "createdAt");
                    s.updatedAt = up;
                    outSaves.push_back(s);
                }
            } else {
                result.success = false;
                result.errorMessage = "Failed to parse saves JSON response.";
            }
        }
    } else {
        result.errorMessage = curl_easy_strerror(res);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}

NetworkResult NetworkManager::uploadSave(uint64_t gameId, const std::string& filePath, const std::string& remoteFileName) {
    NetworkResult result;

    if (!s_networkInitialized) {
        result.errorMessage = "Network is not initialized.";
        return result;
    }

    struct stat fileStat;
    if (filePath.empty() || stat(filePath.c_str(), &fileStat) != 0 || !S_ISREG(fileStat.st_mode)) {
        result.errorMessage = "Save file does not exist or is a directory: " + filePath;
        return result;
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.errorMessage = "Failed to initialize curl.";
        return result;
    }

    // RomM endpoint: POST /api/saves?rom_id={gameId}
    std::string url = buildUrl("/api/saves?rom_id=" + std::to_string(gameId));
    std::string responseData;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");

    std::string apiKey = getApiKey();
    if (!apiKey.empty()) {
        std::string authHeader = "Authorization: Bearer " + apiKey;
        headers = curl_slist_append(headers, authHeader.c_str());
    }

    curl_mime* mime = curl_mime_init(curl);

    // Form field: rom_id
    curl_mimepart* partRomId = curl_mime_addpart(mime);
    curl_mime_name(partRomId, "rom_id");
    curl_mime_data(partRomId, std::to_string(gameId).c_str(), CURL_ZERO_TERMINATED);

    // Form field: slot
    curl_mimepart* partSlot = curl_mime_addpart(mime);
    curl_mime_name(partSlot, "slot");
    curl_mime_data(partSlot, "0", CURL_ZERO_TERMINATED);

    // File field: saveFile (required by RomM FastAPI backend)
    curl_mimepart* partFile = curl_mime_addpart(mime);
    curl_mime_name(partFile, "saveFile");
    curl_mime_filedata(partFile, filePath.c_str());

    std::string uploadName = remoteFileName;
    if (uploadName.empty()) {
        size_t slashPos = filePath.find_last_of("/\\");
        uploadName = (slashPos != std::string::npos) ? filePath.substr(slashPos + 1) : filePath;
    }
    curl_mime_filename(partFile, uploadName.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
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
            if (!responseData.empty()) {
                result.errorMessage += ": " + responseData;
            }
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
    std::vector<RommSave> saves;
    NetworkResult res = fetchSaves(saves, gameId);
    if (!res.success) {
        return res;
    }
    if (saves.empty()) {
        NetworkResult err;
        err.errorMessage = "No remote save found in RomM for game ID " + std::to_string(gameId);
        return err;
    }
    return downloadSaveById(saves[0].id, destPath);
}

NetworkResult NetworkManager::downloadSaveById(uint64_t saveId, const std::string& destPath) {
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

    std::string url = buildUrl("/api/saves/" + std::to_string(saveId) + "/content");

    struct curl_slist* headers = nullptr;
    std::string apiKey = getApiKey();
    if (!apiKey.empty()) {
        std::string authHeader = "Authorization: Bearer " + apiKey;
        headers = curl_slist_append(headers, authHeader.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerCallback);
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
