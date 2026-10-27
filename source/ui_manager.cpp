#include "ui_manager.hpp"
#include "network_manager.hpp"
#include "save_manager.hpp"
#include "sync_manager.hpp"
#include "config_manager.hpp"
#include "common.hpp"

#include <algorithm>
#include <cctype>
#include <map>

namespace RomM3DS {

namespace {

constexpr u32 CLR_BG_TOP       = C2D_Color32(0x18, 0x1B, 0x22, 0xFF);
constexpr u32 CLR_BG_BOTTOM    = C2D_Color32(0x10, 0x12, 0x17, 0xFF);
constexpr u32 CLR_HEADER       = C2D_Color32(0x21, 0x26, 0x31, 0xFF);
constexpr u32 CLR_CARD         = C2D_Color32(0x1C, 0x21, 0x2B, 0xFF);
constexpr u32 CLR_INPUT_BG     = C2D_Color32(0x13, 0x16, 0x1D, 0xFF);
constexpr u32 CLR_ACCENT       = C2D_Color32(0x3B, 0x82, 0xF6, 0xFF);
constexpr u32 CLR_BORDER_OFF   = C2D_Color32(0x2F, 0x36, 0x46, 0xFF);
constexpr u32 CLR_TEXT_WHITE   = C2D_Color32(0xF8, 0xFA, 0xFC, 0xFF);
constexpr u32 CLR_TEXT_MUTED   = C2D_Color32(0x94, 0xA3, 0xB8, 0xFF);
constexpr u32 CLR_GREEN        = C2D_Color32(0x22, 0xC5, 0x5E, 0xFF);
constexpr u32 CLR_YELLOW       = C2D_Color32(0xEA, 0xB3, 0x08, 0xFF);
constexpr u32 CLR_ERROR        = C2D_Color32(0xEF, 0x44, 0x44, 0xFF);

bool containsIgnoreCase(const std::string& str, const std::string& sub) {
    if (sub.empty()) return true;
    if (str.length() < sub.length()) return false;
    auto it = std::search(
        str.begin(), str.end(),
        sub.begin(), sub.end(),
        [](char ch1, char ch2) {
            return std::tolower(static_cast<unsigned char>(ch1)) ==
                   std::tolower(static_cast<unsigned char>(ch2));
        }
    );
    return (it != str.end());
}

const char* getFilterModeName(LocalFilterMode mode) {
    switch (mode) {
        case LocalFilterMode::ALL:        return "All";
        case LocalFilterMode::MATCHED:    return "Matched";
        case LocalFilterMode::UNMATCHED:  return "Unmatched";
        case LocalFilterMode::WITH_SAVES: return "Has Save";
        case LocalFilterMode::HIDDEN:     return "Hidden";
    }
    return "All";
}

} // namespace

UIManager::UIManager() = default;

UIManager::~UIManager() {
    cleanup();
}

void UIManager::init(C3D_RenderTarget* topTarget, C3D_RenderTarget* bottomTarget,
                     ConfigManager* configMgr, NetworkManager* netMgr,
                     SaveManager* saveMgr, SyncManager* syncMgr) {
    m_topTarget = topTarget;
    m_bottomTarget = bottomTarget;
    m_configMgr = configMgr;
    m_netMgr = netMgr;
    m_saveMgr = saveMgr;
    m_syncMgr = syncMgr;

    m_textBuf = C2D_TextBufNew(8192);

    if (m_configMgr && m_configMgr->hasValidConfig()) {
        const auto& cfg = m_configMgr->getConfig();
        m_enteredEndpoint = cfg.serverEndpoint;
        m_enteredApiKey = cfg.apiKey;
        m_state = UIState::MAIN_MENU;
        m_statusMessage = "Configuration loaded.";
        runLibraryRefresh();
    } else {
        m_enteredEndpoint = "http://";
        m_enteredApiKey = "";
        m_state = UIState::FIRST_LAUNCH;
        m_statusMessage = "Welcome! Please enter your RomM credentials.";
    }
}

void UIManager::cleanup() {
    if (m_textBuf) {
        C2D_TextBufDelete(m_textBuf);
        m_textBuf = nullptr;
    }
}

bool UIManager::isTouchInside(const touchPosition& touch, float x, float y, float w, float h) {
    return (touch.px >= x && touch.px <= x + w && touch.py >= y && touch.py <= y + h);
}

std::string UIManager::launchKeyboard(const std::string& hint, const std::string& initial, int maxLen) {
    SwkbdState swkbd;
    char buffer[512] = {0};

    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, maxLen > 0 ? maxLen : 256);
    swkbdSetValidation(&swkbd, SWKBD_ANYTHING, 0, 0);
    swkbdSetHintText(&swkbd, hint.c_str());
    if (!initial.empty()) {
        swkbdSetInitialText(&swkbd, initial.c_str());
    }

    SwkbdButton button = swkbdInputText(&swkbd, buffer, sizeof(buffer));
    if (button == SWKBD_BUTTON_CONFIRM || button == SWKBD_BUTTON_RIGHT) {
        return std::string(buffer);
    }
    return initial;
}

void UIManager::runConnectionTest() {
    if (!m_netMgr) {
        m_statusMessage = "Network subsystem not available.";
        return;
    }

    m_statusMessage = "Testing connection to RomM...";
    render();

    NetworkResult res = m_netMgr->testConnection();
    if (res.success) {
        m_statusMessage = "Connection OK! RomM reachable.";
    } else {
        m_statusMessage = "Conn Failed: " + res.errorMessage;
    }
}

void UIManager::runLibraryRefresh() {
    if (!m_syncMgr) {
        return;
    }

    m_statusMessage = "Scanning titles & querying RomM...";
    m_loadingMessage = "Scanning titles & querying RomM...";
    render();
    render();

    m_syncMgr->refresh();
    m_loadingMessage.clear();
    updateLocalFilteredList();
    updateRemoteFilteredList();
    m_selectedGameIndex = 0;
    m_statusMessage = m_syncMgr->getLastMessage();
}

void UIManager::updateLocalFilteredList() {
    m_filteredLocalIndices.clear();
    if (!m_syncMgr) return;

    const auto& items = m_syncMgr->getItems();
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];

        if (m_localFilterMode == LocalFilterMode::HIDDEN) {
            if (!item.isIgnored) continue;
        } else {
            if (item.isIgnored) continue;
        }

        if (m_localFilterMode == LocalFilterMode::MATCHED && !item.isMatched) {
            continue;
        } else if (m_localFilterMode == LocalFilterMode::UNMATCHED && item.isMatched) {
            continue;
        } else if (m_localFilterMode == LocalFilterMode::WITH_SAVES && !item.localTitle.hasSave) {
            continue;
        }

        if (!m_localFilterText.empty()) {
            if (!containsIgnoreCase(item.localTitle.name, m_localFilterText) &&
                !containsIgnoreCase(item.localTitle.titleIdHex, m_localFilterText) &&
                !containsIgnoreCase(item.localTitle.productCode, m_localFilterText) &&
                !containsIgnoreCase(item.localTitle.console, m_localFilterText)) {
                continue;
            }
        }

        m_filteredLocalIndices.push_back(i);
    }

    if (m_selectedGameIndex >= m_filteredLocalIndices.size()) {
        m_selectedGameIndex = m_filteredLocalIndices.empty() ? 0 : m_filteredLocalIndices.size() - 1;
    }
}

void UIManager::updateRemoteFilteredList() {
    m_filteredRemoteIndices.clear();
    if (!m_syncMgr) return;

    const auto& remote = m_syncMgr->getRemoteLibrary();
    for (size_t i = 0; i < remote.size(); ++i) {
        if (m_selectedConsoleId > 0 && remote[i].platformId > 0) {
            if (remote[i].platformId != m_selectedConsoleId) {
                continue;
            }
        } else if (!m_selectedConsoleSlug.empty()) {
            if (m_selectedConsoleSlug == "other") {
                if (!remote[i].platform.empty()) continue;
            } else {
                if (strcasecmp(remote[i].platform.c_str(), m_selectedConsoleSlug.c_str()) != 0) {
                    continue;
                }
            }
        }

        if (!m_remoteFilterText.empty()) {
            if (!containsIgnoreCase(remote[i].title, m_remoteFilterText)) {
                continue;
            }
        }
        m_filteredRemoteIndices.push_back(i);
    }

    if (m_selectedRemoteIndex >= m_filteredRemoteIndices.size()) {
        m_selectedRemoteIndex = m_filteredRemoteIndices.empty() ? 0 : m_filteredRemoteIndices.size() - 1;
    }
}

void UIManager::cycleLocalFilterMode() {
    switch (m_localFilterMode) {
        case LocalFilterMode::ALL:
            m_localFilterMode = LocalFilterMode::MATCHED;
            break;
        case LocalFilterMode::MATCHED:
            m_localFilterMode = LocalFilterMode::UNMATCHED;
            break;
        case LocalFilterMode::UNMATCHED:
            m_localFilterMode = LocalFilterMode::WITH_SAVES;
            break;
        case LocalFilterMode::WITH_SAVES:
            m_localFilterMode = LocalFilterMode::HIDDEN;
            break;
        case LocalFilterMode::HIDDEN:
            m_localFilterMode = LocalFilterMode::ALL;
            break;
    }
    updateLocalFilteredList();
}

void UIManager::toggleIgnoreSelected() {
    if (!m_syncMgr || m_filteredLocalIndices.empty()) return;
    if (m_selectedGameIndex >= m_filteredLocalIndices.size()) return;

    size_t actualIdx = m_filteredLocalIndices[m_selectedGameIndex];
    SyncItem& item = m_syncMgr->getItems()[actualIdx];

    bool newIgnored = !item.isIgnored;
    item.isIgnored = newIgnored;

    if (m_configMgr) {
        m_configMgr->setIgnoreTitle(item.localTitle.titleIdHex, newIgnored);
        m_configMgr->saveConfig();
    }

    if (newIgnored) {
        m_statusMessage = "Hidden " + item.localTitle.name + " from UI.";
    } else {
        m_statusMessage = "Unhid " + item.localTitle.name + ".";
    }

    updateLocalFilteredList();
}

void UIManager::executeSaveAction() {
    if (m_enteredEndpoint.empty() || m_enteredEndpoint == "http://") {
        m_statusMessage = "Error: Endpoint cannot be empty!";
        return;
    }
    if (m_enteredApiKey.empty()) {
        m_statusMessage = "Error: API Key cannot be empty!";
        return;
    }

    if (m_configMgr) {
        m_configMgr->setEndpoint(m_enteredEndpoint);
        m_configMgr->setApiKey(m_enteredApiKey);

        if (m_netMgr) {
            m_statusMessage = "Testing server connection...";
            render();

            NetworkResult res = m_netMgr->testConnection();
            if (!res.success) {
                m_statusMessage = "Conn Failed: " + res.errorMessage;
                return;
            }
        }

        if (m_configMgr->saveConfig()) {
            m_statusMessage = "Connected & Saved successfully!";
            m_state = UIState::MAIN_MENU;
            runLibraryRefresh();
        } else {
            m_statusMessage = "Error: Failed to write config to SD.";
        }
    }
}

void UIManager::syncSelectedGame() {
    if (!m_syncMgr || m_filteredLocalIndices.empty()) {
        m_statusMessage = "No installed titles match current filter.";
        return;
    }
    if (m_selectedGameIndex >= m_filteredLocalIndices.size()) {
        return;
    }

    size_t actualIdx = m_filteredLocalIndices[m_selectedGameIndex];
    SyncItem& item = m_syncMgr->getItems()[actualIdx];

    if (item.isIgnored) {
        m_statusMessage = "Cannot sync ignored game: " + item.localTitle.name;
        return;
    }

    if (!item.isMatched) {
        m_statusMessage = "Cannot sync: Unmatched game. Press (SELECT) to match.";
        return;
    }

    SyncAction action = m_syncMgr->evaluateItem(item);
    if (action == SyncAction::CONFLICT) {
        m_conflictGameIndex = actualIdx;
        m_conflictChoiceIndex = 0;
        m_state = UIState::CONFLICT_PROMPT;
        return;
    }

    m_statusMessage = "Syncing " + item.localTitle.name + "...";
    m_loadingMessage = "Syncing " + item.localTitle.name + "...";
    render();
    render();

    m_syncMgr->syncItem(item);
    m_loadingMessage.clear();
    m_statusMessage = m_syncMgr->getLastMessage();
    updateLocalFilteredList();
}

void UIManager::syncAllGames() {
    if (!m_syncMgr || m_syncMgr->getItems().empty()) {
        m_statusMessage = "No installed titles found.";
        return;
    }

    size_t count = 0;
    for (size_t i = 0; i < m_syncMgr->getItems().size(); ++i) {
        SyncItem& item = m_syncMgr->getItems()[i];
        if (item.isIgnored || !item.isMatched) {
            continue;
        }

        SyncAction act = m_syncMgr->evaluateItem(item);
        if (act == SyncAction::CONFLICT) {
            m_conflictGameIndex = i;
            m_conflictChoiceIndex = 0;
            m_state = UIState::CONFLICT_PROMPT;
            return;
        }

        m_statusMessage = "Syncing (" + std::to_string(i + 1) + "/" +
                          std::to_string(m_syncMgr->getItems().size()) + "): " + item.localTitle.name;
        m_loadingMessage = "Syncing (" + std::to_string(i + 1) + "/" +
                           std::to_string(m_syncMgr->getItems().size()) + ")\n" + item.localTitle.name;
        render();
        render();

        if (m_syncMgr->syncItem(item)) {
            count++;
        }
    }

    m_loadingMessage.clear();
    m_statusMessage = "Batch sync complete (" + std::to_string(count) + " games synced).";
    updateLocalFilteredList();
}

void UIManager::forceUploadSelected() {
    if (!m_syncMgr || m_filteredLocalIndices.empty()) return;
    if (m_selectedGameIndex >= m_filteredLocalIndices.size()) return;

    size_t actualIdx = m_filteredLocalIndices[m_selectedGameIndex];
    SyncItem& item = m_syncMgr->getItems()[actualIdx];
    m_statusMessage = "Uploading save for " + item.localTitle.name + "...";
    m_loadingMessage = "Uploading save for " + item.localTitle.name + "...";
    render();
    render();

    m_syncMgr->forceUpload(item);
    m_loadingMessage.clear();
    m_statusMessage = m_syncMgr->getLastMessage();
    updateLocalFilteredList();
}

void UIManager::forceDownloadSelected() {
    if (!m_syncMgr || m_filteredLocalIndices.empty()) return;
    if (m_selectedGameIndex >= m_filteredLocalIndices.size()) return;

    size_t actualIdx = m_filteredLocalIndices[m_selectedGameIndex];
    SyncItem& item = m_syncMgr->getItems()[actualIdx];
    m_statusMessage = "Downloading save for " + item.localTitle.name + "...";
    m_loadingMessage = "Downloading save for " + item.localTitle.name + "...";
    render();
    render();

    m_syncMgr->forceDownload(item);
    m_loadingMessage.clear();
    m_statusMessage = m_syncMgr->getLastMessage();
    updateLocalFilteredList();
}

void UIManager::openManualMatch() {
    if (!m_syncMgr || m_filteredLocalIndices.empty()) return;
    if (m_selectedGameIndex >= m_filteredLocalIndices.size()) return;

    size_t localActualIdx = m_filteredLocalIndices[m_selectedGameIndex];
    const auto& localItem = m_syncMgr->getItems()[localActualIdx];

    m_availableConsoles = m_syncMgr->getPlatforms();
    if (m_availableConsoles.empty()) {
        std::map<std::string, int> counts;
        for (const auto& g : m_syncMgr->getRemoteLibrary()) {
            if (!g.platform.empty()) counts[g.platform]++;
            else counts["other"]++;
        }
        for (const auto& pair : counts) {
            RommPlatform p;
            p.slug = pair.first;
            p.name = SyncManager::getFriendlyPlatformName(pair.first);
            p.romCount = pair.second;
            m_availableConsoles.push_back(p);
        }
        std::sort(m_availableConsoles.begin(), m_availableConsoles.end(),
            [](const RommPlatform& a, const RommPlatform& b) {
                return a.name < b.name;
            });
    }

    // Default console selection based on local game console
    m_selectedConsoleIndex = 0;
    for (size_t i = 0; i < m_availableConsoles.size(); ++i) {
        if (SyncManager::isPlatformCompatible(localItem.localTitle.console, m_availableConsoles[i].slug)) {
            m_selectedConsoleIndex = i;
            break;
        }
    }

    m_matchingSubState = MatchingSubState::SELECT_CONSOLE;
    m_state = UIState::MATCHING;
}

void UIManager::selectConsole(size_t index) {
    if (index >= m_availableConsoles.size()) return;
    m_selectedConsoleIndex = index;
    m_selectedConsoleId = m_availableConsoles[index].id;
    m_selectedConsoleSlug = m_availableConsoles[index].slug;
    m_selectedConsoleName = m_availableConsoles[index].name;

    if (m_syncMgr) {
        m_statusMessage = "Loading " + m_selectedConsoleName + " games...";
        m_loadingMessage = "Loading " + m_selectedConsoleName + " games...";
        render();
        render();
        m_syncMgr->loadGamesForPlatform(m_selectedConsoleId, m_selectedConsoleSlug);
        m_loadingMessage.clear();
    }

    m_remoteFilterText.clear();
    updateRemoteFilteredList();
    m_selectedRemoteIndex = 0;
    m_matchingSubState = MatchingSubState::SELECT_GAME;
}

void UIManager::confirmManualMatch() {
    if (!m_syncMgr || m_filteredLocalIndices.empty() || m_filteredRemoteIndices.empty()) return;
    if (m_selectedGameIndex >= m_filteredLocalIndices.size()) return;
    if (m_selectedRemoteIndex >= m_filteredRemoteIndices.size()) return;

    size_t localActualIdx = m_filteredLocalIndices[m_selectedGameIndex];
    size_t remoteActualIdx = m_filteredRemoteIndices[m_selectedRemoteIndex];

    const auto& remoteGames = m_syncMgr->getRemoteLibrary();
    if (remoteActualIdx >= remoteGames.size()) return;

    m_syncMgr->manualMatch(localActualIdx, remoteGames[remoteActualIdx]);
    m_statusMessage = m_syncMgr->getLastMessage();
    updateLocalFilteredList();
    m_state = UIState::MAIN_MENU;
}

void UIManager::resolveConflict(int choice) {
    if (!m_syncMgr || m_conflictGameIndex >= m_syncMgr->getItems().size()) {
        m_state = UIState::MAIN_MENU;
        return;
    }

    SyncItem& item = m_syncMgr->getItems()[m_conflictGameIndex];
    ConflictChoice cChoice = (choice == 0) ? ConflictChoice::OVERWRITE_LOCAL :
                             (choice == 1) ? ConflictChoice::OVERWRITE_REMOTE :
                                             ConflictChoice::SKIP;

    m_statusMessage = "Resolving save conflict...";
    render();

    m_syncMgr->syncItem(item, cChoice);
    m_statusMessage = m_syncMgr->getLastMessage();
    m_state = UIState::MAIN_MENU;
}

void UIManager::handleInput(u32 kDown, const touchPosition& touch) {
    if (m_state == UIState::FIRST_LAUNCH) {
        if (kDown & KEY_UP) {
            m_setupSelectedIndex = (m_setupSelectedIndex - 1 + 3) % 3;
        } else if (kDown & KEY_DOWN) {
            m_setupSelectedIndex = (m_setupSelectedIndex + 1) % 3;
        }

        if (kDown & KEY_A) {
            if (m_setupSelectedIndex == 0) {
                m_enteredEndpoint = launchKeyboard("Enter RomM Server URL", m_enteredEndpoint, 128);
            } else if (m_setupSelectedIndex == 1) {
                m_enteredApiKey = launchKeyboard("Enter RomM API Key", m_enteredApiKey, 128);
            } else if (m_setupSelectedIndex == 2) {
                executeSaveAction();
            }
        }

        if (kDown & KEY_TOUCH) {
            if (isTouchInside(touch, 16.0f, 44.0f, 288.0f, 32.0f)) {
                m_setupSelectedIndex = 0;
                m_enteredEndpoint = launchKeyboard("Enter RomM Server URL", m_enteredEndpoint, 128);
            } else if (isTouchInside(touch, 16.0f, 96.0f, 288.0f, 32.0f)) {
                m_setupSelectedIndex = 1;
                m_enteredApiKey = launchKeyboard("Enter RomM API Key", m_enteredApiKey, 128);
            } else if (isTouchInside(touch, 16.0f, 148.0f, 288.0f, 34.0f)) {
                m_setupSelectedIndex = 2;
                executeSaveAction();
            }
        }
    } else if (m_state == UIState::MAIN_MENU) {
        size_t total = m_filteredLocalIndices.size();

        if (total > 0) {
            if (kDown & KEY_UP) {
                m_selectedGameIndex = (m_selectedGameIndex > 0) ? (m_selectedGameIndex - 1) : (total - 1);
            } else if (kDown & KEY_DOWN) {
                m_selectedGameIndex = (m_selectedGameIndex + 1 < total) ? (m_selectedGameIndex + 1) : 0;
            } else if (kDown & KEY_LEFT) {
                m_selectedGameIndex = (m_selectedGameIndex >= 4) ? (m_selectedGameIndex - 4) : 0;
            } else if (kDown & KEY_RIGHT) {
                m_selectedGameIndex = (m_selectedGameIndex + 4 < total) ? (m_selectedGameIndex + 4) : (total - 1);
            }
        }

        if (kDown & KEY_A) {
            syncSelectedGame();
        } else if (kDown & KEY_X) {
            syncAllGames();
        } else if (kDown & KEY_L) {
            forceUploadSelected();
        } else if (kDown & KEY_R) {
            forceDownloadSelected();
        } else if (kDown & KEY_SELECT) {
            openManualMatch();
        } else if (kDown & KEY_Y) {
            std::string q = launchKeyboard("Search installed games...", m_localFilterText, 64);
            if (q != m_localFilterText) {
                m_localFilterText = q;
                updateLocalFilteredList();
            }
        }

        if (kDown & KEY_TOUCH) {
            if (isTouchInside(touch, 212.0f, 7.0f, 92.0f, 20.0f)) {
                cycleLocalFilterMode();
            } else if (isTouchInside(touch, 16.0f, 30.0f, 230.0f, 22.0f)) {
                std::string q = launchKeyboard("Search installed games...", m_localFilterText, 64);
                if (q != m_localFilterText) {
                    m_localFilterText = q;
                    updateLocalFilteredList();
                }
            } else if (isTouchInside(touch, 252.0f, 30.0f, 52.0f, 22.0f)) {
                if (!m_localFilterText.empty()) {
                    m_localFilterText.clear();
                    updateLocalFilteredList();
                }
            } else if (isTouchInside(touch, 16.0f, 163.0f, 138.0f, 26.0f)) {
                syncSelectedGame();
            } else if (isTouchInside(touch, 166.0f, 163.0f, 138.0f, 26.0f)) {
                syncAllGames();
            } else if (isTouchInside(touch, 16.0f, 193.0f, 138.0f, 26.0f)) {
                openManualMatch();
            } else if (isTouchInside(touch, 166.0f, 193.0f, 138.0f, 26.0f)) {
                toggleIgnoreSelected();
            } else {
                for (size_t i = 0; i < 4 && i < total; ++i) {
                    float rowY = 56.0f + static_cast<float>(i * 26.0f);
                    if (isTouchInside(touch, 16.0f, rowY, 288.0f, 24.0f)) {
                        size_t pageOffset = (m_selectedGameIndex / 4) * 4;
                        size_t target = pageOffset + i;
                        if (target < total) {
                            m_selectedGameIndex = target;
                        }
                        break;
                    }
                }
            }
        }
    } else if (m_state == UIState::MATCHING) {
        if (m_matchingSubState == MatchingSubState::SELECT_CONSOLE) {
            size_t consoleTotal = m_availableConsoles.size();
            if (consoleTotal > 0) {
                if (kDown & KEY_UP) {
                    m_selectedConsoleIndex = (m_selectedConsoleIndex > 0) ? (m_selectedConsoleIndex - 1) : (consoleTotal - 1);
                } else if (kDown & KEY_DOWN) {
                    m_selectedConsoleIndex = (m_selectedConsoleIndex + 1 < consoleTotal) ? (m_selectedConsoleIndex + 1) : 0;
                } else if (kDown & KEY_LEFT) {
                    m_selectedConsoleIndex = (m_selectedConsoleIndex >= 5) ? (m_selectedConsoleIndex - 5) : 0;
                } else if (kDown & KEY_RIGHT) {
                    m_selectedConsoleIndex = (m_selectedConsoleIndex + 5 < consoleTotal) ? (m_selectedConsoleIndex + 5) : (consoleTotal - 1);
                }
            }

            if (kDown & KEY_A) {
                selectConsole(m_selectedConsoleIndex);
            } else if (kDown & KEY_B) {
                m_state = UIState::MAIN_MENU;
            } else if (kDown & KEY_X) {
                runLibraryRefresh();
                m_availableConsoles = m_syncMgr ? m_syncMgr->getPlatforms() : std::vector<RommPlatform>();
            }

            if (kDown & KEY_TOUCH) {
                if (isTouchInside(touch, 16.0f, 194.0f, 138.0f, 30.0f)) {
                    selectConsole(m_selectedConsoleIndex);
                } else if (isTouchInside(touch, 166.0f, 194.0f, 138.0f, 30.0f)) {
                    m_state = UIState::MAIN_MENU;
                } else {
                    size_t pageOffset = (m_selectedConsoleIndex / 5) * 5;
                    for (size_t i = 0; i < 5 && (pageOffset + i) < consoleTotal; ++i) {
                        float rowY = 36.0f + static_cast<float>(i * 30.0f);
                        if (isTouchInside(touch, 16.0f, rowY, 288.0f, 26.0f)) {
                            selectConsole(pageOffset + i);
                            break;
                        }
                    }
                }
            }
        } else {
            size_t remoteTotal = m_filteredRemoteIndices.size();
            if (remoteTotal > 0) {
                if (kDown & KEY_UP) {
                    m_selectedRemoteIndex = (m_selectedRemoteIndex > 0) ? (m_selectedRemoteIndex - 1) : (remoteTotal - 1);
                } else if (kDown & KEY_DOWN) {
                    m_selectedRemoteIndex = (m_selectedRemoteIndex + 1 < remoteTotal) ? (m_selectedRemoteIndex + 1) : 0;
                } else if (kDown & KEY_LEFT) {
                    m_selectedRemoteIndex = (m_selectedRemoteIndex >= 5) ? (m_selectedRemoteIndex - 5) : 0;
                } else if (kDown & KEY_RIGHT) {
                    m_selectedRemoteIndex = (m_selectedRemoteIndex + 5 < remoteTotal) ? (m_selectedRemoteIndex + 5) : (remoteTotal - 1);
                }
            }

            if (kDown & KEY_A) {
                confirmManualMatch();
            } else if (kDown & KEY_B) {
                m_matchingSubState = MatchingSubState::SELECT_CONSOLE;
            } else if (kDown & KEY_X) {
                std::string prompt = "Search in " + m_selectedConsoleName + "...";
                std::string q = launchKeyboard(prompt, m_remoteFilterText, 64);
                if (q != m_remoteFilterText) {
                    m_remoteFilterText = q;
                    updateRemoteFilteredList();
                }
            } else if (kDown & KEY_Y) {
                if (!m_remoteFilterText.empty()) {
                    m_remoteFilterText.clear();
                    updateRemoteFilteredList();
                }
            }

            if (kDown & KEY_TOUCH) {
                if (isTouchInside(touch, 16.0f, 28.0f, 230.0f, 22.0f)) {
                    std::string prompt = "Search in " + m_selectedConsoleName + "...";
                    std::string q = launchKeyboard(prompt, m_remoteFilterText, 64);
                    if (q != m_remoteFilterText) {
                        m_remoteFilterText = q;
                        updateRemoteFilteredList();
                    }
                } else if (isTouchInside(touch, 252.0f, 28.0f, 52.0f, 22.0f)) {
                    if (!m_remoteFilterText.empty()) {
                        m_remoteFilterText.clear();
                        updateRemoteFilteredList();
                    }
                } else if (isTouchInside(touch, 16.0f, 194.0f, 138.0f, 30.0f)) {
                    confirmManualMatch();
                } else if (isTouchInside(touch, 166.0f, 194.0f, 138.0f, 30.0f)) {
                    m_matchingSubState = MatchingSubState::SELECT_CONSOLE;
                } else {
                    for (size_t i = 0; i < 5 && i < remoteTotal; ++i) {
                        float rowY = 54.0f + static_cast<float>(i * 26.0f);
                        if (isTouchInside(touch, 16.0f, rowY, 288.0f, 24.0f)) {
                            size_t pageOffset = (m_selectedRemoteIndex / 5) * 5;
                            size_t target = pageOffset + i;
                            if (target < remoteTotal) {
                                m_selectedRemoteIndex = target;
                            }
                            break;
                        }
                    }
                }
            }
        }
    } else if (m_state == UIState::CONFLICT_PROMPT) {
        if (kDown & KEY_UP) {
            m_conflictChoiceIndex = (m_conflictChoiceIndex - 1 + 3) % 3;
        } else if (kDown & KEY_DOWN) {
            m_conflictChoiceIndex = (m_conflictChoiceIndex + 1) % 3;
        }

        if (kDown & KEY_A) {
            resolveConflict(m_conflictChoiceIndex);
        } else if (kDown & KEY_B) {
            resolveConflict(2);
        }

        if (kDown & KEY_TOUCH) {
            if (isTouchInside(touch, 16.0f, 44.0f, 288.0f, 32.0f)) {
                resolveConflict(0);
            } else if (isTouchInside(touch, 16.0f, 84.0f, 288.0f, 32.0f)) {
                resolveConflict(1);
            } else if (isTouchInside(touch, 16.0f, 124.0f, 288.0f, 32.0f)) {
                resolveConflict(2);
            }
        }
    }
}

void UIManager::drawInputBox(float x, float y, float w, float h, const char* label, const std::string& value, bool selected) {
    u32 borderColor = selected ? CLR_ACCENT : CLR_BORDER_OFF;
    C2D_DrawRectSolid(x, y, 0.5f, w, h, borderColor);
    C2D_DrawRectSolid(x + 1.5f, y + 1.5f, 0.5f, w - 3.0f, h - 3.0f, CLR_INPUT_BG);

    C2D_Text lblText;
    C2D_TextParse(&lblText, m_textBuf, label);
    C2D_TextOptimize(&lblText);
    C2D_DrawText(&lblText, C2D_WithColor, x, y - 14.0f, 0.5f, 0.45f, 0.45f, CLR_TEXT_MUTED);

    std::string displayVal = value.empty() ? "<Empty - Tap or Press (A)>" : value;
    u32 valColor = value.empty() ? CLR_TEXT_MUTED : CLR_TEXT_WHITE;

    C2D_Text valText;
    C2D_TextParse(&valText, m_textBuf, displayVal.c_str());
    C2D_TextOptimize(&valText);
    C2D_DrawText(&valText, C2D_WithColor, x + 8.0f, y + 8.0f, 0.5f, 0.48f, 0.48f, valColor);
}

void UIManager::drawButton(float x, float y, float w, float h, const char* label, bool selected, u32 accentColor) {
    u32 btnBg = selected ? (accentColor != 0 ? accentColor : CLR_ACCENT) : CLR_CARD;
    u32 borderColor = selected ? CLR_TEXT_WHITE : CLR_BORDER_OFF;

    C2D_DrawRectSolid(x, y, 0.5f, w, h, borderColor);
    C2D_DrawRectSolid(x + 1.5f, y + 1.5f, 0.5f, w - 3.0f, h - 3.0f, btnBg);

    C2D_Text btnText;
    C2D_TextParse(&btnText, m_textBuf, label);
    C2D_TextOptimize(&btnText);

    float scale = (h <= 22.0f) ? 0.38f : (h <= 28.0f ? 0.42f : 0.46f);
    float textW = 0.0f, textH = 0.0f;
    C2D_TextGetDimensions(&btnText, scale, scale, &textW, &textH);

    if (textW > w - 8.0f && textW > 0.0f) {
        scale *= (w - 8.0f) / textW;
        C2D_TextGetDimensions(&btnText, scale, scale, &textW, &textH);
    }

    float textX = x + (w - textW) / 2.0f;
    float textY = y + (h - textH) / 2.0f;
    C2D_DrawText(&btnText, C2D_WithColor, textX, textY, 0.5f, scale, scale, CLR_TEXT_WHITE);
}

void UIManager::drawLoadingOverlay(const std::string& message) {
    constexpr u32 CLR_OVERLAY_DIM = C2D_Color32(0x05, 0x07, 0x0B, 0xCC);
    C2D_DrawRectSolid(0.0f, 0.0f, 0.7f, RomM3DS::SCREEN_BOTTOM_WIDTH, RomM3DS::SCREEN_BOTTOM_HEIGHT, CLR_OVERLAY_DIM);

    float boxW = 260.0f;
    float boxH = 74.0f;
    float boxX = (RomM3DS::SCREEN_BOTTOM_WIDTH - boxW) / 2.0f;
    float boxY = (RomM3DS::SCREEN_BOTTOM_HEIGHT - boxH) / 2.0f;

    C2D_DrawRectSolid(boxX, boxY, 0.75f, boxW, boxH, CLR_ACCENT);
    C2D_DrawRectSolid(boxX + 2.0f, boxY + 2.0f, 0.75f, boxW - 4.0f, boxH - 4.0f, CLR_HEADER);

    C2D_Text headerText;
    C2D_TextParse(&headerText, m_textBuf, "Please wait...");
    C2D_TextOptimize(&headerText);
    C2D_DrawText(&headerText, C2D_WithColor, boxX + 16.0f, boxY + 14.0f, 0.8f, 0.50f, 0.50f, CLR_ACCENT);

    C2D_Text msgText;
    C2D_TextParse(&msgText, m_textBuf, message.c_str());
    C2D_TextOptimize(&msgText);

    float msgScale = 0.42f;
    float msgW = 0.0f, msgH = 0.0f;
    C2D_TextGetDimensions(&msgText, msgScale, msgScale, &msgW, &msgH);
    if (msgW > (boxW - 32.0f) && msgW > 0.0f) {
        msgScale *= ((boxW - 32.0f) / msgW);
    }
    C2D_DrawText(&msgText, C2D_WithColor, boxX + 16.0f, boxY + 40.0f, 0.8f, msgScale, msgScale, CLR_TEXT_WHITE);
}

void UIManager::renderFirstLaunchTop() {
    C2D_DrawRectSolid(0.0f, 0.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 42.0f, CLR_HEADER);
    C2D_DrawRectSolid(0.0f, 40.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 2.0f, CLR_ACCENT);

    C2D_Text headerText;
    C2D_TextParse(&headerText, m_textBuf, "romm3ds - Initial Setup");
    C2D_TextOptimize(&headerText);
    C2D_DrawText(&headerText, C2D_WithColor, 12.0f, 10.0f, 0.5f, 0.75f, 0.75f, CLR_TEXT_WHITE);

    C2D_Text descText;
    C2D_TextParse(&descText, m_textBuf,
                  "Connect to your self-hosted RomM instance.\n\n"
                  "- RomM Endpoint: http(s)://your-server:port\n"
                  "- API Key / Token: generated in RomM dashboard\n\n"
                  "Use D-Pad Up/Down to navigate, (A) to edit fields.\n"
                  "Touch screen keyboard input is supported.");
    C2D_TextOptimize(&descText);
    C2D_DrawText(&descText, C2D_WithColor, 16.0f, 56.0f, 0.5f, 0.48f, 0.48f, CLR_TEXT_MUTED);

    C2D_Text promptText;
    C2D_TextParse(&promptText, m_textBuf, "Press START to exit");
    C2D_TextOptimize(&promptText);
    C2D_DrawText(&promptText, C2D_WithColor, 16.0f, 212.0f, 0.5f, 0.45f, 0.45f, CLR_TEXT_MUTED);
}

void UIManager::renderFirstLaunchBottom() {
    C2D_DrawRectSolid(8.0f, 6.0f, 0.5f, RomM3DS::SCREEN_BOTTOM_WIDTH - 16.0f, RomM3DS::SCREEN_BOTTOM_HEIGHT - 12.0f, CLR_CARD);

    drawInputBox(16.0f, 44.0f, 288.0f, 32.0f, "RomM Server Endpoint:", m_enteredEndpoint, m_setupSelectedIndex == 0);

    std::string maskedKey = m_enteredApiKey.empty() ? "" : std::string(m_enteredApiKey.length(), '*');
    drawInputBox(16.0f, 96.0f, 288.0f, 32.0f, "API Key / Credentials:", maskedKey, m_setupSelectedIndex == 1);

    drawButton(16.0f, 148.0f, 288.0f, 34.0f, "[ Save Configuration ]", m_setupSelectedIndex == 2);

    u32 statusColor = (m_statusMessage.find("Error") != std::string::npos || m_statusMessage.find("Failed") != std::string::npos) ? CLR_ERROR : CLR_GREEN;
    C2D_Text statusText;
    C2D_TextParse(&statusText, m_textBuf, m_statusMessage.c_str());
    C2D_TextOptimize(&statusText);
    C2D_DrawText(&statusText, C2D_WithColor, 16.0f, 198.0f, 0.5f, 0.44f, 0.44f, statusColor);
}

void UIManager::renderMainMenuTop() {
    C2D_DrawRectSolid(0.0f, 0.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 42.0f, CLR_HEADER);
    C2D_DrawRectSolid(0.0f, 40.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 2.0f, CLR_ACCENT);

    C2D_Text headerText;
    C2D_TextParse(&headerText, m_textBuf, "romm3ds - Save Synchronization");
    C2D_TextOptimize(&headerText);
    C2D_DrawText(&headerText, C2D_WithColor, 12.0f, 10.0f, 0.5f, 0.75f, 0.75f, CLR_TEXT_WHITE);

    if (m_syncMgr && !m_filteredLocalIndices.empty() && m_selectedGameIndex < m_filteredLocalIndices.size()) {
        size_t actualIdx = m_filteredLocalIndices[m_selectedGameIndex];
        const auto& item = m_syncMgr->getItems()[actualIdx];

        std::string displayName = item.localTitle.name + "  [" + item.localTitle.console + "]";
        if (item.isIgnored) {
            displayName += " [IGNORED / HIDDEN]";
        }
        C2D_Text nameText;
        C2D_TextParse(&nameText, m_textBuf, displayName.c_str());
        C2D_TextOptimize(&nameText);
        u32 nameColor = item.isIgnored ? CLR_TEXT_MUTED : CLR_TEXT_WHITE;
        float nameScale = 0.58f;
        float nameW = 0.0f, nameH = 0.0f;
        C2D_TextGetDimensions(&nameText, nameScale, nameScale, &nameW, &nameH);
        if (nameW > 368.0f && nameW > 0.0f) {
            nameScale *= (368.0f / nameW);
        }
        C2D_DrawText(&nameText, C2D_WithColor, 16.0f, 48.0f, 0.5f, nameScale, nameScale, nameColor);

        std::string matchDesc = item.isIgnored ? "Status: Ignored / Excluded from Sync"
                                : (item.isMatched ? ("Matched: " + item.remoteTitle) : "Unmatched (Press SELECT for RomM Library)");
        u32 matchColor = item.isIgnored ? CLR_TEXT_MUTED : (item.isMatched ? CLR_GREEN : CLR_YELLOW);
        C2D_Text matchText;
        C2D_TextParse(&matchText, m_textBuf, matchDesc.c_str());
        C2D_TextOptimize(&matchText);
        float matchScale = 0.45f;
        float matchW = 0.0f, matchH = 0.0f;
        C2D_TextGetDimensions(&matchText, matchScale, matchScale, &matchW, &matchH);
        if (matchW > 368.0f && matchW > 0.0f) {
            matchScale *= (368.0f / matchW);
        }
        C2D_DrawText(&matchText, C2D_WithColor, 16.0f, 70.0f, 0.5f, matchScale, matchScale, matchColor);

        std::string detailLine1 = "Console: " + item.localTitle.console +
                                  "  |  Title ID: " + item.localTitle.titleIdHex +
                                  (!item.localTitle.productCode.empty() ? (" (" + item.localTitle.productCode + ")") : "");
        C2D_Text detailText1;
        C2D_TextParse(&detailText1, m_textBuf, detailLine1.c_str());
        C2D_TextOptimize(&detailText1);
        float d1Scale = 0.42f;
        float d1W = 0.0f, d1H = 0.0f;
        C2D_TextGetDimensions(&detailText1, d1Scale, d1Scale, &d1W, &d1H);
        if (d1W > 368.0f && d1W > 0.0f) {
            d1Scale *= (368.0f / d1W);
        }
        C2D_DrawText(&detailText1, C2D_WithColor, 16.0f, 92.0f, 0.5f, d1Scale, d1Scale, CLR_TEXT_MUTED);

        std::string detailLine2 = "Local Save: " + std::string(item.localTitle.hasSave ? "YES" : "NO") +
                                  "    |    Remote Save: " + std::string(item.remoteHasSave ? "YES" : "NO");
        if (item.remoteHasSave && !item.remoteSaveUpdatedAt.empty()) {
            detailLine2 += " (" + item.remoteSaveUpdatedAt.substr(0, 10) + ")";
        }
        C2D_Text detailText2;
        C2D_TextParse(&detailText2, m_textBuf, detailLine2.c_str());
        C2D_TextOptimize(&detailText2);
        u32 saveColor = (item.localTitle.hasSave || item.remoteHasSave) ? CLR_TEXT_WHITE : CLR_TEXT_MUTED;
        C2D_DrawText(&detailText2, C2D_WithColor, 16.0f, 108.0f, 0.5f, 0.42f, 0.42f, saveColor);

        std::string controls = "Controls:\n"
                               "  (A) Sync Selected       (X) Batch Sync All\n"
                               "  (L) Force Upload Save   (R) Force Download Save\n"
                               "  (SELECT) RomM Library   (Y) Search Filter\n"
                               "  (D-Pad L/R) Prev/Next Page | Tap [Hide] to ignore app";
        C2D_Text ctrlText;
        C2D_TextParse(&ctrlText, m_textBuf, controls.c_str());
        C2D_TextOptimize(&ctrlText);
        C2D_DrawText(&ctrlText, C2D_WithColor, 16.0f, 128.0f, 0.5f, 0.44f, 0.44f, CLR_TEXT_WHITE);
    } else {
        C2D_Text emptyText;
        std::string msg = "No installed titles match the current filter.\n\n"
                          "- Filter Mode: " + std::string(getFilterModeName(m_localFilterMode)) + "\n";
        if (!m_localFilterText.empty()) {
            msg += "- Search Query: \"" + m_localFilterText + "\"\n";
        }
        msg += "\nTap [Clr] or [Mode] to reset view, or (START) to exit.";
        C2D_TextParse(&emptyText, m_textBuf, msg.c_str());
        C2D_TextOptimize(&emptyText);
        C2D_DrawText(&emptyText, C2D_WithColor, 16.0f, 65.0f, 0.5f, 0.48f, 0.48f, CLR_TEXT_MUTED);
    }
}

void UIManager::renderMainMenuBottom() {
    C2D_DrawRectSolid(8.0f, 4.0f, 0.5f, RomM3DS::SCREEN_BOTTOM_WIDTH - 16.0f, RomM3DS::SCREEN_BOTTOM_HEIGHT - 8.0f, CLR_CARD);

    size_t total = m_filteredLocalIndices.size();
    std::string header = "Installed (" + std::to_string(total > 0 ? (m_selectedGameIndex + 1) : 0) +
                         "/" + std::to_string(total) + ")";
    C2D_Text titleText;
    C2D_TextParse(&titleText, m_textBuf, header.c_str());
    C2D_TextOptimize(&titleText);
    C2D_DrawText(&titleText, C2D_WithColor, 16.0f, 9.0f, 0.5f, 0.48f, 0.48f, CLR_TEXT_WHITE);

    std::string modeLabel = std::string(getFilterModeName(m_localFilterMode));
    u32 modeColor = (m_localFilterMode == LocalFilterMode::HIDDEN) ? CLR_YELLOW : CLR_ACCENT;
    drawButton(212.0f, 7.0f, 92.0f, 20.0f, modeLabel.c_str(), false, modeColor);

    std::string searchDisplay = m_localFilterText.empty() ? "Search: Tap to filter (Y)..." : ("Search: " + m_localFilterText);
    u32 searchBorder = m_localFilterText.empty() ? CLR_BORDER_OFF : CLR_ACCENT;
    u32 searchTextColor = m_localFilterText.empty() ? CLR_TEXT_MUTED : CLR_TEXT_WHITE;
    C2D_DrawRectSolid(16.0f, 30.0f, 0.5f, 230.0f, 22.0f, searchBorder);
    C2D_DrawRectSolid(17.5f, 31.5f, 0.5f, 227.0f, 19.0f, CLR_INPUT_BG);

    C2D_Text searchTxt;
    C2D_TextParse(&searchTxt, m_textBuf, searchDisplay.c_str());
    C2D_TextOptimize(&searchTxt);
    C2D_DrawText(&searchTxt, C2D_WithColor, 22.0f, 34.0f, 0.5f, 0.40f, 0.40f, searchTextColor);

    drawButton(252.0f, 30.0f, 52.0f, 22.0f, "[ Clr ]", false);

    size_t pageOffset = (m_selectedGameIndex / 4) * 4;
    for (size_t i = 0; i < 4 && (pageOffset + i) < total; ++i) {
        size_t filteredIdx = pageOffset + i;
        size_t actualIdx = m_filteredLocalIndices[filteredIdx];
        const auto& item = m_syncMgr->getItems()[actualIdx];
        bool selected = (filteredIdx == m_selectedGameIndex);

        float y = 56.0f + static_cast<float>(i * 26.0f);
        u32 rowBg = selected ? CLR_HEADER : CLR_INPUT_BG;
        u32 borderColor = selected ? CLR_ACCENT : CLR_BORDER_OFF;

        C2D_DrawRectSolid(16.0f, y, 0.5f, 288.0f, 24.0f, borderColor);
        C2D_DrawRectSolid(17.5f, y + 1.5f, 0.5f, 285.0f, 21.0f, rowBg);

        u32 dotColor = item.isIgnored ? CLR_TEXT_MUTED : (item.isMatched ? CLR_GREEN : CLR_YELLOW);
        C2D_DrawRectSolid(22.0f, y + 7.0f, 0.5f, 8.0f, 8.0f, dotColor);

        std::string tag;
        if (item.localTitle.hasSave) {
            tag += " [S]";
        }
        if (item.isIgnored) {
            tag += " (Hide)";
        }

        std::string prefix = "[" + item.localTitle.console + "] ";
        std::string fullLabel = prefix + item.localTitle.name + tag;

        constexpr float MAX_ROW_TEXT_WIDTH = 260.0f;
        float textScale = 0.40f;

        C2D_Text rowText;
        C2D_TextParse(&rowText, m_textBuf, fullLabel.c_str());
        C2D_TextOptimize(&rowText);

        float textW = 0.0f, textH = 0.0f;
        C2D_TextGetDimensions(&rowText, textScale, textScale, &textW, &textH);

        if (textW > MAX_ROW_TEXT_WIDTH) {
            std::string name = item.localTitle.name;
            int low = 0;
            int high = static_cast<int>(name.length());
            int bestLen = 0;

            while (low <= high) {
                int mid = low + (high - low) / 2;
                std::string testLabel = prefix + name.substr(0, mid) + ".." + tag;
                C2D_Text testText;
                C2D_TextParse(&testText, m_textBuf, testLabel.c_str());
                C2D_TextOptimize(&testText);
                float curW = 0.0f, curH = 0.0f;
                C2D_TextGetDimensions(&testText, textScale, textScale, &curW, &curH);

                if (curW <= MAX_ROW_TEXT_WIDTH) {
                    bestLen = mid;
                    low = mid + 1;
                } else {
                    high = mid - 1;
                }
            }

            fullLabel = prefix + name.substr(0, bestLen) + ".." + tag;
            C2D_TextParse(&rowText, m_textBuf, fullLabel.c_str());
            C2D_TextOptimize(&rowText);
        }

        C2D_DrawText(&rowText, C2D_WithColor, 36.0f, y + 5.0f, 0.5f, textScale, textScale, CLR_TEXT_WHITE);
    }

    drawButton(16.0f, 163.0f, 138.0f, 26.0f, "[ Sync (A) ]", false);
    drawButton(166.0f, 163.0f, 138.0f, 26.0f, "[ Sync All (X) ]", false);

    drawButton(16.0f, 193.0f, 138.0f, 26.0f, "[ RomM Lib (SELECT) ]", false);

    bool isCurrentlyIgnored = (!m_filteredLocalIndices.empty() &&
                               m_selectedGameIndex < m_filteredLocalIndices.size() &&
                               m_syncMgr->getItems()[m_filteredLocalIndices[m_selectedGameIndex]].isIgnored);
    const char* ignoreLabel = isCurrentlyIgnored ? "[ Unhide Game ]" : "[ Hide Game ]";
    u32 ignoreAccent = isCurrentlyIgnored ? CLR_GREEN : 0;
    drawButton(166.0f, 193.0f, 138.0f, 26.0f, ignoreLabel, false, ignoreAccent);

    u32 statusColor = (m_statusMessage.find("Error") != std::string::npos ||
                       m_statusMessage.find("Failed") != std::string::npos) ? CLR_ERROR : CLR_GREEN;
    C2D_Text statusText;
    C2D_TextParse(&statusText, m_textBuf, m_statusMessage.c_str());
    C2D_TextOptimize(&statusText);
    C2D_DrawText(&statusText, C2D_WithColor, 16.0f, 223.0f, 0.5f, 0.38f, 0.38f, statusColor);
}

void UIManager::renderMatchingTop() {
    if (m_matchingSubState == MatchingSubState::SELECT_CONSOLE) {
        renderMatchingConsolesTop();
    } else {
        renderMatchingGamesTop();
    }
}

void UIManager::renderMatchingBottom() {
    if (m_matchingSubState == MatchingSubState::SELECT_CONSOLE) {
        renderMatchingConsolesBottom();
    } else {
        renderMatchingGamesBottom();
    }
}

void UIManager::renderMatchingConsolesTop() {
    C2D_DrawRectSolid(0.0f, 0.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 42.0f, CLR_HEADER);
    C2D_DrawRectSolid(0.0f, 40.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 2.0f, CLR_ACCENT);

    C2D_Text headerText;
    C2D_TextParse(&headerText, m_textBuf, "romm3ds - RomM Consoles");
    C2D_TextOptimize(&headerText);
    C2D_DrawText(&headerText, C2D_WithColor, 12.0f, 10.0f, 0.5f, 0.75f, 0.75f, CLR_TEXT_WHITE);

    if (m_syncMgr && !m_filteredLocalIndices.empty() && m_selectedGameIndex < m_filteredLocalIndices.size()) {
        size_t localActualIdx = m_filteredLocalIndices[m_selectedGameIndex];
        const auto& item = m_syncMgr->getItems()[localActualIdx];

        std::string guide = "Target Local Game:\n  " + item.localTitle.name + " [" + item.localTitle.console + "]\n\n"
                            "Select console to browse in RomM library:\n"
                            "  (A) Open Selected Console\n"
                            "  (B) Back to Main Menu\n"
                            "  (X) Refresh Consoles & Library";
        if (!m_loadingMessage.empty()) {
            guide += "\n\n--> " + m_loadingMessage;
        }
        C2D_Text guideText;
        C2D_TextParse(&guideText, m_textBuf, guide.c_str());
        C2D_TextOptimize(&guideText);
        C2D_DrawText(&guideText, C2D_WithColor, 16.0f, 56.0f, 0.5f, 0.48f, 0.48f, CLR_TEXT_WHITE);
    }
}

void UIManager::renderMatchingConsolesBottom() {
    C2D_DrawRectSolid(8.0f, 4.0f, 0.5f, RomM3DS::SCREEN_BOTTOM_WIDTH - 16.0f, RomM3DS::SCREEN_BOTTOM_HEIGHT - 8.0f, CLR_CARD);

    size_t consoleTotal = m_availableConsoles.size();
    std::string header = "RomM Consoles (" + std::to_string(consoleTotal > 0 ? (m_selectedConsoleIndex + 1) : 0) +
                         "/" + std::to_string(consoleTotal) + ")";
    C2D_Text titleText;
    C2D_TextParse(&titleText, m_textBuf, header.c_str());
    C2D_TextOptimize(&titleText);
    C2D_DrawText(&titleText, C2D_WithColor, 16.0f, 9.0f, 0.5f, 0.48f, 0.48f, CLR_TEXT_WHITE);

    size_t pageOffset = (m_selectedConsoleIndex / 5) * 5;
    for (size_t i = 0; i < 5 && (pageOffset + i) < consoleTotal; ++i) {
        size_t idx = pageOffset + i;
        const auto& c = m_availableConsoles[idx];
        bool selected = (idx == m_selectedConsoleIndex);

        float y = 36.0f + static_cast<float>(i * 30.0f);
        u32 rowBg = selected ? CLR_HEADER : CLR_INPUT_BG;
        u32 borderColor = selected ? CLR_ACCENT : CLR_BORDER_OFF;

        C2D_DrawRectSolid(16.0f, y, 0.5f, 288.0f, 26.0f, borderColor);
        C2D_DrawRectSolid(17.5f, y + 1.5f, 0.5f, 285.0f, 23.0f, rowBg);

        u32 dotColor = selected ? CLR_YELLOW : CLR_ACCENT;
        C2D_DrawRectSolid(24.0f, y + 8.0f, 0.5f, 8.0f, 8.0f, dotColor);

        std::string nameLabel = c.name;
        if (nameLabel.length() > 22) {
            nameLabel = nameLabel.substr(0, 20) + "..";
        }
        C2D_Text nameText;
        C2D_TextParse(&nameText, m_textBuf, nameLabel.c_str());
        C2D_TextOptimize(&nameText);
        C2D_DrawText(&nameText, C2D_WithColor, 38.0f, y + 5.0f, 0.5f, 0.44f, 0.44f, CLR_TEXT_WHITE);

        std::string countLabel = std::to_string(c.romCount) + " ROMs";
        C2D_Text countText;
        C2D_TextParse(&countText, m_textBuf, countLabel.c_str());
        C2D_TextOptimize(&countText);
        C2D_DrawText(&countText, C2D_WithColor, 230.0f, y + 6.0f, 0.5f, 0.40f, 0.40f, CLR_TEXT_MUTED);
    }

    if (consoleTotal == 0) {
        C2D_Text emptyTxt;
        C2D_TextParse(&emptyTxt, m_textBuf, "No consoles found in RomM library.\nCheck connection or refresh (X).");
        C2D_TextOptimize(&emptyTxt);
        C2D_DrawText(&emptyTxt, C2D_WithColor, 24.0f, 80.0f, 0.5f, 0.45f, 0.45f, CLR_TEXT_MUTED);
    }

    drawButton(16.0f, 194.0f, 138.0f, 30.0f, "[ Open Console (A) ]", false);
    drawButton(166.0f, 194.0f, 138.0f, 30.0f, "[ Back (B) ]", false);
}

void UIManager::renderMatchingGamesTop() {
    C2D_DrawRectSolid(0.0f, 0.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 42.0f, CLR_HEADER);
    C2D_DrawRectSolid(0.0f, 40.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 2.0f, CLR_YELLOW);

    std::string header = "romm3ds - " + m_selectedConsoleName;
    if (header.length() > 36) header = header.substr(0, 34) + "..";
    C2D_Text headerText;
    C2D_TextParse(&headerText, m_textBuf, header.c_str());
    C2D_TextOptimize(&headerText);
    C2D_DrawText(&headerText, C2D_WithColor, 12.0f, 10.0f, 0.5f, 0.75f, 0.75f, CLR_TEXT_WHITE);

    if (m_syncMgr && !m_filteredLocalIndices.empty() && m_selectedGameIndex < m_filteredLocalIndices.size()) {
        size_t localActualIdx = m_filteredLocalIndices[m_selectedGameIndex];
        const auto& item = m_syncMgr->getItems()[localActualIdx];

        std::string guide = "Local Target: " + item.localTitle.name + " [" + item.localTitle.console + "]\n"
                            "Console: " + m_selectedConsoleName + " (" + m_selectedConsoleSlug + ")\n\n"
                            "Controls:\n"
                            "  (A) Pair Selected Game     (B) Back to Consoles\n"
                            "  (X) Search Filter Games    (Y) Clear Filter";
        if (!m_remoteFilterText.empty()) {
            guide += "\n\nActive Filter: \"" + m_remoteFilterText + "\"";
        }
        C2D_Text guideText;
        C2D_TextParse(&guideText, m_textBuf, guide.c_str());
        C2D_TextOptimize(&guideText);
        C2D_DrawText(&guideText, C2D_WithColor, 16.0f, 56.0f, 0.5f, 0.48f, 0.48f, CLR_TEXT_WHITE);
    }
}

void UIManager::renderMatchingGamesBottom() {
    C2D_DrawRectSolid(8.0f, 4.0f, 0.5f, RomM3DS::SCREEN_BOTTOM_WIDTH - 16.0f, RomM3DS::SCREEN_BOTTOM_HEIGHT - 8.0f, CLR_CARD);

    size_t remoteTotal = m_filteredRemoteIndices.size();
    std::string header = m_selectedConsoleName + " (" + std::to_string(remoteTotal > 0 ? (m_selectedRemoteIndex + 1) : 0) +
                         "/" + std::to_string(remoteTotal) + ")";
    C2D_Text titleText;
    C2D_TextParse(&titleText, m_textBuf, header.c_str());
    C2D_TextOptimize(&titleText);
    C2D_DrawText(&titleText, C2D_WithColor, 16.0f, 9.0f, 0.5f, 0.48f, 0.48f, CLR_TEXT_WHITE);

    std::string searchDisplay = m_remoteFilterText.empty() ? "Search: Tap to filter (X)..." : ("Search: " + m_remoteFilterText);
    u32 searchBorder = m_remoteFilterText.empty() ? CLR_BORDER_OFF : CLR_ACCENT;
    u32 searchTextColor = m_remoteFilterText.empty() ? CLR_TEXT_MUTED : CLR_TEXT_WHITE;
    C2D_DrawRectSolid(16.0f, 28.0f, 0.5f, 230.0f, 22.0f, searchBorder);
    C2D_DrawRectSolid(17.5f, 29.5f, 0.5f, 227.0f, 19.0f, CLR_INPUT_BG);

    C2D_Text searchTxt;
    C2D_TextParse(&searchTxt, m_textBuf, searchDisplay.c_str());
    C2D_TextOptimize(&searchTxt);
    C2D_DrawText(&searchTxt, C2D_WithColor, 22.0f, 32.0f, 0.5f, 0.40f, 0.40f, searchTextColor);

    drawButton(252.0f, 28.0f, 52.0f, 22.0f, "[ Clr ]", false);

    const auto& remote = m_syncMgr ? m_syncMgr->getRemoteLibrary() : std::vector<RommGame>();
    size_t pageOffset = (m_selectedRemoteIndex / 5) * 5;
    for (size_t i = 0; i < 5 && (pageOffset + i) < remoteTotal; ++i) {
        size_t filteredIdx = pageOffset + i;
        size_t actualIdx = m_filteredRemoteIndices[filteredIdx];
        const auto& g = remote[actualIdx];
        bool selected = (filteredIdx == m_selectedRemoteIndex);

        float y = 54.0f + static_cast<float>(i * 26.0f);
        u32 rowBg = selected ? CLR_HEADER : CLR_INPUT_BG;
        u32 borderColor = selected ? CLR_ACCENT : CLR_BORDER_OFF;

        C2D_DrawRectSolid(16.0f, y, 0.5f, 288.0f, 24.0f, borderColor);
        C2D_DrawRectSolid(17.5f, y + 1.5f, 0.5f, 285.0f, 21.0f, rowBg);

        std::string label = g.title;
        constexpr float MAX_REMOTE_ROW_WIDTH = 268.0f;
        float textScale = 0.42f;

        C2D_Text rowText;
        C2D_TextParse(&rowText, m_textBuf, label.c_str());
        C2D_TextOptimize(&rowText);

        float textW = 0.0f, textH = 0.0f;
        C2D_TextGetDimensions(&rowText, textScale, textScale, &textW, &textH);

        if (textW > MAX_REMOTE_ROW_WIDTH) {
            int low = 0;
            int high = static_cast<int>(label.length());
            int bestLen = 0;

            while (low <= high) {
                int mid = low + (high - low) / 2;
                std::string testLabel = label.substr(0, mid) + "..";
                C2D_Text testText;
                C2D_TextParse(&testText, m_textBuf, testLabel.c_str());
                C2D_TextOptimize(&testText);
                float curW = 0.0f, curH = 0.0f;
                C2D_TextGetDimensions(&testText, textScale, textScale, &curW, &curH);

                if (curW <= MAX_REMOTE_ROW_WIDTH) {
                    bestLen = mid;
                    low = mid + 1;
                } else {
                    high = mid - 1;
                }
            }

            label = label.substr(0, bestLen) + "..";
            C2D_TextParse(&rowText, m_textBuf, label.c_str());
            C2D_TextOptimize(&rowText);
        }

        C2D_DrawText(&rowText, C2D_WithColor, 24.0f, y + 5.0f, 0.5f, textScale, textScale, CLR_TEXT_WHITE);
    }

    if (remoteTotal == 0) {
        C2D_Text noMatches;
        std::string noMsg = m_remoteFilterText.empty() ? "No games found for this console." : "No games match search query.\nTap [Clr] to reset filter.";
        C2D_TextParse(&noMatches, m_textBuf, noMsg.c_str());
        C2D_TextOptimize(&noMatches);
        C2D_DrawText(&noMatches, C2D_WithColor, 24.0f, 80.0f, 0.5f, 0.45f, 0.45f, CLR_TEXT_MUTED);
    }

    drawButton(16.0f, 194.0f, 138.0f, 30.0f, "[ Pair Game (A) ]", false);
    drawButton(166.0f, 194.0f, 138.0f, 30.0f, "[ Consoles (B) ]", false);
}

void UIManager::renderConflictTop() {
    C2D_DrawRectSolid(0.0f, 0.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 42.0f, CLR_HEADER);
    C2D_DrawRectSolid(0.0f, 40.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 2.0f, CLR_ERROR);

    C2D_Text headerText;
    C2D_TextParse(&headerText, m_textBuf, "romm3ds - Save Conflict Detected!");
    C2D_TextOptimize(&headerText);
    C2D_DrawText(&headerText, C2D_WithColor, 12.0f, 10.0f, 0.5f, 0.75f, 0.75f, CLR_TEXT_WHITE);

    if (m_syncMgr && m_conflictGameIndex < m_syncMgr->getItems().size()) {
        const auto& item = m_syncMgr->getItems()[m_conflictGameIndex];

        std::string info = "Conflict in: " + item.localTitle.name +
                           "\n\nBoth local 3DS and remote RomM saves exist.\n" +
                           "Remote save date: " + (item.remoteSaveUpdatedAt.empty() ? "N/A" : item.remoteSaveUpdatedAt) +
                           "\n\nChoose resolution policy on bottom screen.";
        C2D_Text infoText;
        C2D_TextParse(&infoText, m_textBuf, info.c_str());
        C2D_TextOptimize(&infoText);
        C2D_DrawText(&infoText, C2D_WithColor, 16.0f, 60.0f, 0.5f, 0.50f, 0.50f, CLR_TEXT_WHITE);
    }
}

void UIManager::renderConflictBottom() {
    C2D_DrawRectSolid(8.0f, 6.0f, 0.5f, RomM3DS::SCREEN_BOTTOM_WIDTH - 16.0f, RomM3DS::SCREEN_BOTTOM_HEIGHT - 12.0f, CLR_CARD);

    C2D_Text promptText;
    C2D_TextParse(&promptText, m_textBuf, "Select Resolution Action:");
    C2D_TextOptimize(&promptText);
    C2D_DrawText(&promptText, C2D_WithColor, 16.0f, 14.0f, 0.5f, 0.55f, 0.55f, CLR_TEXT_WHITE);

    drawButton(16.0f, 44.0f, 288.0f, 32.0f, "[ Overwrite Local (Use Remote Save) ]", m_conflictChoiceIndex == 0);
    drawButton(16.0f, 84.0f, 288.0f, 32.0f, "[ Overwrite Remote (Use Local Save) ]", m_conflictChoiceIndex == 1);
    drawButton(16.0f, 124.0f, 288.0f, 32.0f, "[ Skip This Game ]", m_conflictChoiceIndex == 2);

    C2D_Text guideText;
    C2D_TextParse(&guideText, m_textBuf, "Use D-Pad Up/Down + (A) or tap with stylus.");
    C2D_TextOptimize(&guideText);
    C2D_DrawText(&guideText, C2D_WithColor, 16.0f, 180.0f, 0.5f, 0.44f, 0.44f, CLR_TEXT_MUTED);
}

void UIManager::renderTopScreen() {
    switch (m_state) {
        case UIState::FIRST_LAUNCH:
            renderFirstLaunchTop();
            break;
        case UIState::MAIN_MENU:
            renderMainMenuTop();
            break;
        case UIState::MATCHING:
            renderMatchingTop();
            break;
        case UIState::CONFLICT_PROMPT:
            renderConflictTop();
            break;
    }
}

void UIManager::renderBottomScreen() {
    switch (m_state) {
        case UIState::FIRST_LAUNCH:
            renderFirstLaunchBottom();
            break;
        case UIState::MAIN_MENU:
            renderMainMenuBottom();
            break;
        case UIState::MATCHING:
            renderMatchingBottom();
            break;
        case UIState::CONFLICT_PROMPT:
            renderConflictBottom();
            break;
    }

    if (!m_loadingMessage.empty()) {
        drawLoadingOverlay(m_loadingMessage);
    }
}

void UIManager::render() {
    C2D_TextBufClear(m_textBuf);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    C2D_TargetClear(m_topTarget, CLR_BG_TOP);
    C2D_SceneBegin(m_topTarget);
    renderTopScreen();

    C2D_TargetClear(m_bottomTarget, CLR_BG_BOTTOM);
    C2D_SceneBegin(m_bottomTarget);
    renderBottomScreen();

    C3D_FrameEnd(0);
}

} // namespace RomM3DS
