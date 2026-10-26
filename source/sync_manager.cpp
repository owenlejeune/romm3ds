#include "sync_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <map>

namespace RomM3DS {

SyncManager::SyncManager(ConfigManager* cfg, NetworkManager* net, SaveManager* save)
    : m_configMgr(cfg), m_netMgr(net), m_saveMgr(save) {
}

SyncManager::~SyncManager() = default;

std::string SyncManager::getStagingDirectory(uint64_t titleId) {
    return "sdmc:/3ds/romm3ds/saves/" + SaveManager::formatTitleId(titleId);
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
    if (lc == "ds" && (rp == "nds" || rp == "ds")) return true;
    if (lc == "gba" && rp == "gba") return true;
    if (lc == "gbc" && rp == "gbc") return true;
    if (lc == "gb" && (rp == "gb" || rp == "dmg")) return true;
    if (lc == "nes" && (rp == "nes" || rp == "famicom" || rp == "fc")) return true;
    if (lc == "snes" && (rp == "snes" || rp == "sfc")) return true;
    if (lc == "gg" && (rp == "gg" || rp == "gamegear")) return true;
    if (lc == "pce" && (rp == "pce" || rp == "tg16" || rp == "turbografx16")) return true;

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
            p.romCount = it->second;
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

bool SyncManager::refresh() {
    m_items.clear();
    m_remoteGames.clear();
    m_platforms.clear();

    if (m_configMgr) {
        m_configMgr->loadMappings();
    }

    if (m_netMgr) {
        m_netMgr->fetchPlatforms(m_platforms);
        m_netMgr->fetchLibrary(m_remoteGames, "");
        consolidatePlatforms();
    }

    std::vector<InstalledTitle> localTitles;
    if (m_saveMgr) {
        localTitles = m_saveMgr->scanInstalledTitles();
    }

    for (const auto& local : localTitles) {
        SyncItem item;
        item.localTitle = local;
        item.isIgnored = (m_configMgr && m_configMgr->isTitleIgnored(local.titleIdHex));

        if (!item.isIgnored) {
            const GameMapping* mapping = m_configMgr ? m_configMgr->findMappingByTitleId(local.titleIdHex) : nullptr;
            if (mapping && mapping->remoteGameId != 0) {
                item.isMatched = true;
                item.remoteGameId = mapping->remoteGameId;
                item.remoteTitle = mapping->remoteTitle;

                for (const auto& r : m_remoteGames) {
                    if (r.id == item.remoteGameId) {
                        item.remoteHasSave = !r.savePath.empty();
                        item.remoteSaveUpdatedAt = r.updatedAt;
                        break;
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
                    item.remoteHasSave = !matchTarget->savePath.empty();
                    item.remoteSaveUpdatedAt = matchTarget->updatedAt;

                    if (m_configMgr) {
                        GameMapping newMap;
                        newMap.titleId = local.titleIdHex;
                        newMap.localName = local.name;
                        newMap.remoteGameId = matchTarget->id;
                        newMap.remoteTitle = matchTarget->title;
                        m_configMgr->addOrUpdateMapping(newMap);
                    }
                }
            }
        }

        m_items.push_back(item);
    }

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
    item.remoteHasSave = !remoteGame.savePath.empty();
    item.remoteSaveUpdatedAt = remoteGame.updatedAt;

    if (m_configMgr) {
        GameMapping map;
        map.titleId = item.localTitle.titleIdHex;
        map.localName = item.localTitle.name;
        map.remoteGameId = remoteGame.id;
        map.remoteTitle = remoteGame.title;
        m_configMgr->addOrUpdateMapping(map);
        m_configMgr->saveMappings();
    }

    m_lastMessage = "Matched " + item.localTitle.name + " -> " + remoteGame.title;
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

bool SyncManager::forceUpload(SyncItem& item) {
    if (item.isIgnored) {
        m_lastMessage = "Game is ignored: " + item.localTitle.name;
        return false;
    }
    if (!item.isMatched || !m_saveMgr || !m_netMgr) {
        m_lastMessage = "Cannot upload: Unmatched or subsystems missing.";
        return false;
    }

    std::string stagingDir = getStagingDirectory(item.localTitle.titleId);
    ConfigManager::ensureDirectories(stagingDir + "/");

    if (!m_saveMgr->backupSave(item.localTitle.titleId, stagingDir, item.localTitle.mediaType)) {
        m_lastMessage = "Failed to backup local save from 3DS FS.";
        return false;
    }

    std::string targetFile;
    DIR* d = opendir(stagingDir.c_str());
    if (d) {
        struct dirent* ent = nullptr;
        while ((ent = readdir(d)) != nullptr) {
            if (std::strcmp(ent->d_name, ".") != 0 && std::strcmp(ent->d_name, "..") != 0) {
                targetFile = stagingDir + "/" + ent->d_name;
                break;
            }
        }
        closedir(d);
    }

    if (targetFile.empty()) {
        m_lastMessage = "No save files found in dump directory.";
        return false;
    }

    NetworkResult res = m_netMgr->uploadSave(item.remoteGameId, targetFile);
    if (res.success) {
        item.remoteHasSave = true;
        m_lastMessage = "Successfully uploaded save for " + item.localTitle.name;
        return true;
    }

    m_lastMessage = "Upload failed: " + res.errorMessage;
    return false;
}

bool SyncManager::forceDownload(SyncItem& item) {
    if (item.isIgnored) {
        m_lastMessage = "Game is ignored: " + item.localTitle.name;
        return false;
    }
    if (!item.isMatched || !m_saveMgr || !m_netMgr) {
        m_lastMessage = "Cannot download: Unmatched or subsystems missing.";
        return false;
    }

    std::string stagingDir = getStagingDirectory(item.localTitle.titleId);
    ConfigManager::ensureDirectories(stagingDir + "/");

    std::string targetFile = stagingDir + "/main";
    NetworkResult res = m_netMgr->downloadSave(item.remoteGameId, targetFile);
    if (!res.success) {
        m_lastMessage = "Download failed: " + res.errorMessage;
        return false;
    }

    if (!m_saveMgr->injectSave(item.localTitle.titleId, stagingDir, item.localTitle.mediaType)) {
        m_lastMessage = "Save downloaded but injection into 3DS save archive failed.";
        return false;
    }

    item.localTitle.hasSave = true;
    m_lastMessage = "Downloaded & injected save for " + item.localTitle.name;
    return true;
}

bool SyncManager::syncItem(SyncItem& item, ConflictChoice choice) {
    SyncAction action = evaluateItem(item);

    switch (action) {
        case SyncAction::DOWNLOAD_REMOTE_TO_LOCAL:
            return forceDownload(item);

        case SyncAction::UPLOAD_LOCAL_TO_REMOTE:
            return forceUpload(item);

        case SyncAction::CONFLICT:
            if (choice == ConflictChoice::OVERWRITE_LOCAL) {
                return forceDownload(item);
            } else if (choice == ConflictChoice::OVERWRITE_REMOTE) {
                return forceUpload(item);
            } else {
                m_lastMessage = "Skipped conflict for " + item.localTitle.name;
                return true;
            }

        case SyncAction::NO_SAVES:
            m_lastMessage = "No save data found to synchronize for " + item.localTitle.name;
            return true;

        case SyncAction::UP_TO_DATE:
            m_lastMessage = item.localTitle.name + " is already up to date.";
            return true;
    }

    return false;
}

} // namespace RomM3DS
