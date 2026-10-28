#include "sync_manager.hpp"
#include "common.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <unistd.h>

namespace RomM3DS {

using json = nlohmann::json;

namespace {

bool isPathPointingToMgbaOrGba(const std::string& path) {
    if (path.empty()) return false;
    std::string lower = path;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.find("mgba") != std::string::npos || lower.find(".gba") != std::string::npos) {
        return true;
    }
    size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) {
        std::string dir = path.substr(0, slash);
        DIR* d = opendir(dir.c_str());
        if (d) {
            struct dirent* e = nullptr;
            while ((e = readdir(d)) != nullptr) {
                if (e->d_name[0] == '.') continue;
                std::string fn = e->d_name;
                if (fn.size() >= 4) {
                    std::string ext = fn.substr(fn.size() - 4);
                    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    if (ext == ".gba") {
                        closedir(d);
                        return true;
                    }
                }
            }
            closedir(d);
        }
    }
    return false;
}

void clearDirectory(const std::string& dirPath) {
    DIR* d = opendir(dirPath.c_str());
    if (!d) return;
    struct dirent* ent = nullptr;
    while ((ent = readdir(d)) != nullptr) {
        if (std::strcmp(ent->d_name, ".") != 0 && std::strcmp(ent->d_name, "..") != 0) {
            std::string fullPath = dirPath + "/" + ent->d_name;
            struct stat st;
            if (stat(fullPath.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                clearDirectory(fullPath);
                rmdir(fullPath.c_str());
            } else {
                remove(fullPath.c_str());
            }
        }
    }
    closedir(d);
}

std::string findRegularSaveFile(const std::string& dirPath, const std::string& preferredName = "") {
    if (!preferredName.empty()) {
        std::string directPreferred = dirPath + "/" + preferredName;
        struct stat st;
        if (stat(directPreferred.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            return directPreferred;
        }
    }

    std::string directMain = dirPath + "/main";
    struct stat mainSt;
    if (stat(directMain.c_str(), &mainSt) == 0 && S_ISREG(mainSt.st_mode)) {
        return directMain;
    }

    std::vector<std::string> regularFiles;
    std::function<void(const std::string&)> scanDir = [&](const std::string& currentDir) {
        DIR* d = opendir(currentDir.c_str());
        if (!d) return;

        struct dirent* ent = nullptr;
        while ((ent = readdir(d)) != nullptr) {
            if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0) {
                continue;
            }
            std::string fullPath = currentDir + "/" + ent->d_name;
            struct stat st;
            if (stat(fullPath.c_str(), &st) != 0) {
                continue;
            }
            if (S_ISDIR(st.st_mode)) {
                scanDir(fullPath);
            } else if (S_ISREG(st.st_mode)) {
                regularFiles.push_back(fullPath);
            }
        }
        closedir(d);
    };

    scanDir(dirPath);

    if (regularFiles.empty()) {
        return "";
    }

    if (regularFiles.size() == 1) {
        return regularFiles[0];
    }

    for (const auto& f : regularFiles) {
        size_t pos = f.find_last_of("/\\");
        std::string name = (pos != std::string::npos) ? f.substr(pos + 1) : f;
        if (name == "main") return f;
    }

    if (!preferredName.empty()) {
        for (const auto& f : regularFiles) {
            size_t pos = f.find_last_of("/\\");
            std::string name = (pos != std::string::npos) ? f.substr(pos + 1) : f;
            if (name == preferredName) return f;
        }
    }

    for (const auto& f : regularFiles) {
        size_t pos = f.find_last_of('.');
        if (pos != std::string::npos) {
            std::string ext = f.substr(pos);
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext == ".sav" || ext == ".srm" || ext == ".dat") {
                return f;
            }
        }
    }

    std::string largestFile = regularFiles[0];
    off_t maxSize = 0;
    for (const auto& f : regularFiles) {
        struct stat st;
        if (stat(f.c_str(), &st) == 0 && st.st_size > maxSize) {
            maxSize = st.st_size;
            largestFile = f;
        }
    }

    return largestFile;
}

} // namespace


SyncManager::SyncManager(ConfigManager* cfg, NetworkManager* net, SaveManager* save)
    : m_configMgr(cfg), m_netMgr(net), m_saveMgr(save) {
    clearLibraryCache();
}

SyncManager::~SyncManager() {
    clearLibraryCache();
}

std::string SyncManager::getStagingDirectory(uint64_t titleId) {
    return "sdmc:/3ds/romm3ds/saves/" + SaveManager::formatTitleId(titleId);
}

std::string SyncManager::formatTimestamp(time_t t) {
    if (t <= 0) return "Unknown";
    struct tm* tmInfo = localtime(&t);
    if (!tmInfo) return "Unknown";
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmInfo);
    return std::string(buf);
}

std::string SyncManager::formatIsoTimestamp(const std::string& iso) {
    if (iso.empty()) return "Unknown";
    int y = 0, m = 0, d = 0, h = 0, min = 0, s = 0;
    if (sscanf(iso.c_str(), "%d-%d-%d%*c%d:%d:%d", &y, &m, &d, &h, &min, &s) >= 6) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", y, m, d, h, min, s);
        return std::string(buf);
    }
    return iso;
}

time_t SyncManager::parseIsoTimestamp(const std::string& iso) {
    if (iso.empty()) return 0;
    int y = 0, m = 0, d = 0, h = 0, min = 0, s = 0;
    if (sscanf(iso.c_str(), "%d-%d-%d%*c%d:%d:%d", &y, &m, &d, &h, &min, &s) >= 6) {
        struct tm t = {};
        t.tm_year = y - 1900;
        t.tm_mon = m - 1;
        t.tm_mday = d;
        t.tm_hour = h;
        t.tm_min = min;
        t.tm_sec = s;
        t.tm_isdst = -1;
        return mktime(&t);
    }
    return 0;
}

std::string SyncManager::normalizeName(const std::string& str) {
    std::string out;
    for (char c : str) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

bool SyncManager::matchesFuzzy(const std::string& s1, const std::string& s2) {
    std::string n1 = normalizeName(s1);
    std::string n2 = normalizeName(s2);
    if (n1.empty() || n2.empty()) {
        return false;
    }
    return (n1 == n2) || (n1.find(n2) != std::string::npos) || (n2.find(n1) != std::string::npos);
}

std::string SyncManager::getFriendlyPlatformName(const std::string& slug) {
    std::string s = slug;
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (s == "3ds") return "Nintendo 3DS";
    if (s == "nds" || s == "ds") return "Nintendo DS";
    if (s == "gba") return "Game Boy Advance";
    if (s == "gbc") return "Game Boy Color";
    if (s == "gb" || s == "dmg") return "Game Boy";
    if (s == "nes" || s == "famicom" || s == "fc") return "Nintendo NES";
    if (s == "snes" || s == "sfc") return "Super Nintendo";
    if (s == "n64") return "Nintendo 64";
    if (s == "gg" || s == "gamegear") return "Sega Game Gear";
    if (s == "genesis" || s == "megadrive" || s == "md") return "Sega Genesis";
    if (s == "sms" || s == "mastersystem") return "Sega Master System";
    if (s == "pce" || s == "tg16" || s == "turbografx16") return "PC Engine / TG16";
    if (s == "psx" || s == "ps1" || s == "playstation") return "PlayStation";
    if (s == "psp") return "PlayStation Portable";

    if (!s.empty()) {
        s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    }
    return s.empty() ? "Other" : s;
}

bool SyncManager::isPlatformCompatible(const std::string& localConsole, const std::string& remotePlatformSlug) {
    if (localConsole.empty() || remotePlatformSlug.empty()) return true;

    std::string lc = localConsole;
    for (char& c : lc) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string rp = remotePlatformSlug;
    for (char& c : rp) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (lc == "3ds" && rp == "3ds") return true;
    if ((lc == "ds" || lc == "nds") && (rp == "nds" || rp == "ds")) return true;
    if (lc == "gba" && rp == "gba") return true;
    if (lc == "gbc" && rp == "gbc") return true;
    if (lc == "gb" && (rp == "gb" || rp == "dmg")) return true;
    if (lc == "nes" && (rp == "nes" || rp == "famicom" || rp == "fc")) return true;
    if (lc == "snes" && (rp == "snes" || rp == "sfc")) return true;
    if (lc == "gg" && (rp == "gg" || rp == "gamegear")) return true;
    if (lc == "pce" && (rp == "pce" || rp == "tg16" || rp == "turbografx16")) return true;

    // 3DS installed forwarder CIAs can run retro games
    if (lc == "3ds") {
        return (rp == "gba" || rp == "nds" || rp == "ds" || rp == "gbc" || rp == "gb" ||
                rp == "dmg" || rp == "snes" || rp == "sfc" || rp == "nes" || rp == "famicom" ||
                rp == "fc" || rp == "gg" || rp == "gamegear" || rp == "pce");
    }

    return false;
}

void SyncManager::consolidatePlatforms() {
    std::map<std::string, int> slugCounts;
    for (const auto& g : m_remoteGames) {
        if (!g.platform.empty()) {
            slugCounts[g.platform]++;
        } else {
            slugCounts["other"]++;
        }
    }

    for (auto& p : m_platforms) {
        if (p.name.empty()) {
            p.name = getFriendlyPlatformName(p.slug);
        }
        auto it = slugCounts.find(p.slug);
        if (it != slugCounts.end()) {
            if (p.romCount <= 0 || it->second > p.romCount) {
                p.romCount = it->second;
            }
            slugCounts.erase(it);
        }
    }

    for (const auto& pair : slugCounts) {
        RommPlatform p;
        p.slug = pair.first;
        p.name = getFriendlyPlatformName(pair.first);
        p.romCount = pair.second;
        m_platforms.push_back(p);
    }

    if (!m_remoteGames.empty()) {
        m_platforms.erase(
            std::remove_if(m_platforms.begin(), m_platforms.end(),
                [](const RommPlatform& p) { return p.romCount <= 0; }),
            m_platforms.end()
        );
    }

    std::sort(m_platforms.begin(), m_platforms.end(),
        [](const RommPlatform& a, const RommPlatform& b) {
            return a.name < b.name;
        });
}

bool SyncManager::isPlatformLoaded(uint64_t platformId, const std::string& platformSlug) const {
    std::string key = std::to_string(platformId) + ":" + platformSlug;
    if (m_loadedPlatforms.count(key) > 0) return true;

    for (const auto& g : m_remoteGames) {
        if (platformId > 0 && g.platformId == platformId) return true;
        if (!platformSlug.empty() && strcasecmp(g.platform.c_str(), platformSlug.c_str()) == 0) return true;
    }
    return false;
}

bool SyncManager::loadGamesForPlatform(uint64_t platformId, const std::string& platformSlug, bool forceRefresh) {
    std::string key = std::to_string(platformId) + ":" + platformSlug;
    if (!forceRefresh && isPlatformLoaded(platformId, platformSlug)) {
        m_lastMessage = "Loaded from cache.";
        return true;
    }

    if (!m_netMgr) return false;

    std::vector<RommGame> fetchedGames;
    NetworkResult res = m_netMgr->fetchLibrary(fetchedGames, platformId, platformSlug);
    if (!res.success) {
        if (isPlatformLoaded(platformId, platformSlug)) {
            m_lastMessage = "Network failed; using cached games.";
            return true;
        }
        m_lastMessage = "Failed to load games: " + res.errorMessage;
        return false;
    }

    for (const auto& fg : fetchedGames) {
        bool exists = false;
        for (auto& existing : m_remoteGames) {
            if (existing.id == fg.id) {
                existing = fg;
                exists = true;
                break;
            }
        }
        if (!exists) {
            m_remoteGames.push_back(fg);
        }
    }

    std::sort(m_remoteGames.begin(), m_remoteGames.end(), [](const RommGame& a, const RommGame& b) {
        int cmp = strcasecmp(a.title.c_str(), b.title.c_str());
        if (cmp != 0) return cmp < 0;
        return a.id < b.id;
    });

    m_loadedPlatforms.insert(key);

    for (auto& p : m_platforms) {
        if ((platformId > 0 && p.id == platformId) || (!platformSlug.empty() && p.slug == platformSlug)) {
            p.romCount = std::max(p.romCount, static_cast<int>(fetchedGames.size()));
        }
    }

    saveLibraryCache();

    m_lastMessage = "Loaded " + std::to_string(fetchedGames.size()) + " games for console.";
    return true;
}

bool SyncManager::saveLibraryCache(const std::string& path) {
    const std::string& filePath = path.empty() ? RomM3DS::CACHE_PATH : path;
    ConfigManager::ensureDirectories(filePath);

    std::ofstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    json jPlatforms = json::array();
    for (const auto& p : m_platforms) {
        jPlatforms.push_back({
            {"id", p.id},
            {"name", p.name},
            {"slug", p.slug},
            {"rom_count", p.romCount}
        });
    }

    json jGames = json::array();
    for (const auto& g : m_remoteGames) {
        jGames.push_back({
            {"id", g.id},
            {"platform_id", g.platformId},
            {"title", g.title},
            {"platform", g.platform},
            {"rom_path", g.romPath},
            {"save_path", g.savePath},
            {"updated_at", g.updatedAt}
        });
    }

    json jLoaded = json::array();
    for (const auto& lp : m_loadedPlatforms) {
        jLoaded.push_back(lp);
    }

    json root = {
        {"platforms", jPlatforms},
        {"games", jGames},
        {"loaded_platforms", jLoaded}
    };

    file << root.dump(4);
    return file.good();
}

bool SyncManager::loadLibraryCache(const std::string& path) {
    const std::string& filePath = path.empty() ? RomM3DS::CACHE_PATH : path;
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    json root = json::parse(file, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return false;
    }

    if (root.contains("platforms") && root["platforms"].is_array()) {
        for (const auto& item : root["platforms"]) {
            RommPlatform p;
            p.id = item.value("id", (uint64_t)0);
            p.name = item.value("name", "");
            p.slug = item.value("slug", "");
            p.romCount = item.value("rom_count", 0);

            bool exists = false;
            for (auto& existing : m_platforms) {
                if ((p.id > 0 && existing.id == p.id) || (!p.slug.empty() && existing.slug == p.slug)) {
                    exists = true;
                    if (existing.name.empty()) existing.name = p.name;
                    if (existing.romCount <= 0) existing.romCount = p.romCount;
                    break;
                }
            }
            if (!exists && (!p.slug.empty() || !p.name.empty())) {
                m_platforms.push_back(p);
            }
        }
    }

    if (root.contains("games") && root["games"].is_array()) {
        for (const auto& item : root["games"]) {
            RommGame g;
            g.id = item.value("id", (uint64_t)0);
            g.platformId = item.value("platform_id", (uint64_t)0);
            g.title = item.value("title", "");
            g.platform = item.value("platform", "");
            g.romPath = item.value("rom_path", "");
            g.savePath = item.value("save_path", "");
            g.updatedAt = item.value("updated_at", "");

            bool exists = false;
            for (auto& existing : m_remoteGames) {
                if (existing.id == g.id) {
                    exists = true;
                    break;
                }
            }
            if (!exists && g.id > 0) {
                m_remoteGames.push_back(g);
            }
        }
    }

    if (root.contains("loaded_platforms") && root["loaded_platforms"].is_array()) {
        for (const auto& lp : root["loaded_platforms"]) {
            if (lp.is_string()) {
                m_loadedPlatforms.insert(lp.get<std::string>());
            }
        }
    }

    return true;
}

void SyncManager::clearLibraryCache() {
    m_loadedPlatforms.clear();
    remove(RomM3DS::CACHE_PATH);
}

bool SyncManager::refresh() {
    m_items.clear();
    m_remoteGames.clear();
    m_platforms.clear();
    clearLibraryCache();

    if (m_configMgr) {
        m_configMgr->loadMappings();
    }

    std::map<uint64_t, RommSave> remoteSaveMap;
    if (m_netMgr) {
        NetworkResult pRes = m_netMgr->fetchPlatforms(m_platforms);
        NetworkResult lRes = m_netMgr->fetchLibrary(m_remoteGames, 0, "");
        if (pRes.success || lRes.success) {
            consolidatePlatforms();
            saveLibraryCache();
        } else {
            loadLibraryCache();
            consolidatePlatforms();
        }

        std::vector<RommSave> allSaves;
        if (m_netMgr->fetchSaves(allSaves).success) {
            for (const auto& s : allSaves) {
                if (remoteSaveMap.find(s.romId) == remoteSaveMap.end()) {
                    remoteSaveMap[s.romId] = s;
                } else {
                    time_t existingTime = parseIsoTimestamp(remoteSaveMap[s.romId].updatedAt);
                    time_t newTime = parseIsoTimestamp(s.updatedAt);
                    if (newTime > existingTime || (newTime == existingTime && (s.updatedAt > remoteSaveMap[s.romId].updatedAt || s.id > remoteSaveMap[s.romId].id))) {
                        remoteSaveMap[s.romId] = s;
                    }
                }
            }
        }
    }

    std::vector<InstalledTitle> localTitles;
    if (m_saveMgr) {
        std::vector<std::string> romDirs;
        if (m_configMgr) {
            romDirs = m_configMgr->getRomDirectories();
        }
        localTitles = m_saveMgr->scanInstalledTitles(romDirs);
    }

    for (const auto& local : localTitles) {
        SyncItem item;
        item.localTitle = local;
        item.isIgnored = (m_configMgr && m_configMgr->isTitleIgnored(local.titleIdHex));

        if (!item.isIgnored) {
            const GameMapping* mapping = m_configMgr ? m_configMgr->findMappingByTitleId(local.titleIdHex) : nullptr;
            if (mapping && !mapping->customSavePath.empty()) {
                item.localTitle.savePath = mapping->customSavePath;
                struct stat st;
                if (stat(item.localTitle.savePath.c_str(), &st) == 0 && st.st_size > 0) {
                    item.localTitle.hasSave = true;
                    item.localTitle.saveTime = st.st_mtime;
                }
                if (item.localTitle.console == "3DS" && isPathPointingToMgbaOrGba(mapping->customSavePath)) {
                    item.localTitle.console = "GBA";
                }
            }

            if (mapping && mapping->remoteGameId != 0) {
                item.isMatched = true;
                item.remoteGameId = mapping->remoteGameId;
                item.remoteTitle = mapping->remoteTitle;

                auto it = remoteSaveMap.find(item.remoteGameId);
                if (it != remoteSaveMap.end()) {
                    item.remoteHasSave = true;
                    item.remoteSaveUpdatedAt = it->second.updatedAt;
                    item.remoteSaveTime = parseIsoTimestamp(it->second.updatedAt);
                } else {
                    item.remoteHasSave = false;
                    item.remoteSaveUpdatedAt = "";
                    item.remoteSaveTime = 0;
                }

                if (item.localTitle.console == "3DS" && !item.localTitle.savePath.empty()) {
                    for (const auto& r : m_remoteGames) {
                        if (r.id == item.remoteGameId && !r.platform.empty()) {
                            std::string pUpper = r.platform;
                            for (char& c : pUpper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                            if (pUpper != "3DS") {
                                item.localTitle.console = pUpper;
                            }
                            break;
                        }
                    }
                }
            } else {
                // First attempt match with platform-compatible games
                const RommGame* matchTarget = nullptr;
                for (const auto& r : m_remoteGames) {
                    if (isPlatformCompatible(local.console, r.platform) && matchesFuzzy(local.name, r.title)) {
                        matchTarget = &r;
                        break;
                    }
                }
                // Fallback: match any title
                if (!matchTarget) {
                    for (const auto& r : m_remoteGames) {
                        if (matchesFuzzy(local.name, r.title)) {
                            matchTarget = &r;
                            break;
                        }
                    }
                }

                if (matchTarget) {
                    item.isMatched = true;
                    item.remoteGameId = matchTarget->id;
                    item.remoteTitle = matchTarget->title;

                    auto it = remoteSaveMap.find(matchTarget->id);
                    if (it != remoteSaveMap.end()) {
                        item.remoteHasSave = true;
                        item.remoteSaveUpdatedAt = it->second.updatedAt;
                        item.remoteSaveTime = parseIsoTimestamp(it->second.updatedAt);
                    } else {
                        item.remoteHasSave = false;
                        item.remoteSaveUpdatedAt = "";
                        item.remoteSaveTime = 0;
                    }

                    if (item.localTitle.console == "3DS" && !item.localTitle.savePath.empty() && !matchTarget->platform.empty()) {
                        std::string pUpper = matchTarget->platform;
                        for (char& c : pUpper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                        if (pUpper != "3DS") {
                            item.localTitle.console = pUpper;
                        }
                    }

                    if (m_configMgr) {
                        GameMapping newMap;
                        newMap.titleId = local.titleIdHex;
                        newMap.localName = local.name;
                        newMap.remoteGameId = matchTarget->id;
                        newMap.remoteTitle = matchTarget->title;
                        newMap.customSavePath = local.savePath;
                        m_configMgr->addOrUpdateMapping(newMap);
                    }
                }
            }
        }

        if (item.localTitle.saveTime == 0 && item.localTitle.hasSave && m_saveMgr) {
            item.localTitle.saveTime = m_saveMgr->getSaveTime(item.localTitle);
        }

        m_items.push_back(item);
    }

    std::sort(m_items.begin(), m_items.end(), [](const SyncItem& a, const SyncItem& b) {
        int cmp = strcasecmp(a.localTitle.name.c_str(), b.localTitle.name.c_str());
        if (cmp != 0) return cmp < 0;
        return a.localTitle.titleIdHex < b.localTitle.titleIdHex;
    });

    if (m_configMgr) {
        m_configMgr->saveMappings();
    }

    m_lastMessage = "Found " + std::to_string(m_items.size()) + " local titles, " +
                    std::to_string(m_platforms.size()) + " consoles, " +
                    std::to_string(m_remoteGames.size()) + " remote games.";
    return true;
}

bool SyncManager::manualMatch(size_t itemIndex, const RommGame& remoteGame) {
    if (itemIndex >= m_items.size()) {
        return false;
    }

    SyncItem& item = m_items[itemIndex];
    item.isMatched = true;
    item.remoteGameId = remoteGame.id;
    item.remoteTitle = remoteGame.title;

    std::vector<RommSave> existingSaves;
    if (m_netMgr && m_netMgr->fetchSaves(existingSaves, remoteGame.id).success && !existingSaves.empty()) {
        std::sort(existingSaves.begin(), existingSaves.end(), [](const RommSave& a, const RommSave& b) {
            time_t ta = SyncManager::parseIsoTimestamp(a.updatedAt);
            time_t tb = SyncManager::parseIsoTimestamp(b.updatedAt);
            if (ta != tb) return ta > tb;
            if (a.updatedAt != b.updatedAt) return a.updatedAt > b.updatedAt;
            return a.id > b.id;
        });
        item.remoteHasSave = true;
        item.remoteSaveUpdatedAt = existingSaves[0].updatedAt;
        item.remoteSaveTime = parseIsoTimestamp(existingSaves[0].updatedAt);
    } else {
        item.remoteHasSave = false;
        item.remoteSaveUpdatedAt = "";
        item.remoteSaveTime = 0;
    }

    if (item.localTitle.console == "3DS" && !item.localTitle.savePath.empty() && !remoteGame.platform.empty()) {
        std::string pUpper = remoteGame.platform;
        for (char& c : pUpper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (pUpper != "3DS") {
            item.localTitle.console = pUpper;
        }
    }

    if (m_configMgr) {
        GameMapping map;
        map.titleId = item.localTitle.titleIdHex;
        map.localName = item.localTitle.name;
        map.remoteGameId = remoteGame.id;
        map.remoteTitle = remoteGame.title;
        map.customSavePath = item.localTitle.savePath;
        m_configMgr->addOrUpdateMapping(map);
        m_configMgr->saveMappings();
    }

    m_lastMessage = "Matched " + item.localTitle.name + " -> " + remoteGame.title;
    return true;
}

bool SyncManager::setCustomSavePath(size_t itemIndex, const std::string& savePath) {
    if (itemIndex >= m_items.size()) return false;
    SyncItem& item = m_items[itemIndex];
    item.localTitle.savePath = savePath;

    if (!savePath.empty()) {
        struct stat st;
        if (stat(savePath.c_str(), &st) == 0 && st.st_size > 0) {
            item.localTitle.hasSave = true;
            item.localTitle.saveTime = st.st_mtime;
        } else {
            item.localTitle.hasSave = false;
            item.localTitle.saveTime = 0;
        }

        if (item.localTitle.console == "3DS" && isPathPointingToMgbaOrGba(savePath)) {
            item.localTitle.console = "GBA";
        }
    } else {
        // Reset to auto-detection
        if (item.localTitle.isFileRom) {
            item.localTitle.savePath = m_saveMgr ? m_saveMgr->findSaveFileForRom(item.localTitle.romPath, item.localTitle.console) : "";
        } else {
            item.localTitle.savePath = m_saveMgr ? SaveManager::findSaveForInstalledTitle(item.localTitle) : "";
        }
        item.localTitle.hasSave = m_saveMgr ? m_saveMgr->checkSaveExists(item.localTitle) : false;
        item.localTitle.saveTime = m_saveMgr ? m_saveMgr->getSaveTime(item.localTitle) : 0;
    }

    if (m_configMgr) {
        GameMapping map;
        const GameMapping* existing = m_configMgr->findMappingByTitleId(item.localTitle.titleIdHex);
        if (existing) {
            map = *existing;
        } else {
            map.titleId = item.localTitle.titleIdHex;
            map.localName = item.localTitle.name;
            map.remoteGameId = item.isMatched ? item.remoteGameId : 0;
            map.remoteTitle = item.isMatched ? item.remoteTitle : "";
        }
        map.customSavePath = savePath;
        m_configMgr->addOrUpdateMapping(map);
        m_configMgr->saveMappings();
    }

    if (!savePath.empty()) {
        m_lastMessage = "Set save path for " + item.localTitle.name;
    } else {
        m_lastMessage = "Reset save path for " + item.localTitle.name;
    }
    return true;
}

SyncAction SyncManager::evaluateItem(const SyncItem& item) {
    if (item.isIgnored || !item.isMatched) {
        return SyncAction::NO_SAVES;
    }

    bool localSave = item.localTitle.hasSave;
    bool remoteSave = item.remoteHasSave;

    if (remoteSave && !localSave) {
        return SyncAction::DOWNLOAD_REMOTE_TO_LOCAL;
    }
    if (localSave && !remoteSave) {
        return SyncAction::UPLOAD_LOCAL_TO_REMOTE;
    }
    if (localSave && remoteSave) {
        return SyncAction::CONFLICT;
    }

    return SyncAction::NO_SAVES;
}

bool SyncManager::forceUpload(SyncItem& item, LogCallback log) {
    auto emit = [&](const std::string& m) { if (log) log(m); };
    if (item.isIgnored) {
        emit("[WARN] Game is ignored: " + item.localTitle.name);
        m_lastMessage = "Game is ignored: " + item.localTitle.name;
        return false;
    }
    if (!item.isMatched || !m_saveMgr || !m_netMgr) {
        emit("[ERROR] Cannot upload: Unmatched game or subsystems missing.");
        m_lastMessage = "Cannot upload: Unmatched or subsystems missing.";
        return false;
    }

    emit("[Upload] " + item.localTitle.name);
    if (item.localTitle.isFileRom || !item.localTitle.savePath.empty()) {
        if (item.localTitle.isFileRom) {
            emit("ROM: " + item.localTitle.romPath);
        }
        std::string existing = item.localTitle.savePath;
        if (existing.empty() && item.localTitle.isFileRom) {
            existing = m_saveMgr->findSaveFileForRom(item.localTitle.romPath, item.localTitle.console);
        }
        if (!existing.empty()) {
            struct stat est;
            if (stat(existing.c_str(), &est) == 0) {
                emit("Existing local save: " + existing);
                emit("Local save size: " + std::to_string(est.st_size) + " B, Modified: " + formatTimestamp(est.st_mtime));
            }
        }
    }

    std::string stagingDir = getStagingDirectory(item.localTitle.titleId);
    clearDirectory(stagingDir);
    ConfigManager::ensureDirectories(stagingDir + "/");

    emit("Backing up local save to staging...");
    if (!m_saveMgr->backupSave(item.localTitle, stagingDir, log)) {
        emit("[ERROR] Failed to backup local save.");
        m_lastMessage = "Failed to backup local save from 3DS FS.";
        return false;
    }

    std::string remoteFileName;
    // 1. If RomM already has a save for this ROM, match that exact filename
    std::vector<RommSave> existingSaves;
    m_netMgr->fetchSaves(existingSaves, item.remoteGameId);
    if (!existingSaves.empty()) {
        std::sort(existingSaves.begin(), existingSaves.end(), [](const RommSave& a, const RommSave& b) {
            time_t ta = SyncManager::parseIsoTimestamp(a.updatedAt);
            time_t tb = SyncManager::parseIsoTimestamp(b.updatedAt);
            if (ta != tb) return ta > tb;
            if (a.updatedAt != b.updatedAt) return a.updatedAt > b.updatedAt;
            return a.id > b.id;
        });
        if (!existingSaves[0].fileName.empty()) {
            remoteFileName = existingSaves[0].fileName;
        }
    }

    std::string targetFile = findRegularSaveFile(stagingDir, remoteFileName);
    if (targetFile.empty()) {
        emit("[ERROR] No valid regular save file found in dump directory.");
        m_lastMessage = "No save files found in dump directory.";
        return false;
    }

    struct stat tst;
    if (stat(targetFile.c_str(), &tst) != 0 || !S_ISREG(tst.st_mode)) {
        emit("[ERROR] Selected save path is not a valid regular file: " + targetFile);
        m_lastMessage = "Selected save path is not a regular file.";
        return false;
    }
    long saveSize = static_cast<long>(tst.st_size);
    emit("Prepared save file: " + targetFile + " (" + std::to_string(saveSize) + " B)");

    if (remoteFileName.empty()) {
        // 2. Otherwise determine appropriate filename
        size_t slashPos = targetFile.find_last_of("/\\");
        std::string localBaseName = (slashPos != std::string::npos) ? targetFile.substr(slashPos + 1) : targetFile;
        if (localBaseName.find('.') != std::string::npos && localBaseName != "main") {
            remoteFileName = localBaseName;
        } else if (item.localTitle.console == "3DS" && item.localTitle.savePath.empty()) {
            remoteFileName = "main";
        } else {
            const RommGame* rGame = nullptr;
            for (const auto& rg : m_remoteGames) {
                if (rg.id == item.remoteGameId) {
                    rGame = &rg;
                    break;
                }
            }
            if (rGame && !rGame->romPath.empty()) {
                std::string base = rGame->romPath;
                size_t dotPos = base.find_last_of('.');
                if (dotPos != std::string::npos) {
                    base = base.substr(0, dotPos);
                }
                remoteFileName = base + ".sav";
            } else {
                remoteFileName = item.localTitle.name + ".sav";
            }
        }
    }

    emit("Target remote file: " + remoteFileName);
    emit("Uploading to RomM (Game ID: " + std::to_string(item.remoteGameId) + ")...");
    NetworkResult res = m_netMgr->uploadSave(item.remoteGameId, targetFile, remoteFileName);
    if (res.success) {
        item.remoteHasSave = true;
        emit("[SUCCESS] Successfully uploaded save to RomM!");
        m_lastMessage = "Successfully uploaded save for " + item.localTitle.name;
        return true;
    }

    emit("[ERROR] Upload failed: " + res.errorMessage);
    m_lastMessage = "Upload failed: " + res.errorMessage;
    return false;
}

bool SyncManager::forceDownload(SyncItem& item, LogCallback log) {
    auto emit = [&](const std::string& m) { if (log) log(m); };
    if (item.isIgnored) {
        emit("[WARN] Game is ignored: " + item.localTitle.name);
        m_lastMessage = "Game is ignored: " + item.localTitle.name;
        return false;
    }
    if (!item.isMatched || !m_saveMgr || !m_netMgr) {
        emit("[ERROR] Cannot download: Unmatched game or subsystems missing.");
        m_lastMessage = "Cannot download: Unmatched or subsystems missing.";
        return false;
    }

    emit("[Download] " + item.localTitle.name);
    emit("Console: " + item.localTitle.console);

    if (item.localTitle.isFileRom || !item.localTitle.savePath.empty()) {
        if (item.localTitle.isFileRom) {
            emit("ROM location: " + item.localTitle.romPath);
        }
        std::string existing = item.localTitle.savePath;
        if (existing.empty() && item.localTitle.isFileRom) {
            existing = m_saveMgr->findSaveFileForRom(item.localTitle.romPath, item.localTitle.console);
        }
        if (!existing.empty()) {
            struct stat est;
            if (stat(existing.c_str(), &est) == 0) {
                emit("Existing local save: " + existing);
                emit("Local save size: " + std::to_string(est.st_size) + " B, Modified: " + formatTimestamp(est.st_mtime));
            } else {
                emit("Existing local save location: " + existing + " (file not yet created)");
            }
        } else {
            emit("Existing local save location: None found on SD");
        }
    }

    std::string stagingDir = getStagingDirectory(item.localTitle.titleId);
    clearDirectory(stagingDir);
    ConfigManager::ensureDirectories(stagingDir + "/");

    emit("Fetching remote saves for game ID " + std::to_string(item.remoteGameId) + "...");
    std::vector<RommSave> saves;
    m_netMgr->fetchSaves(saves, item.remoteGameId);
    if (saves.empty()) {
        emit("[ERROR] No remote save found in RomM for " + item.remoteTitle);
        m_lastMessage = "No remote save found in RomM for " + item.remoteTitle;
        return false;
    }

    // Sort descending by timestamp and ID so the most recent save (.srm, .sav, etc.) is selected
    std::sort(saves.begin(), saves.end(), [](const RommSave& a, const RommSave& b) {
        time_t ta = SyncManager::parseIsoTimestamp(a.updatedAt);
        time_t tb = SyncManager::parseIsoTimestamp(b.updatedAt);
        if (ta != tb) return ta > tb;
        if (a.updatedAt != b.updatedAt) return a.updatedAt > b.updatedAt;
        return a.id > b.id;
    });

    emit("Selected remote save ID: " + std::to_string(saves[0].id));
    emit("Remote save file: " + saves[0].fileName + " (" + formatIsoTimestamp(saves[0].updatedAt) + ")");

    std::string localFileName = "main";
    if (item.localTitle.isFileRom || !item.localTitle.savePath.empty()) {
        if (!item.localTitle.savePath.empty()) {
            size_t slashPos = item.localTitle.savePath.find_last_of("/\\");
            localFileName = (slashPos != std::string::npos) ? item.localTitle.savePath.substr(slashPos + 1) : item.localTitle.savePath;
        } else {
            size_t slashPos = item.localTitle.romPath.find_last_of("/\\");
            std::string filename = (slashPos != std::string::npos) ? item.localTitle.romPath.substr(slashPos + 1) : item.localTitle.romPath;
            size_t dotPos = filename.find_last_of('.');
            std::string base = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
            localFileName = base + ".sav";
        }
        if (!saves[0].fileName.empty()) {
            localFileName = saves[0].fileName;
        }
    } else if (item.localTitle.console != "3DS") {
        std::string existingRegular = findRegularSaveFile(stagingDir, saves[0].fileName);
        if (!existingRegular.empty()) {
            size_t slashPos = existingRegular.find_last_of("/\\");
            localFileName = (slashPos != std::string::npos) ? existingRegular.substr(slashPos + 1) : existingRegular;
        }
        if (localFileName == "main" && !saves[0].fileName.empty()) {
            localFileName = saves[0].fileName;
        }
    }

    std::string targetFile = stagingDir + "/" + localFileName;
    emit("Staging target: " + targetFile);
    emit("Downloading remote save from RomM...");
    NetworkResult res = m_netMgr->downloadSaveById(saves[0].id, targetFile);
    if (!res.success) {
        emit("[ERROR] Download failed: " + res.errorMessage);
        m_lastMessage = "Download failed: " + res.errorMessage;
        return false;
    }

    struct stat dst;
    long dlSize = (stat(targetFile.c_str(), &dst) == 0) ? (long)dst.st_size : 0;
    emit("[OK] Download finished (" + std::to_string(dlSize) + " bytes).");

    std::string injectSource = (item.localTitle.isFileRom || !item.localTitle.savePath.empty()) ? targetFile : stagingDir;
    emit("Injecting save into local storage...");
    if (!m_saveMgr->injectSave(item.localTitle, injectSource, log)) {
        emit("[ERROR] Save injection failed.");
        m_lastMessage = "Save downloaded but injection failed.";
        return false;
    }

    item.localTitle.hasSave = true;
    if (item.localTitle.isFileRom || !item.localTitle.savePath.empty()) {
        if (item.localTitle.savePath.empty() && item.localTitle.isFileRom) {
            item.localTitle.savePath = m_saveMgr->findSaveFileForRom(item.localTitle.romPath, item.localTitle.console);
        }
        emit("Active local save path: " + item.localTitle.savePath);
    }
    item.localTitle.saveTime = m_saveMgr->getSaveTime(item.localTitle);
    emit("Local save time updated to: " + formatTimestamp(item.localTitle.saveTime));
    emit("[SUCCESS] Save successfully downloaded and applied!");
    m_lastMessage = "Downloaded & injected save for " + item.localTitle.name;
    return true;
}

bool SyncManager::syncItem(SyncItem& item, ConflictChoice choice, LogCallback log) {
    SyncAction action = evaluateItem(item);

    switch (action) {
        case SyncAction::DOWNLOAD_REMOTE_TO_LOCAL:
            return forceDownload(item, log);

        case SyncAction::UPLOAD_LOCAL_TO_REMOTE:
            return forceUpload(item, log);

        case SyncAction::CONFLICT:
            if (choice == ConflictChoice::OVERWRITE_LOCAL) {
                return forceDownload(item, log);
            } else if (choice == ConflictChoice::OVERWRITE_REMOTE) {
                return forceUpload(item, log);
            } else {
                if (log) log("Skipped conflict for " + item.localTitle.name);
                m_lastMessage = "Skipped conflict for " + item.localTitle.name;
                return true;
            }

        case SyncAction::NO_SAVES:
            if (log) log("No save data found to synchronize for " + item.localTitle.name);
            m_lastMessage = "No save data found to synchronize for " + item.localTitle.name;
            return true;

        case SyncAction::UP_TO_DATE:
            if (log) log(item.localTitle.name + " is already up to date.");
            m_lastMessage = item.localTitle.name + " is already up to date.";
            return true;
    }

    return false;
}

} // namespace RomM3DS
