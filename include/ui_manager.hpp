#pragma once

#include <3ds.h>
#include <citro2d.h>
#include <string>
#include <vector>

namespace RomM3DS {

class ConfigManager;
class NetworkManager;
class SaveManager;
class SyncManager;

enum class UIState {
    FIRST_LAUNCH,
    MAIN_MENU,
    MATCHING,
    CONFLICT_PROMPT
};

enum class LocalFilterMode {
    ALL,
    MATCHED,
    UNMATCHED,
    WITH_SAVES,
    HIDDEN
};

class UIManager {
public:
    UIManager();
    ~UIManager();

    void init(C3D_RenderTarget* topTarget, C3D_RenderTarget* bottomTarget,
              ConfigManager* configMgr, NetworkManager* netMgr = nullptr,
              SaveManager* saveMgr = nullptr, SyncManager* syncMgr = nullptr);

    void handleInput(u32 kDown, const touchPosition& touch);
    void render();
    void cleanup();

    void setState(UIState state) { m_state = state; }
    UIState getState() const { return m_state; }

    void setStatusMessage(const std::string& msg) { m_statusMessage = msg; }

    void runConnectionTest();
    void runLibraryRefresh();
    void syncSelectedGame();
    void syncAllGames();
    void forceUploadSelected();
    void forceDownloadSelected();

    void updateLocalFilteredList();
    void updateRemoteFilteredList();
    void cycleLocalFilterMode();
    void toggleIgnoreSelected();

private:
    void renderTopScreen();
    void renderBottomScreen();

    void renderFirstLaunchTop();
    void renderFirstLaunchBottom();

    void renderMainMenuTop();
    void renderMainMenuBottom();

    void renderMatchingTop();
    void renderMatchingBottom();

    void renderConflictTop();
    void renderConflictBottom();

    void drawInputBox(float x, float y, float w, float h, const char* label, const std::string& value, bool selected);
    void drawButton(float x, float y, float w, float h, const char* label, bool selected, u32 accentColor = 0);

    std::string launchKeyboard(const std::string& hint, const std::string& initial, int maxLen = 256);
    void executeSaveAction();

    void openManualMatch();
    void confirmManualMatch();
    void resolveConflict(int choice);

    static bool isTouchInside(const touchPosition& touch, float x, float y, float w, float h);

    UIState m_state = UIState::FIRST_LAUNCH;
    ConfigManager* m_configMgr = nullptr;
    NetworkManager* m_netMgr = nullptr;
    SaveManager* m_saveMgr = nullptr;
    SyncManager* m_syncMgr = nullptr;

    C3D_RenderTarget* m_topTarget = nullptr;
    C3D_RenderTarget* m_bottomTarget = nullptr;
    C2D_TextBuf m_textBuf = nullptr;

    int m_setupSelectedIndex = 0;
    std::string m_enteredEndpoint;
    std::string m_enteredApiKey;
    std::string m_statusMessage;

    size_t m_selectedGameIndex = 0;
    size_t m_selectedRemoteIndex = 0;
    int m_conflictChoiceIndex = 0;
    size_t m_conflictGameIndex = 0;

    LocalFilterMode m_localFilterMode = LocalFilterMode::ALL;
    std::string m_localFilterText;
    std::vector<size_t> m_filteredLocalIndices;

    std::string m_remoteFilterText;
    std::vector<size_t> m_filteredRemoteIndices;
};

} // namespace RomM3DS
