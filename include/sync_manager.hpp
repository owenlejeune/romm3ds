#pragma once

#include "config_manager.hpp"
#include "network_manager.hpp"
#include "save_manager.hpp"

#include <string>
#include <vector>
#include <set>
#include <cstdint>
#include <ctime>

namespace RomM3DS {

enum class SyncAction {
    DOWNLOAD_REMOTE_TO_LOCAL,
    UPLOAD_LOCAL_TO_REMOTE,
    CONFLICT,
    UP_TO_DATE,
    NO_SAVES
};

enum class ConflictChoice {
    OVERWRITE_LOCAL,
    OVERWRITE_REMOTE,
    SKIP
};

struct SyncItem {
    InstalledTitle localTitle;
    bool isMatched = false;
    uint64_t remoteGameId = 0;
    std::string remoteTitle;
    std::string remoteSaveUpdatedAt;
    time_t remoteSaveTime = 0;
    bool remoteHasSave = false;
    bool isIgnored = false;
};

class SyncManager {
public:
    SyncManager(ConfigManager* cfg, NetworkManager* net, SaveManager* save);
    ~SyncManager();

    bool refresh();
    std::vector<SyncItem>& getItems() { return m_items; }
    const std::vector<SyncItem>& getItems() const { return m_items; }
    const std::vector<RommGame>& getRemoteLibrary() const { return m_remoteGames; }
    const std::vector<RommPlatform>& getPlatforms() const { return m_platforms; }

    bool isPlatformLoaded(uint64_t platformId, const std::string& platformSlug) const;
    bool loadGamesForPlatform(uint64_t platformId, const std::string& platformSlug, bool forceRefresh = false);

    bool saveLibraryCache(const std::string& path = "");
    bool loadLibraryCache(const std::string& path = "");
    void clearLibraryCache();

    bool manualMatch(size_t itemIndex, const RommGame& remoteGame);
    SyncAction evaluateItem(const SyncItem& item);

    bool syncItem(SyncItem& item, ConflictChoice choice = ConflictChoice::OVERWRITE_LOCAL, LogCallback log = nullptr);
    bool forceUpload(SyncItem& item, LogCallback log = nullptr);
    bool forceDownload(SyncItem& item, LogCallback log = nullptr);

    const std::string& getLastMessage() const { return m_lastMessage; }

    static std::string getStagingDirectory(uint64_t titleId);
    static std::string getFriendlyPlatformName(const std::string& slug);
    static bool isPlatformCompatible(const std::string& localConsole, const std::string& remotePlatformSlug);
    static std::string formatTimestamp(time_t t);
    static std::string formatIsoTimestamp(const std::string& iso);
    static time_t parseIsoTimestamp(const std::string& iso);

private:
    void autoMatchTitles();
    void consolidatePlatforms();
    static std::string normalizeName(const std::string& str);
    static bool matchesFuzzy(const std::string& s1, const std::string& s2);

    ConfigManager* m_configMgr = nullptr;
    NetworkManager* m_netMgr = nullptr;
    SaveManager* m_saveMgr = nullptr;

    std::vector<SyncItem> m_items;
    std::vector<RommGame> m_remoteGames;
    std::vector<RommPlatform> m_platforms;
    std::set<std::string> m_loadedPlatforms;
    std::string m_lastMessage;
};

} // namespace RomM3DS
