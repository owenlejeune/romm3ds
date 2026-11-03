#include "ui_manager.hpp"
#include "common.hpp"
#include "config_manager.hpp"
#include "network_manager.hpp"
#include "save_manager.hpp"
#include "sync_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <map>
#include <sys/stat.h>

namespace RomM3DS {

namespace {

// Obsidian Black, Dark Charcoal Gray & Amethyst Purple Theme Palette
constexpr u32 CLR_BG_TOP = C2D_Color32(0x0B, 0x0A, 0x0E, 0xFF);
constexpr u32 CLR_BG_BOTTOM = C2D_Color32(0x08, 0x07, 0x0B, 0xFF);
constexpr u32 CLR_SURFACE_HEADER = C2D_Color32(0x14, 0x12, 0x1A, 0xFF);
constexpr u32 CLR_SURFACE_CARD = C2D_Color32(0x18, 0x16, 0x20, 0xFF);
constexpr u32 CLR_SURFACE_ELEVATED = C2D_Color32(0x21, 0x1E, 0x2C, 0xFF);
constexpr u32 CLR_SURFACE_ALT = C2D_Color32(0x12, 0x10, 0x17, 0xFF);
constexpr u32 CLR_BORDER_SUBTLE = C2D_Color32(0x2B, 0x26, 0x38, 0xFF);
constexpr u32 CLR_ACCENT_PRIMARY = C2D_Color32(0x8B, 0x5C, 0xF6, 0xFF);
constexpr u32 CLR_ACCENT_PRESSED = C2D_Color32(0x6D, 0x28, 0xD9, 0xFF);
constexpr u32 CLR_ACCENT_SOFT = C2D_Color32(0xA7, 0x8B, 0xFA, 0xFF);
constexpr u32 CLR_ACCENT_MUTED = C2D_Color32(0x4C, 0x1D, 0x95, 0xFF);
constexpr u32 CLR_TEXT_PRIMARY = C2D_Color32(0xF3, 0xF0, 0xFF, 0xFF);
constexpr u32 CLR_TEXT_SECONDARY = C2D_Color32(0x9C, 0xA3, 0xAF, 0xFF);
constexpr u32 CLR_TEXT_DIM = C2D_Color32(0x5B, 0x56, 0x6B, 0xFF);
constexpr u32 CLR_DANGER = C2D_Color32(0xE1, 0x1D, 0x48, 0xFF);
constexpr u32 CLR_DIM_OVERLAY = C2D_Color32(0x05, 0x04, 0x08, 0xB8);

// Semantic aliases & status indicator colors (Green / Yellow / Gray)
constexpr u32 CLR_HEADER = CLR_SURFACE_HEADER;
constexpr u32 CLR_CARD = CLR_SURFACE_CARD;
constexpr u32 CLR_INPUT_BG = CLR_SURFACE_ALT;
constexpr u32 CLR_ACCENT = CLR_ACCENT_PRIMARY;
constexpr u32 CLR_BORDER_OFF = CLR_BORDER_SUBTLE;
constexpr u32 CLR_TEXT_WHITE = CLR_TEXT_PRIMARY;
constexpr u32 CLR_TEXT_MUTED = CLR_TEXT_SECONDARY;
constexpr u32 CLR_GREEN = C2D_Color32(0x22, 0xC5, 0x5E, 0xFF);
constexpr u32 CLR_YELLOW = C2D_Color32(0xEA, 0xB3, 0x08, 0xFF);
constexpr u32 CLR_ERROR = CLR_DANGER;

bool containsIgnoreCase(const std::string &str, const std::string &sub) {
  if (sub.empty())
    return true;
  if (str.length() < sub.length())
    return false;
  auto it = std::search(
      str.begin(), str.end(), sub.begin(), sub.end(), [](char ch1, char ch2) {
        return std::tolower(static_cast<unsigned char>(ch1)) ==
               std::tolower(static_cast<unsigned char>(ch2));
      });
  return (it != str.end());
}

const char *getFilterModeName(LocalFilterMode mode) {
  switch (mode) {
  case LocalFilterMode::ALL:
    return "All";
  case LocalFilterMode::MATCHED:
    return "Matched";
  case LocalFilterMode::UNMATCHED:
    return "Unmatched";
  case LocalFilterMode::WITH_SAVES:
    return "Has Save";
  case LocalFilterMode::OUTDATED:
    return "Outdated";
  case LocalFilterMode::UPDATED:
    return "Updated";
  case LocalFilterMode::HIDDEN:
    return "Hidden";
  case LocalFilterMode::CONSOLE:
    return "Console";
  }
  return "All";
}

const char *getSortModeName(LocalSortMode mode) {
  switch (mode) {
  case LocalSortMode::TITLE:
    return "Title";
  case LocalSortMode::SAVE_STATUS:
    return "Save";
  }
  return "Title";
}

} // namespace

UIManager::UIManager() = default;

UIManager::~UIManager() { cleanup(); }

void UIManager::init(C3D_RenderTarget *topTarget,
                     C3D_RenderTarget *bottomTarget, ConfigManager *configMgr,
                     NetworkManager *netMgr, SaveManager *saveMgr,
                     SyncManager *syncMgr) {
  m_topTarget = topTarget;
  m_bottomTarget = bottomTarget;
  m_configMgr = configMgr;
  m_netMgr = netMgr;
  m_saveMgr = saveMgr;
  m_syncMgr = syncMgr;

  m_textBuf = C2D_TextBufNew(8192);

  if (m_configMgr && m_configMgr->hasValidConfig()) {
    const auto &cfg = m_configMgr->getConfig();
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
  freeGameLogo();
  if (m_textBuf) {
    C2D_TextBufDelete(m_textBuf);
    m_textBuf = nullptr;
  }
}

bool UIManager::isTouchInside(const touchPosition &touch, float x, float y,
                              float w, float h) {
  return (touch.px >= x && touch.px <= x + w && touch.py >= y &&
          touch.py <= y + h);
}

std::string UIManager::launchKeyboard(const std::string &hint,
                                      const std::string &initial, int maxLen) {
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

void UIManager::runLibraryRefresh(bool forceFullRemoteRefresh) {
  if (!m_syncMgr) {
    return;
  }

  m_statusMessage = "Scanning titles & querying RomM...";
  m_loadingMessage = "Scanning titles & querying RomM...";
  render();
  render();

  m_syncMgr->refresh(forceFullRemoteRefresh);
  m_loadingMessage.clear();
  updateLocalFilteredList();
  updateRemoteFilteredList();
  m_selectedGameIndex = 0;
  m_statusMessage = m_syncMgr->getLastMessage();
}

void UIManager::updateLocalFilteredList() {
  m_filteredLocalIndices.clear();
  if (!m_syncMgr)
    return;

  const auto &items = m_syncMgr->getItems();
  for (size_t i = 0; i < items.size(); ++i) {
    const auto &item = items[i];

    if (m_localFilterMode == LocalFilterMode::HIDDEN) {
      if (!item.isIgnored)
        continue;
    } else {
      if (item.isIgnored)
        continue;
    }

    if (m_localFilterMode == LocalFilterMode::MATCHED && !item.isMatched) {
      continue;
    } else if (m_localFilterMode == LocalFilterMode::UNMATCHED &&
               item.isMatched) {
      continue;
    } else if (m_localFilterMode == LocalFilterMode::WITH_SAVES &&
               !item.localTitle.hasSave) {
      continue;
    } else if (m_localFilterMode == LocalFilterMode::OUTDATED) {
      if (!item.isMatched || !item.remoteHasSave)
        continue;
      time_t localTime = item.localTitle.saveTime;
      time_t remoteTime =
          item.remoteSaveTime != 0
              ? item.remoteSaveTime
              : SyncManager::parseIsoTimestamp(item.remoteSaveUpdatedAt);
      bool remoteIsNewer =
          !item.localTitle.hasSave ||
          (remoteTime > 0 && (localTime == 0 || remoteTime > localTime + 5));
      if (!remoteIsNewer)
        continue;
    } else if (m_localFilterMode == LocalFilterMode::UPDATED) {
      if (!item.isMatched || !item.localTitle.hasSave)
        continue;
      time_t localTime = item.localTitle.saveTime;
      time_t remoteTime =
          item.remoteSaveTime != 0
              ? item.remoteSaveTime
              : SyncManager::parseIsoTimestamp(item.remoteSaveUpdatedAt);
      bool localIsNewer =
          !item.remoteHasSave ||
          (localTime > 0 && (remoteTime == 0 || localTime > remoteTime + 5));
      if (!localIsNewer)
        continue;
    } else if (m_localFilterMode == LocalFilterMode::CONSOLE) {
      if (strcasecmp(item.localTitle.console.c_str(),
                     m_localFilterConsole.c_str()) != 0) {
        continue;
      }
    }

    if (!m_localFilterText.empty()) {
      if (!containsIgnoreCase(item.localTitle.name, m_localFilterText) &&
          !containsIgnoreCase(item.localTitle.titleIdHex, m_localFilterText) &&
          !containsIgnoreCase(item.localTitle.productCode, m_localFilterText) &&
          !containsIgnoreCase(item.localTitle.console, m_localFilterText) &&
          (!item.localTitle.isFileRom ||
           !containsIgnoreCase(item.localTitle.romPath, m_localFilterText))) {
        continue;
      }
    }

    m_filteredLocalIndices.push_back(i);
  }

  std::sort(
      m_filteredLocalIndices.begin(), m_filteredLocalIndices.end(),
      [&](size_t a, size_t b) {
        const auto &itemA = items[a];
        const auto &itemB = items[b];

        if (m_localSortMode == LocalSortMode::SAVE_STATUS) {
          bool aHas = itemA.localTitle.hasSave || itemA.remoteHasSave;
          bool bHas = itemB.localTitle.hasSave || itemB.remoteHasSave;
          if (aHas != bHas)
            return aHas > bHas;
          time_t aTime =
              std::max(itemA.localTitle.saveTime, itemA.remoteSaveTime);
          time_t bTime =
              std::max(itemB.localTitle.saveTime, itemB.remoteSaveTime);
          if (aTime != bTime)
            return aTime > bTime;
          int nCmp = strcasecmp(itemA.localTitle.name.c_str(),
                                itemB.localTitle.name.c_str());
          if (nCmp != 0)
            return nCmp < 0;
          return itemA.localTitle.titleIdHex < itemB.localTitle.titleIdHex;
        } else { // LocalSortMode::TITLE (default)
          int cmp = strcasecmp(itemA.localTitle.name.c_str(),
                               itemB.localTitle.name.c_str());
          if (cmp != 0)
            return cmp < 0;
          return itemA.localTitle.titleIdHex < itemB.localTitle.titleIdHex;
        }
      });

  if (m_selectedGameIndex >= m_filteredLocalIndices.size()) {
    m_selectedGameIndex =
        m_filteredLocalIndices.empty() ? 0 : m_filteredLocalIndices.size() - 1;
  }
  ensureSelectedVisible();
}

void UIManager::updateRemoteFilteredList() {
  m_filteredRemoteIndices.clear();
  if (!m_syncMgr)
    return;

  const auto &remote = m_syncMgr->getRemoteLibrary();
  for (size_t i = 0; i < remote.size(); ++i) {
    if (m_selectedConsoleId > 0 && remote[i].platformId > 0) {
      if (remote[i].platformId != m_selectedConsoleId) {
        continue;
      }
    } else if (!m_selectedConsoleSlug.empty()) {
      if (m_selectedConsoleSlug == "other") {
        if (!remote[i].platform.empty())
          continue;
      } else {
        if (strcasecmp(remote[i].platform.c_str(),
                       m_selectedConsoleSlug.c_str()) != 0) {
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

  std::sort(m_filteredRemoteIndices.begin(), m_filteredRemoteIndices.end(),
            [&](size_t a, size_t b) {
              int cmp =
                  strcasecmp(remote[a].title.c_str(), remote[b].title.c_str());
              if (cmp != 0)
                return cmp < 0;
              return remote[a].id < remote[b].id;
            });

  if (m_selectedRemoteIndex >= m_filteredRemoteIndices.size()) {
    m_selectedRemoteIndex = m_filteredRemoteIndices.empty()
                                ? 0
                                : m_filteredRemoteIndices.size() - 1;
  }
}

std::string UIManager::getActiveFilterButtonLabel() const {
  if (m_localFilterMode == LocalFilterMode::CONSOLE &&
      !m_localFilterConsole.empty()) {
    return m_localFilterConsole;
  }
  return getFilterModeName(m_localFilterMode);
}

std::string UIManager::getActiveFilterDescription() const {
  switch (m_localFilterMode) {
  case LocalFilterMode::ALL:
    return "All";
  case LocalFilterMode::MATCHED:
    return "Matched";
  case LocalFilterMode::UNMATCHED:
    return "Unmatched";
  case LocalFilterMode::WITH_SAVES:
    return "Has Save";
  case LocalFilterMode::OUTDATED:
    return "Outdated (Remote Newer)";
  case LocalFilterMode::UPDATED:
    return "Updated (Local Newer)";
  case LocalFilterMode::HIDDEN:
    return "Hidden";
  case LocalFilterMode::CONSOLE:
    return "Console: " + m_localFilterConsole;
  }
  return "All";
}

void UIManager::openFilterPicker() {
  m_filterOptions.clear();
  m_filterOptions.push_back({LocalFilterMode::ALL, "", "All"});
  m_filterOptions.push_back({LocalFilterMode::MATCHED, "", "Matched"});
  m_filterOptions.push_back({LocalFilterMode::UNMATCHED, "", "Unmatched"});
  m_filterOptions.push_back({LocalFilterMode::WITH_SAVES, "", "Has Save"});
  m_filterOptions.push_back(
      {LocalFilterMode::OUTDATED, "", "Outdated (Remote Newer)"});
  m_filterOptions.push_back(
      {LocalFilterMode::UPDATED, "", "Updated (Local Newer)"});
  m_filterOptions.push_back({LocalFilterMode::HIDDEN, "", "Hidden"});

  if (m_syncMgr) {
    std::vector<std::string> consoles;
    for (const auto &item : m_syncMgr->getItems()) {
      if (item.localTitle.console.empty())
        continue;
      std::string cUpper = item.localTitle.console;
      for (char &ch : cUpper)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      if (std::find(consoles.begin(), consoles.end(), cUpper) ==
          consoles.end()) {
        consoles.push_back(cUpper);
      }
    }
    std::sort(consoles.begin(), consoles.end());
    for (const auto &c : consoles) {
      m_filterOptions.push_back({LocalFilterMode::CONSOLE, c, "Console: " + c});
    }
  }

  m_selectedFilterPickerIndex = 0;
  for (size_t i = 0; i < m_filterOptions.size(); ++i) {
    if (m_filterOptions[i].mode == m_localFilterMode) {
      if (m_localFilterMode != LocalFilterMode::CONSOLE ||
          strcasecmp(m_filterOptions[i].console.c_str(),
                     m_localFilterConsole.c_str()) == 0) {
        m_selectedFilterPickerIndex = i;
        break;
      }
    }
  }

  m_activeSheet = ActiveSheet::FILTER;
  m_sheetSelectedIndex = static_cast<int>(m_selectedFilterPickerIndex);
  m_sheetTargetY = 56.0f;
}

void UIManager::openSortPicker() {
  m_sortOptions.clear();
  m_sortOptions.push_back({LocalSortMode::TITLE, "Title (A-Z)"});
  m_sortOptions.push_back({LocalSortMode::SAVE_STATUS, "Save (Newest First)"});

  m_selectedSortPickerIndex = 0;
  for (size_t i = 0; i < m_sortOptions.size(); ++i) {
    if (m_sortOptions[i].mode == m_localSortMode) {
      m_selectedSortPickerIndex = i;
      break;
    }
  }

  m_activeSheet = ActiveSheet::SORT;
  m_sheetSelectedIndex = static_cast<int>(m_selectedSortPickerIndex);
  m_sheetTargetY = 130.0f;
}

void UIManager::cycleLocalFilterMode() { openFilterPicker(); }

void UIManager::cycleLocalSortMode() { openSortPicker(); }

void UIManager::toggleIgnoreSelected() {
  if (!m_syncMgr || m_filteredLocalIndices.empty())
    return;
  if (m_selectedGameIndex >= m_filteredLocalIndices.size())
    return;

  size_t actualIdx = m_filteredLocalIndices[m_selectedGameIndex];
  SyncItem &item = m_syncMgr->getItems()[actualIdx];

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

void UIManager::openServerSettings() {
  if (m_configMgr) {
    const auto &cfg = m_configMgr->getConfig();
    m_enteredEndpoint =
        cfg.serverEndpoint.empty() ? "http://" : cfg.serverEndpoint;
    m_enteredApiKey = cfg.apiKey;
  }
  m_setupSelectedIndex = 0;
  m_statusMessage = "Edit RomM server endpoint or API key.";
  m_state = UIState::FIRST_LAUNCH;
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
    std::string prevEndpoint = m_configMgr->getConfig().serverEndpoint;
    std::string prevApiKey = m_configMgr->getConfig().apiKey;

    m_configMgr->setEndpoint(m_enteredEndpoint);
    m_configMgr->setApiKey(m_enteredApiKey);

    if (m_netMgr) {
      m_statusMessage = "Testing server connection...";
      render();

      NetworkResult res = m_netMgr->testConnection();
      if (!res.success) {
        m_configMgr->setEndpoint(prevEndpoint);
        m_configMgr->setApiKey(prevApiKey);
        m_statusMessage = "Conn Failed: " + res.errorMessage;
        return;
      }
    }

    if (m_configMgr->saveConfig()) {
      m_statusMessage = "Connected & Saved successfully!";
      m_state = UIState::MAIN_MENU;
      runLibraryRefresh(true);
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
  SyncItem &item = m_syncMgr->getItems()[actualIdx];

  if (item.isIgnored) {
    m_statusMessage = "Cannot sync ignored game: " + item.localTitle.name;
    return;
  }

  if (!item.isMatched) {
    m_statusMessage = "Cannot sync: Unmatched game. Press (SELECT) to match.";
    return;
  }

  if (!item.localTitle.hasSave && !item.remoteHasSave) {
    m_statusMessage = "Cannot sync: No local or remote save exists.";
    return;
  }

  SyncAction action = m_syncMgr->evaluateItem(item);
  if (action == SyncAction::CONFLICT) {
    m_conflictGameIndex = actualIdx;
    time_t localTime = item.localTitle.saveTime;
    time_t remoteTime =
        item.remoteSaveTime != 0
            ? item.remoteSaveTime
            : SyncManager::parseIsoTimestamp(item.remoteSaveUpdatedAt);
    if (remoteTime > localTime + 5) {
      m_conflictChoiceIndex = 0;
    } else if (localTime > remoteTime + 5) {
      m_conflictChoiceIndex = 1;
    } else {
      m_conflictChoiceIndex = 0;
    }
    m_state = UIState::CONFLICT_PROMPT;
    return;
  }

  openOperationLog("Sync: " + item.localTitle.name);
  auto logger = [this](const std::string &msg) { addOperationLog(msg); };

  m_syncMgr->syncItem(item, ConflictChoice::OVERWRITE_LOCAL, logger);
  m_statusMessage = m_syncMgr->getLastMessage();
  finishOperationLog();
  updateLocalFilteredList();
}

void UIManager::syncAllGames() {
  if (!m_syncMgr || m_filteredLocalIndices.empty()) {
    m_statusMessage = "No installed titles match current filter.";
    return;
  }

  m_batchSyncTargetIndices.clear();
  for (size_t idx : m_filteredLocalIndices) {
    if (idx < m_syncMgr->getItems().size()) {
      const auto &item = m_syncMgr->getItems()[idx];
      if (!item.isIgnored && item.isMatched &&
          (item.localTitle.hasSave || item.remoteHasSave)) {
        m_batchSyncTargetIndices.push_back(idx);
      }
    }
  }

  if (m_batchSyncTargetIndices.empty()) {
    m_statusMessage = "No titles with saves to batch sync.";
    return;
  }

  m_batchSyncActive = true;
  m_batchSyncIndex = 0;
  m_batchSyncSuccessCount = 0;

  openOperationLog("Batch Sync (" +
                   std::to_string(m_batchSyncTargetIndices.size()) +
                   " titles with saves)");
  processBatchSync();
}

void UIManager::processBatchSync() {
  if (!m_syncMgr || !m_batchSyncActive) {
    return;
  }

  auto logger = [this](const std::string &msg) { addOperationLog(msg); };

  while (m_batchSyncIndex < m_batchSyncTargetIndices.size()) {
    size_t actualIdx = m_batchSyncTargetIndices[m_batchSyncIndex];
    if (actualIdx >= m_syncMgr->getItems().size()) {
      m_batchSyncIndex++;
      continue;
    }

    SyncItem &item = m_syncMgr->getItems()[actualIdx];
    if (item.isIgnored || !item.isMatched ||
        (!item.localTitle.hasSave && !item.remoteHasSave)) {
      m_batchSyncIndex++;
      continue;
    }

    SyncAction act = m_syncMgr->evaluateItem(item);
    if (act == SyncAction::NO_SAVES) {
      // Skip any games with no saves
      m_batchSyncIndex++;
      continue;
    }

    if (act == SyncAction::CONFLICT) {
      // Show conflict resolution UI before sync continues
      m_conflictGameIndex = actualIdx;
      time_t localTime = item.localTitle.saveTime;
      time_t remoteTime =
          item.remoteSaveTime != 0
              ? item.remoteSaveTime
              : SyncManager::parseIsoTimestamp(item.remoteSaveUpdatedAt);
      if (remoteTime > localTime + 5) {
        m_conflictChoiceIndex = 0;
      } else if (localTime > remoteTime + 5) {
        m_conflictChoiceIndex = 1;
      } else {
        m_conflictChoiceIndex = 0;
      }

      // Hide the log dialog so the conflict UI is visible
      m_showLogDialog = false;
      m_state = UIState::CONFLICT_PROMPT;
      return;
    }

    if (m_syncMgr->syncItem(item, ConflictChoice::OVERWRITE_LOCAL, logger)) {
      m_batchSyncSuccessCount++;
    }
    m_batchSyncIndex++;
  }

  m_batchSyncActive = false;
  m_batchSyncTargetIndices.clear();
  logger("Batch sync finished (" + std::to_string(m_batchSyncSuccessCount) +
         " games synced).");
  m_statusMessage = "Batch sync complete (" +
                    std::to_string(m_batchSyncSuccessCount) + " games synced).";
  finishOperationLog();
  updateLocalFilteredList();
}

void UIManager::forceUploadSelected() {
  if (!m_syncMgr || m_filteredLocalIndices.empty())
    return;
  if (m_selectedGameIndex >= m_filteredLocalIndices.size())
    return;

  size_t actualIdx = m_filteredLocalIndices[m_selectedGameIndex];
  SyncItem &item = m_syncMgr->getItems()[actualIdx];

  if (!item.isMatched) {
    m_statusMessage = "Cannot upload: Unmatched game.";
    return;
  }
  if (!item.localTitle.hasSave) {
    m_statusMessage = "Cannot upload: No local save file found.";
    return;
  }

  // Destructive Action Guarding (NSA Pattern C):
  // Overwriting a remote save is a destructive operation.
  if (item.remoteHasSave) {
    m_conflictGameIndex = actualIdx;
    m_conflictChoiceIndex = 2; // Default to safe Cancel / Skip
    m_state = UIState::CONFLICT_PROMPT;
    return;
  }

  openOperationLog("Upload Save: " + item.localTitle.name);
  auto logger = [this](const std::string &msg) { addOperationLog(msg); };

  m_syncMgr->forceUpload(item, logger);
  m_statusMessage = m_syncMgr->getLastMessage();
  finishOperationLog();
  updateLocalFilteredList();
}

void UIManager::forceDownloadSelected() {
  if (!m_syncMgr || m_filteredLocalIndices.empty())
    return;
  if (m_selectedGameIndex >= m_filteredLocalIndices.size())
    return;

  size_t actualIdx = m_filteredLocalIndices[m_selectedGameIndex];
  SyncItem &item = m_syncMgr->getItems()[actualIdx];

  if (!item.isMatched) {
    m_statusMessage = "Cannot download: Unmatched game.";
    return;
  }
  if (!item.localTitle.hasSave) {
    m_statusMessage = "Cannot download: No local save file found.";
    return;
  }
  if (!item.remoteHasSave) {
    m_statusMessage = "Cannot download: No cloud save found on RomM.";
    return;
  }

  // Destructive Action Guarding (NSA Pattern C):
  // Overwriting a local save is a destructive operation.
  if (item.localTitle.hasSave) {
    m_conflictGameIndex = actualIdx;
    m_conflictChoiceIndex = 2; // Default to safe Cancel / Skip
    m_state = UIState::CONFLICT_PROMPT;
    return;
  }

  openOperationLog("Download Save: " + item.localTitle.name);
  auto logger = [this](const std::string &msg) { addOperationLog(msg); };

  m_syncMgr->forceDownload(item, logger);
  m_statusMessage = m_syncMgr->getLastMessage();
  finishOperationLog();
  updateLocalFilteredList();
}

void UIManager::openManualMatch() {
  if (!m_syncMgr || m_filteredLocalIndices.empty())
    return;
  if (m_selectedGameIndex >= m_filteredLocalIndices.size())
    return;

  size_t localActualIdx = m_filteredLocalIndices[m_selectedGameIndex];
  const auto &localItem = m_syncMgr->getItems()[localActualIdx];

  m_availableConsoles = m_syncMgr->getPlatforms();
  if (m_availableConsoles.empty()) {
    std::map<std::string, int> counts;
    for (const auto &g : m_syncMgr->getRemoteLibrary()) {
      if (!g.platform.empty())
        counts[g.platform]++;
      else
        counts["other"]++;
    }
    for (const auto &pair : counts) {
      RommPlatform p;
      p.slug = pair.first;
      p.name = SyncManager::getFriendlyPlatformName(pair.first);
      p.romCount = pair.second;
      m_availableConsoles.push_back(p);
    }
    std::sort(m_availableConsoles.begin(), m_availableConsoles.end(),
              [](const RommPlatform &a, const RommPlatform &b) {
                return a.name < b.name;
              });
  }

  // Default console selection based on local game console
  m_selectedConsoleIndex = 0;
  for (size_t i = 0; i < m_availableConsoles.size(); ++i) {
    if (SyncManager::isPlatformCompatible(localItem.localTitle.console,
                                          m_availableConsoles[i].slug)) {
      m_selectedConsoleIndex = i;
      break;
    }
  }

  m_matchingSubState = MatchingSubState::SELECT_CONSOLE;
  m_state = UIState::MATCHING;
}

void UIManager::selectConsole(size_t index) {
  if (index >= m_availableConsoles.size())
    return;
  m_selectedConsoleIndex = index;
  m_selectedConsoleId = m_availableConsoles[index].id;
  m_selectedConsoleSlug = m_availableConsoles[index].slug;
  m_selectedConsoleName = m_availableConsoles[index].name;

  if (m_syncMgr) {
    if (!m_syncMgr->isPlatformLoaded(m_selectedConsoleId,
                                     m_selectedConsoleSlug)) {
      m_statusMessage = "Loading " + m_selectedConsoleName + " games...";
      m_loadingMessage = "Loading " + m_selectedConsoleName + " games...";
      render();
      render();
      m_syncMgr->loadGamesForPlatform(m_selectedConsoleId,
                                      m_selectedConsoleSlug);
      m_loadingMessage.clear();
    }
  }

  m_remoteFilterText.clear();
  updateRemoteFilteredList();
  m_selectedRemoteIndex = 0;
  m_matchingSubState = MatchingSubState::SELECT_GAME;
}

void UIManager::confirmManualMatch() {
  if (!m_syncMgr || m_filteredLocalIndices.empty() ||
      m_filteredRemoteIndices.empty())
    return;
  if (m_selectedGameIndex >= m_filteredLocalIndices.size())
    return;
  if (m_selectedRemoteIndex >= m_filteredRemoteIndices.size())
    return;

  size_t localActualIdx = m_filteredLocalIndices[m_selectedGameIndex];
  size_t remoteActualIdx = m_filteredRemoteIndices[m_selectedRemoteIndex];

  const auto &remoteGames = m_syncMgr->getRemoteLibrary();
  if (remoteActualIdx >= remoteGames.size())
    return;

  m_syncMgr->manualMatch(localActualIdx, remoteGames[remoteActualIdx]);
  m_statusMessage = m_syncMgr->getLastMessage();
  updateLocalFilteredList();
  m_state = UIState::MAIN_MENU;
}

void UIManager::resolveConflict(int choice) {
  if (!m_syncMgr || m_conflictGameIndex >= m_syncMgr->getItems().size()) {
    m_state = UIState::MAIN_MENU;
    m_batchSyncActive = false;
    return;
  }

  SyncItem &item = m_syncMgr->getItems()[m_conflictGameIndex];
  ConflictChoice cChoice = (choice == 0)   ? ConflictChoice::OVERWRITE_LOCAL
                           : (choice == 1) ? ConflictChoice::OVERWRITE_REMOTE
                                           : ConflictChoice::SKIP;

  if (m_batchSyncActive) {
    m_state = UIState::MAIN_MENU;
    m_showLogDialog = true;
    m_operationInProgress = true;
    render();
    render();

    auto logger = [this](const std::string &msg) { addOperationLog(msg); };

    if (cChoice == ConflictChoice::SKIP) {
      addOperationLog("[WARN] Skipped conflict for: " + item.localTitle.name);
    } else {
      std::string actionName = (choice == 0) ? "Download Save (Overwrite Local)"
                                             : "Upload Save (Use Local Save)";
      addOperationLog("[Sync] Resolving conflict for " + item.localTitle.name +
                      " (" + actionName + ")...");
      if (m_syncMgr->syncItem(item, cChoice, logger)) {
        m_batchSyncSuccessCount++;
      }
    }

    m_batchSyncIndex++;
    processBatchSync();
    return;
  }

  if (cChoice == ConflictChoice::SKIP) {
    m_statusMessage = "Skipped conflict for " + item.localTitle.name;
    m_state = UIState::MAIN_MENU;
    return;
  }

  std::string opTitle = (choice == 0) ? "Download Save: " : "Upload Save: ";
  opTitle += item.localTitle.name;
  openOperationLog(opTitle);

  auto logger = [this](const std::string &msg) { addOperationLog(msg); };

  m_syncMgr->syncItem(item, cChoice, logger);
  m_statusMessage = m_syncMgr->getLastMessage();
  m_state = UIState::MAIN_MENU;
  finishOperationLog();
  updateLocalFilteredList();
}

void UIManager::ensureSelectedVisible() {
  if (m_filteredLocalIndices.empty())
    return;
  float targetTop = static_cast<float>(m_selectedGameIndex) * 32.0f;
  float maxScroll =
      std::max(0.0f, static_cast<float>(m_filteredLocalIndices.size()) * 32.0f -
                         160.0f);
  if (targetTop < m_targetScrollY) {
    m_targetScrollY = targetTop;
  } else if (targetTop + 32.0f > m_targetScrollY + 160.0f) {
    m_targetScrollY = targetTop + 32.0f - 160.0f;
  }
  m_targetScrollY = std::clamp(m_targetScrollY, 0.0f, maxScroll);
}

void UIManager::openGameActionsSheet() {
  if (m_filteredLocalIndices.empty())
    return;
  m_activeSheet = ActiveSheet::GAME_ACTIONS;
  m_sheetSelectedIndex = 0;
  m_sheetTargetY = 48.0f;
}

void UIManager::openAppMenuSheet() {
  m_activeSheet = ActiveSheet::APP_MENU;
  m_sheetSelectedIndex = 0;
  m_sheetTargetY = 120.0f;
}

void UIManager::closeSheet() {
  m_sheetTargetY = 240.0f;
}

bool UIManager::handleSheetInput(u32 kDown, const touchPosition &touch) {
  if (m_activeSheet == ActiveSheet::NONE) {
    return false;
  }

  int numOptions = 0;
  if (m_activeSheet == ActiveSheet::GAME_ACTIONS) {
    numOptions = 6;
  } else if (m_activeSheet == ActiveSheet::APP_MENU) {
    numOptions = 3;
  } else if (m_activeSheet == ActiveSheet::SORT) {
    numOptions = static_cast<int>(m_sortOptions.size());
  } else if (m_activeSheet == ActiveSheet::FILTER) {
    numOptions = static_cast<int>(m_filterOptions.size());
  }

  if (numOptions <= 0) {
    closeSheet();
    return true;
  }

  if (kDown & (KEY_B | KEY_START)) {
    closeSheet();
    return true;
  }

  if (kDown & (KEY_UP | KEY_CPAD_UP)) {
    m_sheetSelectedIndex =
        (m_sheetSelectedIndex > 0) ? (m_sheetSelectedIndex - 1) : (numOptions - 1);
    return true;
  }
  if (kDown & (KEY_DOWN | KEY_CPAD_DOWN)) {
    m_sheetSelectedIndex =
        (m_sheetSelectedIndex + 1 < numOptions) ? (m_sheetSelectedIndex + 1) : 0;
    return true;
  }

  auto executeGameAction = [this](int index) {
    closeSheet();
    switch (index) {
    case 0: {
      bool canSync = false;
      if (m_syncMgr && !m_filteredLocalIndices.empty() &&
          m_selectedGameIndex < m_filteredLocalIndices.size()) {
        const auto &item =
            m_syncMgr->getItems()[m_filteredLocalIndices[m_selectedGameIndex]];
        canSync = !item.isIgnored && item.isMatched &&
                  (item.localTitle.hasSave || item.remoteHasSave);
      }
      if (canSync) {
        syncSelectedGame();
      } else {
        m_statusMessage = "Cannot sync: No local or remote save exists.";
      }
      break;
    }
    case 1: {
      bool canUpload = false;
      if (m_syncMgr && !m_filteredLocalIndices.empty() &&
          m_selectedGameIndex < m_filteredLocalIndices.size()) {
        const auto &item =
            m_syncMgr->getItems()[m_filteredLocalIndices[m_selectedGameIndex]];
        canUpload = !item.isIgnored && item.isMatched && item.localTitle.hasSave;
      }
      if (canUpload) {
        forceUploadSelected();
      } else {
        m_statusMessage = "Cannot upload: No local save file found.";
      }
      break;
    }
    case 2: {
      bool canDownload = false;
      if (m_syncMgr && !m_filteredLocalIndices.empty() &&
          m_selectedGameIndex < m_filteredLocalIndices.size()) {
        const auto &item =
            m_syncMgr->getItems()[m_filteredLocalIndices[m_selectedGameIndex]];
        canDownload = !item.isIgnored && item.isMatched &&
                      item.localTitle.hasSave && item.remoteHasSave;
      }
      if (canDownload) {
        forceDownloadSelected();
      } else {
        if (m_syncMgr && !m_filteredLocalIndices.empty() &&
            m_selectedGameIndex < m_filteredLocalIndices.size()) {
          const auto &item =
              m_syncMgr->getItems()[m_filteredLocalIndices[m_selectedGameIndex]];
          if (!item.localTitle.hasSave) {
            m_statusMessage = "Cannot download: No local save file found.";
          } else {
            m_statusMessage = "Cannot download: No cloud save found on RomM.";
          }
        }
      }
      break;
    }
    case 3:
      openManualMatch();
      break;
    case 4:
      openSavePicker();
      break;
    case 5:
      toggleIgnoreSelected();
      break;
    }
  };

  auto executeAppMenuAction = [this](int index) {
    closeSheet();
    switch (index) {
    case 0:
      syncAllGames();
      break;
    case 1:
      runLibraryRefresh(true);
      break;
    case 2:
      openServerSettings();
      break;
    }
  };

  auto executeSortAction = [this](int index) {
    closeSheet();
    if (index >= 0 && index < static_cast<int>(m_sortOptions.size())) {
      m_localSortMode = m_sortOptions[index].mode;
      updateLocalFilteredList();
    }
  };

  auto executeFilterAction = [this](int index) {
    closeSheet();
    if (index >= 0 && index < static_cast<int>(m_filterOptions.size())) {
      m_localFilterMode = m_filterOptions[index].mode;
      m_localFilterConsole = m_filterOptions[index].console;
      updateLocalFilteredList();
    }
  };

  if (kDown & KEY_A) {
    if (m_activeSheet == ActiveSheet::GAME_ACTIONS) {
      executeGameAction(m_sheetSelectedIndex);
    } else if (m_activeSheet == ActiveSheet::APP_MENU) {
      executeAppMenuAction(m_sheetSelectedIndex);
    } else if (m_activeSheet == ActiveSheet::SORT) {
      executeSortAction(m_sheetSelectedIndex);
    } else if (m_activeSheet == ActiveSheet::FILTER) {
      executeFilterAction(m_sheetSelectedIndex);
    }
    return true;
  }

  if (kDown & KEY_TOUCH) {
    if (touch.py < m_sheetCurrentY) {
      closeSheet();
      return true;
    }

    if (m_activeSheet == ActiveSheet::GAME_ACTIONS) {
      float startY = m_sheetCurrentY + 28.0f;
      for (int i = 0; i < numOptions; ++i) {
        float itemY = startY + static_cast<float>(i) * 26.0f;
        if (isTouchInside(touch, 12.0f, itemY, 296.0f, 24.0f)) {
          m_sheetSelectedIndex = i;
          executeGameAction(i);
          return true;
        }
      }
    } else if (m_activeSheet == ActiveSheet::APP_MENU) {
      float startY = m_sheetCurrentY + 30.0f;
      for (int i = 0; i < numOptions; ++i) {
        float itemY = startY + static_cast<float>(i) * 28.0f;
        if (isTouchInside(touch, 12.0f, itemY, 296.0f, 26.0f)) {
          m_sheetSelectedIndex = i;
          executeAppMenuAction(i);
          return true;
        }
      }
    } else if (m_activeSheet == ActiveSheet::SORT) {
      float startY = m_sheetCurrentY + 30.0f;
      for (int i = 0; i < numOptions; ++i) {
        float itemY = startY + static_cast<float>(i) * 28.0f;
        if (isTouchInside(touch, 12.0f, itemY, 296.0f, 26.0f)) {
          m_sheetSelectedIndex = i;
          executeSortAction(i);
          return true;
        }
      }
    } else if (m_activeSheet == ActiveSheet::FILTER) {
      int windowStart = 0;
      if (m_sheetSelectedIndex >= 5) {
        windowStart = m_sheetSelectedIndex - 4;
      }
      int visibleCount = std::min(5, numOptions - windowStart);
      float startY = m_sheetCurrentY + 28.0f;
      for (int i = 0; i < visibleCount; ++i) {
        float itemY = startY + static_cast<float>(i) * 26.0f;
        if (isTouchInside(touch, 12.0f, itemY, 296.0f, 24.0f)) {
          m_sheetSelectedIndex = windowStart + i;
          executeFilterAction(m_sheetSelectedIndex);
          return true;
        }
      }
    }
  }

  return true;
}

void UIManager::renderBottomSheet() {
  if (m_activeSheet == ActiveSheet::NONE && m_sheetCurrentY >= 239.0f) {
    return;
  }

  // Backdrop dim overlay
  C2D_DrawRectSolid(0.0f, 0.0f, 0.75f, RomM3DS::SCREEN_BOTTOM_WIDTH,
                    RomM3DS::SCREEN_BOTTOM_HEIGHT, CLR_DIM_OVERLAY);

  float sheetH = 240.0f - m_sheetCurrentY;
  if (sheetH <= 0.0f)
    return;

  // Bottom sheet card
  C2D_DrawRectSolid(0.0f, m_sheetCurrentY, 0.80f,
                    RomM3DS::SCREEN_BOTTOM_WIDTH, sheetH, CLR_SURFACE_CARD);
  // Purple top accent bar
  C2D_DrawRectSolid(0.0f, m_sheetCurrentY, 0.82f,
                    RomM3DS::SCREEN_BOTTOM_WIDTH, 2.0f, CLR_ACCENT_PRIMARY);
  // Center grab handle pill
  C2D_DrawRectSolid(144.0f, m_sheetCurrentY + 5.0f, 0.84f, 32.0f, 3.0f,
                    CLR_BORDER_SUBTLE);

  if (m_activeSheet == ActiveSheet::GAME_ACTIONS) {
    std::string titleStr = "Game Actions";
    if (!m_filteredLocalIndices.empty() &&
        m_selectedGameIndex < m_filteredLocalIndices.size()) {
      size_t actualIdx = m_filteredLocalIndices[m_selectedGameIndex];
      titleStr = m_syncMgr->getItems()[actualIdx].localTitle.name;
      if (titleStr.length() > 32) {
        titleStr = titleStr.substr(0, 30) + "..";
      }
    }
    C2D_Text titleTxt;
    C2D_TextParse(&titleTxt, m_textBuf, titleStr.c_str());
    C2D_TextOptimize(&titleTxt);
    C2D_DrawText(&titleTxt, C2D_WithColor, 16.0f, m_sheetCurrentY + 11.0f,
                 0.85f, 0.42f, 0.42f, CLR_ACCENT_PRIMARY);

    bool isIgnored = (!m_filteredLocalIndices.empty() &&
                      m_selectedGameIndex < m_filteredLocalIndices.size() &&
                      m_syncMgr->getItems()[m_filteredLocalIndices[m_selectedGameIndex]]
                          .isIgnored);

    bool hasSaves = false;
    bool hasLocalSave = false;
    bool hasRemoteSave = false;
    if (m_syncMgr && !m_filteredLocalIndices.empty() &&
        m_selectedGameIndex < m_filteredLocalIndices.size()) {
      const auto &item =
          m_syncMgr->getItems()[m_filteredLocalIndices[m_selectedGameIndex]];
      hasLocalSave = !item.isIgnored && item.isMatched && item.localTitle.hasSave;
      hasRemoteSave = !item.isIgnored && item.isMatched && item.remoteHasSave;
      hasSaves = !item.isIgnored && item.isMatched &&
                 (item.localTitle.hasSave || item.remoteHasSave);
    }

    const char *options[6] = {
        "Sync Save Now (A)",
        "Force Upload Local Save (L)",
        "Force Download Remote Save (R)",
        "Manual Match with RomM (SEL)",
        "Set Custom Save Path",
        isIgnored ? "Unhide Title" : "Hide Title"};

    float startY = m_sheetCurrentY + 28.0f;
    for (int i = 0; i < 6; ++i) {
      float itemY = startY + static_cast<float>(i) * 26.0f;
      bool selected = (i == m_sheetSelectedIndex);
      bool isOptionDisabled = false;
      if (i == 0 && !hasSaves) {
        isOptionDisabled = true;
      } else if (i == 1 && !hasLocalSave) {
        isOptionDisabled = true;
      } else if (i == 2 && (!hasLocalSave || !hasRemoteSave)) {
        isOptionDisabled = true;
      }

      u32 bg = selected ? CLR_SURFACE_ELEVATED : CLR_SURFACE_ALT;
      u32 border = selected ? CLR_ACCENT_PRIMARY : CLR_BORDER_SUBTLE;
      u32 textColor = selected ? CLR_TEXT_PRIMARY : CLR_TEXT_SECONDARY;
      if (isOptionDisabled) {
        textColor = CLR_TEXT_DIM;
        if (!selected) {
          bg = CLR_SURFACE_ALT;
        }
      }

      C2D_DrawRectSolid(12.0f, itemY, 0.84f, 296.0f, 24.0f, border);
      C2D_DrawRectSolid(13.0f, itemY + 1.0f, 0.85f, 294.0f, 22.0f, bg);
      if (selected) {
        C2D_DrawRectSolid(12.0f, itemY, 0.86f, 3.0f, 24.0f,
                          CLR_ACCENT_PRIMARY);
      }

      C2D_Text optTxt;
      C2D_TextParse(&optTxt, m_textBuf, options[i]);
      C2D_TextOptimize(&optTxt);
      C2D_DrawText(&optTxt, C2D_WithColor, 22.0f, itemY + 5.0f, 0.87f, 0.38f,
                   0.38f, textColor);
    }
  } else if (m_activeSheet == ActiveSheet::APP_MENU) {
    C2D_Text titleTxt;
    C2D_TextParse(&titleTxt, m_textBuf, "Library & Settings");
    C2D_TextOptimize(&titleTxt);
    C2D_DrawText(&titleTxt, C2D_WithColor, 16.0f, m_sheetCurrentY + 11.0f,
                 0.85f, 0.44f, 0.44f, CLR_ACCENT_PRIMARY);

    const char *options[3] = {"Batch Sync All Filtered (X)",
                              "Refresh Library & Saves",
                              "Server Settings (Endpoint & API Key)"};

    float startY = m_sheetCurrentY + 30.0f;
    for (int i = 0; i < 3; ++i) {
      float itemY = startY + static_cast<float>(i) * 28.0f;
      bool selected = (i == m_sheetSelectedIndex);
      u32 bg = selected ? CLR_SURFACE_ELEVATED : CLR_SURFACE_ALT;
      u32 border = selected ? CLR_ACCENT_PRIMARY : CLR_BORDER_SUBTLE;
      u32 textColor = selected ? CLR_TEXT_PRIMARY : CLR_TEXT_SECONDARY;

      C2D_DrawRectSolid(12.0f, itemY, 0.84f, 296.0f, 26.0f, border);
      C2D_DrawRectSolid(13.0f, itemY + 1.0f, 0.85f, 294.0f, 24.0f, bg);
      if (selected) {
        C2D_DrawRectSolid(12.0f, itemY, 0.86f, 3.0f, 26.0f,
                          CLR_ACCENT_PRIMARY);
      }

      C2D_Text optTxt;
      C2D_TextParse(&optTxt, m_textBuf, options[i]);
      C2D_TextOptimize(&optTxt);
      C2D_DrawText(&optTxt, C2D_WithColor, 22.0f, itemY + 6.0f, 0.87f, 0.40f,
                   0.40f, textColor);
    }
  } else if (m_activeSheet == ActiveSheet::SORT) {
    C2D_Text titleTxt;
    C2D_TextParse(&titleTxt, m_textBuf, "Sort Installed Games");
    C2D_TextOptimize(&titleTxt);
    C2D_DrawText(&titleTxt, C2D_WithColor, 16.0f, m_sheetCurrentY + 11.0f,
                 0.85f, 0.44f, 0.44f, CLR_ACCENT_PRIMARY);

    float startY = m_sheetCurrentY + 30.0f;
    for (size_t i = 0; i < m_sortOptions.size(); ++i) {
      float itemY = startY + static_cast<float>(i) * 28.0f;
      bool selected = (static_cast<int>(i) == m_sheetSelectedIndex);
      bool active = (m_sortOptions[i].mode == m_localSortMode);
      u32 bg = selected ? CLR_SURFACE_ELEVATED : CLR_SURFACE_ALT;
      u32 border = selected ? CLR_ACCENT_PRIMARY : CLR_BORDER_SUBTLE;
      u32 textColor = selected ? CLR_TEXT_PRIMARY : CLR_TEXT_SECONDARY;

      C2D_DrawRectSolid(12.0f, itemY, 0.84f, 296.0f, 26.0f, border);
      C2D_DrawRectSolid(13.0f, itemY + 1.0f, 0.85f, 294.0f, 24.0f, bg);
      if (selected) {
        C2D_DrawRectSolid(12.0f, itemY, 0.86f, 3.0f, 26.0f,
                          CLR_ACCENT_PRIMARY);
      }

      u32 dotColor = active ? CLR_GREEN : CLR_BORDER_SUBTLE;
      C2D_DrawRectSolid(20.0f, itemY + 10.0f, 0.86f, 6.0f, 6.0f, dotColor);

      C2D_Text optTxt;
      C2D_TextParse(&optTxt, m_textBuf, m_sortOptions[i].label.c_str());
      C2D_TextOptimize(&optTxt);
      C2D_DrawText(&optTxt, C2D_WithColor, 32.0f, itemY + 6.0f, 0.87f, 0.40f,
                   0.40f, textColor);

      if (active) {
        drawPillBadge(248.0f, itemY + 6.0f, "ACTIVE", CLR_SURFACE_HEADER,
                      CLR_GREEN, 0.34f, 0.87f);
      }
    }
  } else if (m_activeSheet == ActiveSheet::FILTER) {
    std::string titleStr = "Filter Library (" +
                           std::to_string(m_filterOptions.size()) +
                           " options)";
    C2D_Text titleTxt;
    C2D_TextParse(&titleTxt, m_textBuf, titleStr.c_str());
    C2D_TextOptimize(&titleTxt);
    C2D_DrawText(&titleTxt, C2D_WithColor, 16.0f, m_sheetCurrentY + 11.0f,
                 0.85f, 0.44f, 0.44f, CLR_ACCENT_PRIMARY);

    int windowStart = 0;
    if (m_sheetSelectedIndex >= 5) {
      windowStart = m_sheetSelectedIndex - 4;
    }
    int visibleCount =
        std::min(5, static_cast<int>(m_filterOptions.size()) - windowStart);

    float startY = m_sheetCurrentY + 28.0f;
    for (int i = 0; i < visibleCount; ++i) {
      int idx = windowStart + i;
      const auto &opt = m_filterOptions[idx];
      float itemY = startY + static_cast<float>(i) * 26.0f;
      bool selected = (idx == m_sheetSelectedIndex);
      bool active =
          (opt.mode == m_localFilterMode &&
           (opt.mode != LocalFilterMode::CONSOLE ||
            strcasecmp(opt.console.c_str(), m_localFilterConsole.c_str()) == 0));

      u32 bg = selected ? CLR_SURFACE_ELEVATED : CLR_SURFACE_ALT;
      u32 border = selected ? CLR_ACCENT_PRIMARY : CLR_BORDER_SUBTLE;
      u32 textColor = selected ? CLR_TEXT_PRIMARY : CLR_TEXT_SECONDARY;

      C2D_DrawRectSolid(12.0f, itemY, 0.84f, 296.0f, 24.0f, border);
      C2D_DrawRectSolid(13.0f, itemY + 1.0f, 0.85f, 294.0f, 22.0f, bg);
      if (selected) {
        C2D_DrawRectSolid(12.0f, itemY, 0.86f, 3.0f, 24.0f,
                          CLR_ACCENT_PRIMARY);
      }

      u32 dotColor = active ? CLR_GREEN : CLR_BORDER_SUBTLE;
      C2D_DrawRectSolid(20.0f, itemY + 9.0f, 0.86f, 6.0f, 6.0f, dotColor);

      C2D_Text optTxt;
      C2D_TextParse(&optTxt, m_textBuf, opt.label.c_str());
      C2D_TextOptimize(&optTxt);
      C2D_DrawText(&optTxt, C2D_WithColor, 32.0f, itemY + 5.0f, 0.87f, 0.38f,
                   0.38f, textColor);

      if (active) {
        drawPillBadge(248.0f, itemY + 5.0f, "ACTIVE", CLR_SURFACE_HEADER,
                      CLR_GREEN, 0.32f, 0.87f);
      }
    }

    if (m_filterOptions.size() > 5) {
      float scrollH = 130.0f;
      float thumbH = std::max(
          16.0f, scrollH * (5.0f / static_cast<float>(m_filterOptions.size())));
      float maxOffset = static_cast<float>(m_filterOptions.size() - 5);
      float thumbY =
          startY + (scrollH - thumbH) * (static_cast<float>(windowStart) / maxOffset);
      C2D_DrawRectSolid(311.0f, startY, 0.86f, 2.0f, scrollH, CLR_BORDER_SUBTLE);
      C2D_DrawRectSolid(310.0f, thumbY, 0.88f, 3.0f, thumbH, CLR_ACCENT_PRIMARY);
    }
  }
}

void UIManager::handleInput(u32 kDown, u32 kHeld, u32 kUp, const touchPosition &touch) {
  m_touchHeld = (kHeld & KEY_TOUCH) != 0;
  if (m_touchHeld || (kDown & KEY_TOUCH)) {
    m_activeTouch = touch;
  }

  if (m_activeSheet != ActiveSheet::NONE) {
    if (handleSheetInput(kDown, touch)) {
      return;
    }
  }

  if (m_showLogDialog) {
    if (!m_operationInProgress) {
      if (kDown & (KEY_A | KEY_B | KEY_START)) {
        m_showLogDialog = false;
        m_operationLogs.clear();
        m_logScrollOffset = 0;
        m_batchSyncActive = false;
        updateLocalFilteredList();
        return;
      }
      if (kDown & KEY_TOUCH) {
        if (touch.py >= 196) {
          m_showLogDialog = false;
          m_operationLogs.clear();
          m_logScrollOffset = 0;
          m_batchSyncActive = false;
          updateLocalFilteredList();
          return;
        }
        if (touch.py >= 30 && touch.py < 114) {
          constexpr int maxVisible = 12;
          if ((int)m_operationLogs.size() > maxVisible) {
            m_logScrollOffset =
                std::min(m_logScrollOffset + 1,
                         (int)m_operationLogs.size() - maxVisible);
          }
        } else if (touch.py >= 114 && touch.py < 196) {
          m_logScrollOffset = std::max(0, m_logScrollOffset - 1);
        }
      }
      if (kDown & KEY_UP) {
        constexpr int maxVisible = 12;
        if ((int)m_operationLogs.size() > maxVisible) {
          m_logScrollOffset = std::min(
              m_logScrollOffset + 1, (int)m_operationLogs.size() - maxVisible);
        }
      } else if (kDown & KEY_DOWN) {
        m_logScrollOffset = std::max(0, m_logScrollOffset - 1);
      }
    }
    return;
  }

  if (m_state == UIState::FIRST_LAUNCH) {
    bool canCancel = (m_configMgr && m_configMgr->hasValidConfig());
    int numOptions = canCancel ? 4 : 3;

    if (kDown & KEY_UP) {
      m_setupSelectedIndex =
          (m_setupSelectedIndex - 1 + numOptions) % numOptions;
    } else if (kDown & KEY_DOWN) {
      m_setupSelectedIndex = (m_setupSelectedIndex + 1) % numOptions;
    } else if (canCancel && (kDown & (KEY_LEFT | KEY_RIGHT)) &&
               m_setupSelectedIndex >= 2) {
      m_setupSelectedIndex = (m_setupSelectedIndex == 2) ? 3 : 2;
    }

    if (canCancel && (kDown & KEY_B)) {
      const auto &cfg = m_configMgr->getConfig();
      m_enteredEndpoint = cfg.serverEndpoint;
      m_enteredApiKey = cfg.apiKey;
      m_statusMessage = "Cancelled settings changes.";
      m_state = UIState::MAIN_MENU;
      return;
    }

    if (kDown & KEY_A) {
      if (m_setupSelectedIndex == 0) {
        m_enteredEndpoint =
            launchKeyboard("Enter RomM Server URL", m_enteredEndpoint, 128);
      } else if (m_setupSelectedIndex == 1) {
        m_enteredApiKey =
            launchKeyboard("Enter RomM API Key", m_enteredApiKey, 128);
      } else if (m_setupSelectedIndex == 2) {
        executeSaveAction();
      } else if (canCancel && m_setupSelectedIndex == 3) {
        const auto &cfg = m_configMgr->getConfig();
        m_enteredEndpoint = cfg.serverEndpoint;
        m_enteredApiKey = cfg.apiKey;
        m_statusMessage = "Cancelled settings changes.";
        m_state = UIState::MAIN_MENU;
      }
    }

    if (kDown & KEY_TOUCH) {
      if (isTouchInside(touch, 16.0f, 44.0f, 288.0f, 32.0f)) {
        m_setupSelectedIndex = 0;
        m_enteredEndpoint =
            launchKeyboard("Enter RomM Server URL", m_enteredEndpoint, 128);
      } else if (isTouchInside(touch, 16.0f, 96.0f, 288.0f, 32.0f)) {
        m_setupSelectedIndex = 1;
        m_enteredApiKey =
            launchKeyboard("Enter RomM API Key", m_enteredApiKey, 128);
      } else if (canCancel) {
        if (isTouchInside(touch, 16.0f, 148.0f, 186.0f, 34.0f)) {
          m_setupSelectedIndex = 2;
          executeSaveAction();
        } else if (isTouchInside(touch, 210.0f, 148.0f, 94.0f, 34.0f)) {
          m_setupSelectedIndex = 3;
          const auto &cfg = m_configMgr->getConfig();
          m_enteredEndpoint = cfg.serverEndpoint;
          m_enteredApiKey = cfg.apiKey;
          m_statusMessage = "Cancelled settings changes.";
          m_state = UIState::MAIN_MENU;
        }
      } else if (isTouchInside(touch, 16.0f, 148.0f, 288.0f, 34.0f)) {
        m_setupSelectedIndex = 2;
        executeSaveAction();
      }
    }
  } else if (m_state == UIState::MAIN_MENU) {
    size_t total = m_filteredLocalIndices.size();

    if (total > 0) {
      if (kDown & (KEY_UP | KEY_CPAD_UP)) {
        m_selectedGameIndex =
            (m_selectedGameIndex > 0) ? (m_selectedGameIndex - 1) : (total - 1);
        m_holdRepeatTimer = 0;
        ensureSelectedVisible();
      } else if (kDown & (KEY_DOWN | KEY_CPAD_DOWN)) {
        m_selectedGameIndex =
            (m_selectedGameIndex + 1 < total) ? (m_selectedGameIndex + 1) : 0;
        m_holdRepeatTimer = 0;
        ensureSelectedVisible();
      } else if (kHeld & (KEY_UP | KEY_CPAD_UP | KEY_DOWN | KEY_CPAD_DOWN)) {
        m_holdRepeatTimer++;
        if (m_holdRepeatTimer >= 18 && ((m_holdRepeatTimer - 18) % 4 == 0)) {
          if (kHeld & (KEY_UP | KEY_CPAD_UP)) {
            m_selectedGameIndex =
                (m_selectedGameIndex > 0) ? (m_selectedGameIndex - 1) : (total - 1);
          } else {
            m_selectedGameIndex =
                (m_selectedGameIndex + 1 < total) ? (m_selectedGameIndex + 1) : 0;
          }
          ensureSelectedVisible();
        }
      } else {
        m_holdRepeatTimer = 0;
      }

      if (kDown & (KEY_LEFT | KEY_CPAD_LEFT)) {
        m_selectedGameIndex =
            (m_selectedGameIndex >= 5) ? (m_selectedGameIndex - 5) : 0;
        ensureSelectedVisible();
      } else if (kDown & (KEY_RIGHT | KEY_CPAD_RIGHT)) {
        m_selectedGameIndex = (m_selectedGameIndex + 5 < total)
                                  ? (m_selectedGameIndex + 5)
                                  : (total - 1);
        ensureSelectedVisible();
      }
    }

    bool canSyncSelected = false;
    if (m_syncMgr && !m_filteredLocalIndices.empty() &&
        m_selectedGameIndex < m_filteredLocalIndices.size()) {
      size_t actualIdx = m_filteredLocalIndices[m_selectedGameIndex];
      const auto &item = m_syncMgr->getItems()[actualIdx];
      canSyncSelected = !item.isIgnored && item.isMatched &&
                        (item.localTitle.hasSave || item.remoteHasSave);
    }

    if (kDown & KEY_A) {
      if (canSyncSelected) {
        syncSelectedGame();
      } else {
        m_statusMessage = "Cannot sync: No local or remote save exists.";
      }
    } else if (kDown & KEY_SELECT) {
      openGameActionsSheet();
    } else if (kDown & KEY_X) {
      openAppMenuSheet();
    } else if (kDown & KEY_L) {
      forceUploadSelected();
    } else if (kDown & KEY_R) {
      forceDownloadSelected();
    } else if (kDown & KEY_Y) {
      std::string q =
          launchKeyboard("Search installed games...", m_localFilterText, 64);
      if (q != m_localFilterText) {
        m_localFilterText = q;
        updateLocalFilteredList();
      }
    }

    if (kDown & KEY_TOUCH) {
      if (isTouchInside(touch, 8.0f, 4.0f, 92.0f, 30.0f)) {
        openSortPicker();
      } else if (isTouchInside(touch, 104.0f, 4.0f, 114.0f, 30.0f)) {
        openFilterPicker();
      } else if (m_localFilterText.empty() &&
                 isTouchInside(touch, 222.0f, 4.0f, 90.0f, 30.0f)) {
        std::string q =
            launchKeyboard("Search installed games...", m_localFilterText, 64);
        if (q != m_localFilterText) {
          m_localFilterText = q;
          updateLocalFilteredList();
        }
      } else if (!m_localFilterText.empty() &&
                 isTouchInside(touch, 222.0f, 4.0f, 62.0f, 30.0f)) {
        std::string q =
            launchKeyboard("Search installed games...", m_localFilterText, 64);
        if (q != m_localFilterText) {
          m_localFilterText = q;
          updateLocalFilteredList();
        }
      } else if (!m_localFilterText.empty() &&
                 isTouchInside(touch, 286.0f, 4.0f, 26.0f, 30.0f)) {
        m_localFilterText.clear();
        updateLocalFilteredList();
      } else if (isTouchInside(touch, 8.0f, 203.0f, 96.0f, 32.0f)) {
        if (canSyncSelected) {
          syncSelectedGame();
        } else {
          m_statusMessage = "Cannot sync: No local or remote save exists.";
        }
      } else if (isTouchInside(touch, 108.0f, 203.0f, 104.0f, 32.0f)) {
        openGameActionsSheet();
      } else if (isTouchInside(touch, 216.0f, 203.0f, 96.0f, 32.0f)) {
        openAppMenuSheet();
      } else if (touch.py >= 38 && touch.py < 198) {
        m_touchStartY = touch.py;
        m_lastTouchY = touch.py;
        m_isDragging = false;
        m_velocityY = 0.0f;
      }
    }

    if (kHeld & KEY_TOUCH) {
      if (touch.py >= 38 && touch.py < 198) {
        float dy = static_cast<float>(touch.py) - m_lastTouchY;
        if (!m_isDragging &&
            std::abs(static_cast<float>(touch.py) - m_touchStartY) > 5.0f) {
          m_isDragging = true;
        }
        if (m_isDragging) {
          float maxScroll =
              std::max(0.0f, static_cast<float>(total) * 32.0f - 160.0f);
          m_targetScrollY =
              std::clamp(m_targetScrollY - dy, 0.0f, maxScroll);
          m_scrollY = m_targetScrollY;
          m_velocityY = -dy;
          m_lastTouchY = touch.py;
        }
      }
    }

    if (kUp & KEY_TOUCH) {
      if (m_isDragging) {
        m_isDragging = false;
      } else if (touch.py >= 38 && touch.py < 198 && touch.px >= 8 &&
                 touch.px <= 312) {
        float tappedListY =
            static_cast<float>(touch.py) - 38.0f + m_scrollY;
        int tappedRow = static_cast<int>(tappedListY / 32.0f);
        if (tappedRow >= 0 && tappedRow < static_cast<int>(total)) {
          if (m_selectedGameIndex == static_cast<size_t>(tappedRow)) {
            openGameActionsSheet();
          } else {
            m_selectedGameIndex = static_cast<size_t>(tappedRow);
            ensureSelectedVisible();
          }
        }
      }
    }
  } else if (m_state == UIState::MATCHING) {
    if (m_matchingSubState == MatchingSubState::SELECT_CONSOLE) {
      size_t consoleTotal = m_availableConsoles.size();
      if (consoleTotal > 0) {
        if (kDown & KEY_UP) {
          m_selectedConsoleIndex = (m_selectedConsoleIndex > 0)
                                       ? (m_selectedConsoleIndex - 1)
                                       : (consoleTotal - 1);
        } else if (kDown & KEY_DOWN) {
          m_selectedConsoleIndex = (m_selectedConsoleIndex + 1 < consoleTotal)
                                       ? (m_selectedConsoleIndex + 1)
                                       : 0;
        } else if (kDown & KEY_LEFT) {
          m_selectedConsoleIndex =
              (m_selectedConsoleIndex >= 5) ? (m_selectedConsoleIndex - 5) : 0;
        } else if (kDown & KEY_RIGHT) {
          m_selectedConsoleIndex = (m_selectedConsoleIndex + 5 < consoleTotal)
                                       ? (m_selectedConsoleIndex + 5)
                                       : (consoleTotal - 1);
        }
      }

      if (kDown & KEY_A) {
        selectConsole(m_selectedConsoleIndex);
      } else if (kDown & KEY_B) {
        m_state = UIState::MAIN_MENU;
      } else if (kDown & KEY_X) {
        runLibraryRefresh(true);
        m_availableConsoles =
            m_syncMgr ? m_syncMgr->getPlatforms() : std::vector<RommPlatform>();
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
          m_selectedRemoteIndex = (m_selectedRemoteIndex > 0)
                                      ? (m_selectedRemoteIndex - 1)
                                      : (remoteTotal - 1);
        } else if (kDown & KEY_DOWN) {
          m_selectedRemoteIndex = (m_selectedRemoteIndex + 1 < remoteTotal)
                                      ? (m_selectedRemoteIndex + 1)
                                      : 0;
        } else if (kDown & KEY_LEFT) {
          m_selectedRemoteIndex =
              (m_selectedRemoteIndex >= 5) ? (m_selectedRemoteIndex - 5) : 0;
        } else if (kDown & KEY_RIGHT) {
          m_selectedRemoteIndex = (m_selectedRemoteIndex + 5 < remoteTotal)
                                      ? (m_selectedRemoteIndex + 5)
                                      : (remoteTotal - 1);
        }
      }

      if (kDown & KEY_A) {
        confirmManualMatch();
      } else if (kDown & KEY_B) {
        m_matchingSubState = MatchingSubState::SELECT_CONSOLE;
      } else if (kDown & KEY_Y) {
        std::string prompt = "Search in " + m_selectedConsoleName + "...";
        std::string q = launchKeyboard(prompt, m_remoteFilterText, 64);
        if (q != m_remoteFilterText) {
          m_remoteFilterText = q;
          updateRemoteFilteredList();
        }
      } else if (kDown & KEY_X) {
        if (!m_remoteFilterText.empty()) {
          m_remoteFilterText.clear();
          updateRemoteFilteredList();
        }
      } else if (kDown & KEY_L) {
        if (m_syncMgr) {
          m_statusMessage = "Refreshing " + m_selectedConsoleName + " games...";
          m_loadingMessage =
              "Refreshing " + m_selectedConsoleName + " games...";
          render();
          render();
          m_syncMgr->loadGamesForPlatform(m_selectedConsoleId,
                                          m_selectedConsoleSlug, true);
          m_loadingMessage.clear();
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
  } else if (m_state == UIState::SAVE_PICKER) {
    handleSavePickerInput(kDown, touch);
  } else if (m_state == UIState::FILTER_PICKER) {
    handleFilterPickerInput(kDown, touch);
  } else if (m_state == UIState::SORT_PICKER) {
    handleSortPickerInput(kDown, touch);
  }
}

void UIManager::drawInputBox(float x, float y, float w, float h,
                             const char *label, const std::string &value,
                             bool selected) {
  bool isPressed = m_touchHeld && isTouchInside(m_activeTouch, x, y, w, h);
  u32 borderColor =
      isPressed ? CLR_ACCENT_SOFT
                : (selected ? CLR_ACCENT_PRIMARY : CLR_BORDER_SUBTLE);
  u32 boxBg =
      (selected || isPressed) ? CLR_SURFACE_ELEVATED : CLR_SURFACE_ALT;

  C2D_DrawRectSolid(x, y, 0.5f, w, h, borderColor);
  C2D_DrawRectSolid(x + 1.5f, y + 1.5f, 0.5f, w - 3.0f, h - 3.0f, boxBg);
  if (selected || isPressed) {
    C2D_DrawRectSolid(x, y, 0.5f, 3.0f, h,
                      isPressed ? CLR_ACCENT_PRESSED : CLR_ACCENT_PRIMARY);
  }

  C2D_Text lblText;
  C2D_TextParse(&lblText, m_textBuf, label);
  C2D_TextOptimize(&lblText);
  C2D_DrawText(&lblText, C2D_WithColor, x, y - 14.0f, 0.5f, 0.45f, 0.45f,
               selected ? CLR_ACCENT_SOFT : CLR_TEXT_SECONDARY);

  std::string displayVal = value.empty() ? "<Empty - Tap or Press (A)>" : value;
  u32 valColor = value.empty() ? CLR_TEXT_DIM : CLR_TEXT_PRIMARY;

  C2D_Text valText;
  C2D_TextParse(&valText, m_textBuf, displayVal.c_str());
  C2D_TextOptimize(&valText);
  float textOffsetY = isPressed ? 9.0f : 8.0f;
  C2D_DrawText(&valText, C2D_WithColor, x + 8.0f, y + textOffsetY, 0.5f, 0.48f,
               0.48f, valColor);
}

void UIManager::drawButton(float x, float y, float w, float h,
                           const char *label, bool selected, u32 accentColor,
                           float z, bool disabled) {
  if (disabled) {
    C2D_DrawRectSolid(x, y, z, w, h, CLR_BORDER_SUBTLE);
    C2D_DrawRectSolid(x + 1.5f, y + 1.5f, z, w - 3.0f, h - 3.0f, CLR_SURFACE_ALT);

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
    C2D_DrawText(&btnText, C2D_WithColor, textX, textY, z + 0.01f, scale, scale,
                 CLR_TEXT_DIM);
    return;
  }

  bool isPressed = m_touchHeld && isTouchInside(m_activeTouch, x, y, w, h);
  u32 baseAccent = (accentColor != 0) ? accentColor : CLR_ACCENT_PRIMARY;

  u32 btnBg = CLR_SURFACE_CARD;
  u32 borderColor = (accentColor != 0) ? accentColor : CLR_BORDER_SUBTLE;
  if (isPressed) {
    btnBg = CLR_ACCENT_PRESSED;
    borderColor = CLR_ACCENT_SOFT;
  } else if (selected) {
    btnBg = CLR_SURFACE_ELEVATED;
    borderColor = baseAccent;
  }

  C2D_DrawRectSolid(x, y, z, w, h, borderColor);
  C2D_DrawRectSolid(x + 1.5f, y + 1.5f, z, w - 3.0f, h - 3.0f, btnBg);
  if (selected || isPressed) {
    C2D_DrawRectSolid(x, y, z, 3.0f, h,
                      isPressed ? CLR_ACCENT_SOFT : baseAccent);
  }

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
  float textY = y + (h - textH) / 2.0f + (isPressed ? 1.0f : 0.0f);
  C2D_DrawText(&btnText, C2D_WithColor, textX, textY, z + 0.01f, scale, scale,
               CLR_TEXT_PRIMARY);
}

float UIManager::drawPillBadge(float x, float y, const char *text, u32 bgColor,
                               u32 textColor, float scale, float z) {
  C2D_Text pillText;
  C2D_TextParse(&pillText, m_textBuf, text);
  C2D_TextOptimize(&pillText);

  float textW = 0.0f, textH = 0.0f;
  C2D_TextGetDimensions(&pillText, scale, scale, &textW, &textH);

  float padX = 6.0f;
  float padY = 2.5f;
  float pillW = textW + padX * 2.0f;
  float pillH = textH + padY * 2.0f;

  C2D_DrawRectSolid(x, y, z, pillW, pillH, CLR_BORDER_SUBTLE);
  C2D_DrawRectSolid(x + 1.0f, y + 1.0f, z, pillW - 2.0f, pillH - 2.0f,
                    bgColor);
  C2D_DrawText(&pillText, C2D_WithColor, x + padX, y + padY, z + 0.01f, scale, scale,
               textColor);
  return pillW;
}

void UIManager::drawLoadingOverlay(const std::string &message) {
  C2D_DrawRectSolid(0.0f, 0.0f, 0.7f, RomM3DS::SCREEN_BOTTOM_WIDTH,
                    RomM3DS::SCREEN_BOTTOM_HEIGHT, CLR_DIM_OVERLAY);

  float boxW = 260.0f;
  float boxH = 74.0f;
  float boxX = (RomM3DS::SCREEN_BOTTOM_WIDTH - boxW) / 2.0f;
  float boxY = (RomM3DS::SCREEN_BOTTOM_HEIGHT - boxH) / 2.0f;

  C2D_DrawRectSolid(boxX, boxY, 0.75f, boxW, boxH, CLR_ACCENT_PRIMARY);
  C2D_DrawRectSolid(boxX + 2.0f, boxY + 2.0f, 0.75f, boxW - 4.0f, boxH - 4.0f,
                    CLR_SURFACE_HEADER);
  C2D_DrawRectSolid(boxX, boxY, 0.76f, 4.0f, boxH, CLR_ACCENT_PRIMARY);

  C2D_Text headerText;
  C2D_TextParse(&headerText, m_textBuf, "Please wait...");
  C2D_TextOptimize(&headerText);
  C2D_DrawText(&headerText, C2D_WithColor, boxX + 16.0f, boxY + 14.0f, 0.8f,
               0.50f, 0.50f, CLR_ACCENT_SOFT);

  C2D_Text msgText;
  C2D_TextParse(&msgText, m_textBuf, message.c_str());
  C2D_TextOptimize(&msgText);

  float msgScale = 0.42f;
  float msgW = 0.0f, msgH = 0.0f;
  C2D_TextGetDimensions(&msgText, msgScale, msgScale, &msgW, &msgH);
  if (msgW > (boxW - 32.0f) && msgW > 0.0f) {
    msgScale *= ((boxW - 32.0f) / msgW);
  }
  C2D_DrawText(&msgText, C2D_WithColor, boxX + 16.0f, boxY + 40.0f, 0.8f,
               msgScale, msgScale, CLR_TEXT_PRIMARY);
}

static std::vector<std::string> softWrapLogLine(const std::string &line,
                                                size_t maxLen = 38) {
  std::vector<std::string> result;
  if (line.empty()) {
    result.push_back("");
    return result;
  }

  size_t lineStart = 0;
  while (lineStart < line.size()) {
    size_t newlinePos = line.find('\n', lineStart);
    std::string segment = (newlinePos == std::string::npos)
                              ? line.substr(lineStart)
                              : line.substr(lineStart, newlinePos - lineStart);

    if (segment.empty()) {
      result.push_back("");
    } else {
      size_t segStart = 0;
      bool isFirstSubLine = (result.empty());

      while (segStart < segment.size()) {
        size_t currentLimit =
            isFirstSubLine ? maxLen : (maxLen > 2 ? maxLen - 2 : maxLen);
        size_t remaining = segment.size() - segStart;

        if (remaining <= currentLimit) {
          std::string chunk = segment.substr(segStart);
          if (!isFirstSubLine) {
            chunk = "  " + chunk;
          }
          result.push_back(chunk);
          break;
        }

        size_t targetEnd = segStart + currentLimit;
        size_t breakPos = std::string::npos;
        for (size_t p = targetEnd; p > segStart + (currentLimit / 3); --p) {
          char c = segment[p - 1];
          if (c == ' ' || c == '/' || c == '\\' || c == '-' || c == '_') {
            breakPos = p;
            break;
          }
        }

        if (breakPos == std::string::npos) {
          breakPos = targetEnd;
        }

        std::string chunk = segment.substr(segStart, breakPos - segStart);
        if (!chunk.empty() && chunk.back() == ' ') {
          chunk.pop_back();
        }

        if (!isFirstSubLine) {
          chunk = "  " + chunk;
        }
        result.push_back(chunk);

        segStart = breakPos;
        while (segStart < segment.size() && segment[segStart] == ' ') {
          segStart++;
        }

        isFirstSubLine = false;
      }
    }

    if (newlinePos == std::string::npos)
      break;
    lineStart = newlinePos + 1;
  }

  return result;
}

void UIManager::openOperationLog(const std::string &title) {
  m_showLogDialog = true;
  m_operationInProgress = true;
  m_logDialogTitle = title;
  m_operationLogs.clear();
  m_logScrollOffset = 0;
  render();
  render();
}

void UIManager::addOperationLog(const std::string &message) {
  u32 color = CLR_TEXT_PRIMARY;
  if (message.rfind("[SUCCESS]", 0) == 0 || message.rfind("[OK]", 0) == 0) {
    color = CLR_GREEN;
  } else if (message.rfind("[ERROR]", 0) == 0 ||
             message.rfind("[FAIL]", 0) == 0) {
    color = CLR_DANGER;
  } else if (message.rfind("[Sync]", 0) == 0 ||
             message.rfind("[Download]", 0) == 0 ||
             message.rfind("[Upload]", 0) == 0) {
    color = CLR_ACCENT_SOFT;
  } else if (message.rfind("[WARN]", 0) == 0) {
    color = CLR_YELLOW;
  } else if (message.rfind("Existing", 0) == 0 ||
             message.rfind("Remote", 0) == 0 ||
             message.rfind("Local", 0) == 0 || message.rfind("ROM", 0) == 0) {
    color = CLR_TEXT_SECONDARY;
  }

  std::vector<std::string> wrapped = softWrapLogLine(message, 38);
  for (const auto &wLine : wrapped) {
    m_operationLogs.push_back({wLine, color});
  }
  m_logScrollOffset = 0;
  render();
}

void UIManager::finishOperationLog() {
  m_operationInProgress = false;
  addOperationLog("Operation finished. Press (A) or tap to dismiss.");
  render();
}

void UIManager::renderOperationLogOverlay() {
  C2D_DrawRectSolid(0.0f, 0.0f, 0.7f, RomM3DS::SCREEN_BOTTOM_WIDTH,
                    RomM3DS::SCREEN_BOTTOM_HEIGHT, CLR_DIM_OVERLAY);

  float boxX = 6.0f;
  float boxY = 6.0f;
  float boxW = RomM3DS::SCREEN_BOTTOM_WIDTH - 12.0f;
  float boxH = RomM3DS::SCREEN_BOTTOM_HEIGHT - 12.0f;

  C2D_DrawRectSolid(boxX, boxY, 0.72f, boxW, boxH, CLR_ACCENT_PRIMARY);
  C2D_DrawRectSolid(boxX + 2.0f, boxY + 2.0f, 0.72f, boxW - 4.0f, boxH - 4.0f,
                    CLR_SURFACE_HEADER);

  C2D_Text titleText;
  C2D_TextParse(&titleText, m_textBuf, m_logDialogTitle.c_str());
  C2D_TextOptimize(&titleText);
  C2D_DrawText(&titleText, C2D_WithColor, boxX + 8.0f, boxY + 6.0f, 0.75f,
               0.46f, 0.46f, CLR_TEXT_PRIMARY);

  float logX = boxX + 6.0f;
  float logY = boxY + 24.0f;
  float logW = boxW - 12.0f;
  float logH = 168.0f;
  C2D_DrawRectSolid(logX, logY, 0.75f, logW, logH, CLR_BORDER_SUBTLE);
  C2D_DrawRectSolid(logX + 1.0f, logY + 1.0f, 0.75f, logW - 2.0f, logH - 2.0f,
                    CLR_SURFACE_ALT);

  constexpr float LINE_HEIGHT = 13.5f;
  int maxVisibleLines = static_cast<int>((logH - 6.0f) / LINE_HEIGHT);
  int totalLines = static_cast<int>(m_operationLogs.size());

  int startLine = totalLines - maxVisibleLines - m_logScrollOffset;
  if (startLine < 0)
    startLine = 0;
  int endLine = startLine + maxVisibleLines;
  if (endLine > totalLines)
    endLine = totalLines;

  if (totalLines > maxVisibleLines) {
    std::string scrollInfo = "^ v Scroll";
    C2D_Text scText;
    C2D_TextParse(&scText, m_textBuf, scrollInfo.c_str());
    C2D_TextOptimize(&scText);
    float scW = 0.0f, scH = 0.0f;
    C2D_TextGetDimensions(&scText, 0.35f, 0.35f, &scW, &scH);
    C2D_DrawText(&scText, C2D_WithColor, boxX + boxW - scW - 8.0f, boxY + 8.0f,
                 0.75f, 0.35f, 0.35f, CLR_TEXT_MUTED);
  }

  float textY = logY + 4.0f;
  constexpr float LOG_FONT_SCALE = 0.38f;
  for (int i = startLine; i < endLine; ++i) {
    const auto &entry = m_operationLogs[i];

    C2D_Text lText;
    C2D_TextParse(&lText, m_textBuf, entry.text.c_str());
    C2D_TextOptimize(&lText);
    C2D_DrawText(&lText, C2D_WithColor, logX + 5.0f, textY, 0.8f,
                 LOG_FONT_SCALE, LOG_FONT_SCALE, entry.color);
    textY += LINE_HEIGHT;
  }

  float actionY = boxY + 196.0f;
  if (m_operationInProgress) {
    C2D_Text waitText;
    C2D_TextParse(&waitText, m_textBuf, "Operation running... please wait");
    C2D_TextOptimize(&waitText);
    C2D_DrawText(&waitText, C2D_WithColor, boxX + 16.0f, actionY + 6.0f, 0.75f,
                 0.42f, 0.42f, CLR_TEXT_MUTED);
  } else {
    drawButton(boxX + 24.0f, actionY + 2.0f, boxW - 48.0f, 24.0f,
               "[ (A) Close / Dismiss Log ]", true);
  }
}

void UIManager::renderFirstLaunchTop() {
  bool canCancel = (m_configMgr && m_configMgr->hasValidConfig());

  // Top Header Bar
  C2D_DrawRectSolid(0.0f, 0.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 32.0f,
                    CLR_SURFACE_HEADER);
  C2D_DrawRectSolid(0.0f, 30.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 2.0f,
                    CLR_ACCENT_PRIMARY);

  C2D_Text headerText;
  C2D_TextParse(&headerText, m_textBuf,
                canCancel ? "romm3ds - Server Settings"
                          : "romm3ds - Initial Setup");
  C2D_TextOptimize(&headerText);
  C2D_DrawText(&headerText, C2D_WithColor, 12.0f, 7.0f, 0.5f, 0.58f, 0.58f,
               CLR_TEXT_PRIMARY);

  std::string verTag = std::string("v") + RomM3DS::APP_VERSION;
  drawPillBadge(334.0f, 7.0f, verTag.c_str(), CLR_SURFACE_ELEVATED,
                CLR_ACCENT_SOFT, 0.38f);

  // Main Inspector Card
  C2D_DrawRectSolid(12.0f, 40.0f, 0.5f, 376.0f, 168.0f, CLR_BORDER_SUBTLE);
  C2D_DrawRectSolid(13.0f, 41.0f, 0.5f, 374.0f, 166.0f, CLR_SURFACE_CARD);
  C2D_DrawRectSolid(12.0f, 40.0f, 0.5f, 4.0f, 168.0f, CLR_ACCENT_PRIMARY);

  C2D_Text cardTitle;
  C2D_TextParse(&cardTitle, m_textBuf,
                canCancel ? "Update RomM Server Connection"
                          : "Connect to Your Self-Hosted RomM Instance");
  C2D_TextOptimize(&cardTitle);
  C2D_DrawText(&cardTitle, C2D_WithColor, 26.0f, 48.0f, 0.5f, 0.50f, 0.50f,
               CLR_ACCENT_SOFT);

  C2D_DrawRectSolid(26.0f, 70.0f, 0.5f, 348.0f, 1.0f, CLR_BORDER_SUBTLE);

  C2D_Text descText;
  C2D_TextParse(&descText, m_textBuf,
                "- Server Endpoint: http(s)://your-romm-server:port\n"
                "- API Key / Token: Generate in RomM Profile -> API Tokens\n"
                "- Required Scopes: roms.read, platforms.read,\n"
                "  saves.read, and saves.write\n\n"
                "Your API Key is encrypted on disk using your 3DS hardware\n"
                "console ID (AES-256-GCM) after saving.");
  C2D_TextOptimize(&descText);
  C2D_DrawText(&descText, C2D_WithColor, 26.0f, 78.0f, 0.5f, 0.41f, 0.41f,
               CLR_TEXT_SECONDARY);

  // Footer HUD Bar
  C2D_DrawRectSolid(0.0f, 216.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 24.0f,
                    CLR_SURFACE_HEADER);
  C2D_DrawRectSolid(0.0f, 216.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 1.0f,
                    CLR_BORDER_SUBTLE);

  C2D_Text promptText;
  C2D_TextParse(&promptText, m_textBuf,
                canCancel
                    ? "(Up/Down) Navigate   (A) Edit / Save   (B) Cancel   (START) Exit"
                    : "(Up/Down) Navigate   (A) Edit / Save   (START) Exit");
  C2D_TextOptimize(&promptText);
  C2D_DrawText(&promptText, C2D_WithColor, 12.0f, 221.0f, 0.5f, 0.40f, 0.40f,
               CLR_TEXT_SECONDARY);
}

void UIManager::renderFirstLaunchBottom() {
  bool canCancel = (m_configMgr && m_configMgr->hasValidConfig());

  C2D_DrawRectSolid(8.0f, 6.0f, 0.5f, RomM3DS::SCREEN_BOTTOM_WIDTH - 16.0f,
                    RomM3DS::SCREEN_BOTTOM_HEIGHT - 12.0f, CLR_CARD);

  drawInputBox(16.0f, 44.0f, 288.0f, 32.0f,
               "RomM Server Endpoint:", m_enteredEndpoint,
               m_setupSelectedIndex == 0);

  std::string maskedKey =
      m_enteredApiKey.empty() ? "" : std::string(m_enteredApiKey.length(), '*');
  drawInputBox(16.0f, 96.0f, 288.0f, 32.0f, "API Key / Credentials:", maskedKey,
               m_setupSelectedIndex == 1);

  if (canCancel) {
    drawButton(16.0f, 148.0f, 186.0f, 34.0f, "[ Save Configuration ]",
               m_setupSelectedIndex == 2);
    drawButton(210.0f, 148.0f, 94.0f, 34.0f, "[ Cancel (B) ]",
               m_setupSelectedIndex == 3);
  } else {
    drawButton(16.0f, 148.0f, 288.0f, 34.0f, "[ Save Configuration ]",
               m_setupSelectedIndex == 2);
  }

  u32 statusColor = (m_statusMessage.find("Error") != std::string::npos ||
                     m_statusMessage.find("Failed") != std::string::npos)
                        ? CLR_ERROR
                        : CLR_GREEN;
  C2D_Text statusText;
  C2D_TextParse(&statusText, m_textBuf, m_statusMessage.c_str());
  C2D_TextOptimize(&statusText);
  C2D_DrawText(&statusText, C2D_WithColor, 16.0f, 198.0f, 0.5f, 0.44f, 0.44f,
               statusColor);
}

void UIManager::renderMainMenuTop() {
  // Top Header Bar (32px)
  C2D_DrawRectSolid(0.0f, 0.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 32.0f,
                    CLR_SURFACE_HEADER);
  C2D_DrawRectSolid(0.0f, 30.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 2.0f,
                    CLR_ACCENT_PRIMARY);

  std::string headerTitle =
      std::string("romm3ds v") + RomM3DS::APP_VERSION + "  •  Inspector";
  C2D_Text headerText;
  C2D_TextParse(&headerText, m_textBuf, headerTitle.c_str());
  C2D_TextOptimize(&headerText);
  C2D_DrawText(&headerText, C2D_WithColor, 12.0f, 7.0f, 0.5f, 0.56f, 0.56f,
               CLR_TEXT_PRIMARY);

  // Active Filter Pill on Top-Right Header
  std::string filterBadge = "Filter: " + getActiveFilterButtonLabel();
  C2D_Text fbMeasure;
  C2D_TextParse(&fbMeasure, m_textBuf, filterBadge.c_str());
  C2D_TextOptimize(&fbMeasure);
  float fbW = 0.0f, fbH = 0.0f;
  C2D_TextGetDimensions(&fbMeasure, 0.37f, 0.37f, &fbW, &fbH);
  float pillX = RomM3DS::SCREEN_TOP_WIDTH - (fbW + 12.0f) - 10.0f;
  drawPillBadge(pillX, 6.0f, filterBadge.c_str(), CLR_SURFACE_ELEVATED,
                (m_localFilterMode == LocalFilterMode::HIDDEN) ? CLR_YELLOW
                                                               : CLR_ACCENT_SOFT,
                0.37f);

  if (m_syncMgr && !m_filteredLocalIndices.empty() &&
      m_selectedGameIndex < m_filteredLocalIndices.size()) {
    size_t actualIdx = m_filteredLocalIndices[m_selectedGameIndex];
    const auto &item = m_syncMgr->getItems()[actualIdx];

    updateGameLogo(item.localTitle);

    time_t localTime = item.localTitle.saveTime;
    time_t remoteTime =
        item.remoteSaveTime != 0
            ? item.remoteSaveTime
            : SyncManager::parseIsoTimestamp(item.remoteSaveUpdatedAt);
    bool remoteIsNewer =
        (remoteTime > 0 && (localTime == 0 || remoteTime > localTime + 5));
    bool localIsNewer =
        (localTime > 0 && (remoteTime == 0 || localTime > remoteTime + 5));

    // =========================================================================
    // LEFT COLUMN (x = 10..106, w = 96px, h = 168px): Icon & Status Pills
    // =========================================================================
    constexpr float leftX = 10.0f;
    constexpr float leftY = 40.0f;
    constexpr float leftW = 96.0f;
    constexpr float leftH = 168.0f;

    u32 leftAccent = item.isIgnored
                         ? CLR_TEXT_DIM
                         : (item.isMatched ? CLR_GREEN : CLR_YELLOW);
    C2D_DrawRectSolid(leftX, leftY, 0.5f, leftW, leftH, CLR_BORDER_SUBTLE);
    C2D_DrawRectSolid(leftX + 1.0f, leftY + 1.0f, 0.5f, leftW - 2.0f,
                      leftH - 2.0f, CLR_SURFACE_CARD);
    C2D_DrawRectSolid(leftX, leftY, 0.5f, leftW, 3.0f, leftAccent);

    // 60x60 Elevated Icon Frame centered in Left Column
    constexpr float iconBoxW = 60.0f;
    constexpr float iconBoxH = 60.0f;
    constexpr float iconBoxX = leftX + (leftW - iconBoxW) / 2.0f;
    constexpr float iconBoxY = leftY + 10.0f;
    C2D_DrawRectSolid(iconBoxX, iconBoxY, 0.5f, iconBoxW, iconBoxH,
                      CLR_BORDER_SUBTLE);
    C2D_DrawRectSolid(iconBoxX + 1.0f, iconBoxY + 1.0f, 0.5f, iconBoxW - 2.0f,
                      iconBoxH - 2.0f, CLR_SURFACE_ALT);

    if (m_hasLogo) {
      C2D_DrawImageAt(m_logoImage, iconBoxX + 6.0f, iconBoxY + 6.0f, 0.5f);
    } else {
      std::string consoleFallback =
          item.localTitle.console.empty() ? "ROM" : item.localTitle.console;
      C2D_Text cTxt;
      C2D_TextParse(&cTxt, m_textBuf, consoleFallback.c_str());
      C2D_TextOptimize(&cTxt);
      float cScale = 0.52f;
      float cW = 0.0f, cH = 0.0f;
      C2D_TextGetDimensions(&cTxt, cScale, cScale, &cW, &cH);
      if (cW > iconBoxW - 8.0f && cW > 0.0f) {
        cScale *= (iconBoxW - 8.0f) / cW;
        C2D_TextGetDimensions(&cTxt, cScale, cScale, &cW, &cH);
      }
      C2D_DrawText(&cTxt, C2D_WithColor, iconBoxX + (iconBoxW - cW) / 2.0f,
                   iconBoxY + (iconBoxH - cH) / 2.0f, 0.5f, cScale, cScale,
                   CLR_ACCENT_SOFT);
    }

    // Helper to center a pill badge horizontally in the left column
    auto drawCenteredLeftPill = [&](float py, const char *label, u32 bg,
                                    u32 fg) {
      C2D_Text mTxt;
      C2D_TextParse(&mTxt, m_textBuf, label);
      C2D_TextOptimize(&mTxt);
      float tw = 0.0f, th = 0.0f;
      C2D_TextGetDimensions(&mTxt, 0.36f, 0.36f, &tw, &th);
      float pw = tw + 12.0f;
      float px = leftX + (leftW - pw) / 2.0f;
      drawPillBadge(px, py, label, bg, fg, 0.36f);
    };

    // Pill 1: Console Badge
    std::string consoleBadge =
        item.localTitle.console.empty() ? "ROM" : item.localTitle.console;
    drawCenteredLeftPill(leftY + 78.0f, consoleBadge.c_str(),
                         CLR_SURFACE_ELEVATED, CLR_ACCENT_SOFT);

    // Pill 2: Match / Ignored Badge
    if (item.isIgnored) {
      drawCenteredLeftPill(leftY + 102.0f, "HIDDEN", CLR_SURFACE_ALT,
                           CLR_TEXT_SECONDARY);
    } else if (item.isMatched) {
      drawCenteredLeftPill(leftY + 102.0f, "MATCHED", CLR_SURFACE_ALT,
                           CLR_GREEN);
    } else {
      drawCenteredLeftPill(leftY + 102.0f, "UNMATCHED", CLR_SURFACE_ALT,
                           CLR_YELLOW);
    }

    // Pill 3: Sync / Save Status Badge
    const char *syncBadge = "NO SAVES";
    u32 syncFg = CLR_TEXT_DIM;
    if (item.localTitle.hasSave && item.remoteHasSave) {
      if (remoteIsNewer) {
        syncBadge = "OUTDATED [v]";
        syncFg = CLR_GREEN;
      } else if (localIsNewer) {
        syncBadge = "UPDATED [^]";
        syncFg = CLR_YELLOW;
      } else {
        syncBadge = "IN SYNC [=]";
        syncFg = CLR_TEXT_SECONDARY;
      }
    } else if (item.localTitle.hasSave) {
      syncBadge = "LOCAL SAVE";
      syncFg = CLR_YELLOW;
    } else if (item.remoteHasSave) {
      syncBadge = "CLOUD SAVE";
      syncFg = CLR_GREEN;
    }
    drawCenteredLeftPill(leftY + 126.0f, syncBadge, CLR_SURFACE_ALT, syncFg);

    // Storage type caption at bottom of Left Column
    const char *storageLbl =
        item.localTitle.isFileRom
            ? "SD Card ROM"
            : (item.localTitle.console == "GBA" ? "NAND VC" : "3DS Title");
    C2D_Text stTypeTxt;
    C2D_TextParse(&stTypeTxt, m_textBuf, storageLbl);
    C2D_TextOptimize(&stTypeTxt);
    float stW = 0.0f, stH = 0.0f;
    C2D_TextGetDimensions(&stTypeTxt, 0.34f, 0.34f, &stW, &stH);
    C2D_DrawText(&stTypeTxt, C2D_WithColor, leftX + (leftW - stW) / 2.0f,
                 leftY + 150.0f, 0.5f, 0.34f, 0.34f, CLR_TEXT_DIM);

    // =========================================================================
    // RIGHT COLUMN (x = 112..390, w = 278px, h = 168px): Structured Inspector
    // =========================================================================
    constexpr float rightX = 112.0f;
    constexpr float rightY = 40.0f;
    constexpr float rightW = 278.0f;
    constexpr float rightH = 168.0f;
    constexpr float contentX = rightX + 10.0f;
    constexpr float maxContentW = rightW - 18.0f;

    C2D_DrawRectSolid(rightX, rightY, 0.5f, rightW, rightH, CLR_BORDER_SUBTLE);
    C2D_DrawRectSolid(rightX + 1.0f, rightY + 1.0f, 0.5f, rightW - 2.0f,
                      rightH - 2.0f, CLR_SURFACE_CARD);
    C2D_DrawRectSolid(rightX, rightY, 0.5f, 3.0f, rightH, CLR_ACCENT_PRIMARY);

    // Game Title
    C2D_Text nameText;
    C2D_TextParse(&nameText, m_textBuf, item.localTitle.name.c_str());
    C2D_TextOptimize(&nameText);
    u32 nameColor = item.isIgnored ? CLR_TEXT_SECONDARY : CLR_TEXT_PRIMARY;
    float nameScale = 0.54f;
    float nameW = 0.0f, nameH = 0.0f;
    C2D_TextGetDimensions(&nameText, nameScale, nameScale, &nameW, &nameH);
    if (nameW > maxContentW && nameW > 0.0f) {
      nameScale *= (maxContentW / nameW);
    }
    C2D_DrawText(&nameText, C2D_WithColor, contentX, rightY + 6.0f, 0.5f,
                 nameScale, nameScale, nameColor);

    // Subtitle: Title ID / Product Code or full ROM path
    std::string subLine;
    if (item.localTitle.isFileRom) {
      subLine = "Path: " + item.localTitle.romPath;
    } else {
      subLine = "ID: " + item.localTitle.titleIdHex;
      if (!item.localTitle.productCode.empty()) {
        subLine += "  •  " + item.localTitle.productCode;
      }
    }
    C2D_Text subText;
    C2D_TextParse(&subText, m_textBuf, subLine.c_str());
    C2D_TextOptimize(&subText);
    float subScale = 0.38f;
    float subW = 0.0f, subH = 0.0f;
    C2D_TextGetDimensions(&subText, subScale, subScale, &subW, &subH);
    if (subW > maxContentW && subW > 0.0f) {
      float fitScale = subScale * (maxContentW / subW);
      if (fitScale >= 0.30f) {
        subScale = fitScale;
      } else {
        subScale = 0.30f;
        std::string full = subLine;
        while (full.length() > 20) {
          full = full.substr(0, full.length() - 4) + "..";
          C2D_Text t;
          C2D_TextParse(&t, m_textBuf, full.c_str());
          C2D_TextOptimize(&t);
          float curW = 0.0f, curH = 0.0f;
          C2D_TextGetDimensions(&t, subScale, subScale, &curW, &curH);
          if (curW <= maxContentW) {
            subLine = full;
            C2D_TextParse(&subText, m_textBuf, subLine.c_str());
            C2D_TextOptimize(&subText);
            break;
          }
        }
      }
    }
    C2D_DrawText(&subText, C2D_WithColor, contentX, rightY + 26.0f, 0.5f,
                 subScale, subScale, CLR_TEXT_SECONDARY);

    // Divider line
    C2D_DrawRectSolid(contentX, rightY + 43.0f, 0.5f, maxContentW, 1.0f,
                      CLR_BORDER_SUBTLE);

    // Helper for key-value metadata rows
    auto drawMetaRow = [&](float rowY, const char *key, const std::string &val,
                           u32 valColor) {
      C2D_Text kTxt;
      C2D_TextParse(&kTxt, m_textBuf, key);
      C2D_TextOptimize(&kTxt);
      C2D_DrawText(&kTxt, C2D_WithColor, contentX, rowY, 0.5f, 0.39f, 0.39f,
                   CLR_TEXT_DIM);

      constexpr float valOffset = 68.0f;
      constexpr float maxValW = maxContentW - valOffset;
      C2D_Text vTxt;
      C2D_TextParse(&vTxt, m_textBuf, val.c_str());
      C2D_TextOptimize(&vTxt);
      float vScale = 0.40f;
      float vW = 0.0f, vH = 0.0f;
      C2D_TextGetDimensions(&vTxt, vScale, vScale, &vW, &vH);
      if (vW > maxValW && vW > 0.0f) {
        vScale *= (maxValW / vW);
      }
      C2D_DrawText(&vTxt, C2D_WithColor, contentX + valOffset, rowY, 0.5f,
                   vScale, vScale, valColor);
    };

    // Row 1: RomM Match
    std::string matchVal =
        item.isIgnored
            ? "Excluded from sync (Hidden)"
            : (item.isMatched ? item.remoteTitle
                              : "Unmatched (Press SELECT or [Match])");
    u32 matchColor = item.isIgnored
                         ? CLR_TEXT_SECONDARY
                         : (item.isMatched ? CLR_GREEN : CLR_YELLOW);
    drawMetaRow(rightY + 50.0f, "RomM Game:", matchVal, matchColor);

    // Row 2: Local Save Timestamp
    std::string localVal =
        item.localTitle.hasSave
            ? SyncManager::formatTimestamp(item.localTitle.saveTime)
            : "No local save found";
    if (item.localTitle.hasSave && item.remoteHasSave && localIsNewer) {
      localVal += "  [NEWER]";
    }
    u32 localColor =
        !item.localTitle.hasSave
            ? CLR_TEXT_DIM
            : (localIsNewer ? CLR_YELLOW : CLR_TEXT_PRIMARY);
    drawMetaRow(rightY + 72.0f, "Local Save:", localVal, localColor);

    // Row 3: Remote Save Timestamp
    std::string remoteVal =
        item.remoteHasSave
            ? SyncManager::formatIsoTimestamp(item.remoteSaveUpdatedAt)
            : "No remote save on RomM";
    if (item.localTitle.hasSave && item.remoteHasSave && remoteIsNewer) {
      remoteVal += "  [NEWER]";
    }
    u32 remoteColor =
        !item.remoteHasSave
            ? CLR_TEXT_DIM
            : (remoteIsNewer ? CLR_GREEN : CLR_TEXT_PRIMARY);
    drawMetaRow(rightY + 94.0f, "Cloud Save:", remoteVal, remoteColor);

    // Divider before Save Path
    C2D_DrawRectSolid(contentX, rightY + 116.0f, 0.5f, maxContentW, 1.0f,
                      CLR_BORDER_SUBTLE);

    // Row 4: Save Path
    C2D_Text pathLbl;
    C2D_TextParse(&pathLbl, m_textBuf, "Active Local Save Path:");
    C2D_TextOptimize(&pathLbl);
    C2D_DrawText(&pathLbl, C2D_WithColor, contentX, rightY + 122.0f, 0.5f,
                 0.36f, 0.36f, CLR_TEXT_DIM);

    std::string savePathStr = item.localTitle.savePath.empty()
                                  ? "(Auto-detect default location)"
                                  : item.localTitle.savePath;
    C2D_Text pathVal;
    C2D_TextParse(&pathVal, m_textBuf, savePathStr.c_str());
    C2D_TextOptimize(&pathVal);
    float pScale = 0.38f;
    float pW = 0.0f, pH = 0.0f;
    C2D_TextGetDimensions(&pathVal, pScale, pScale, &pW, &pH);
    if (pW > maxContentW && pW > 0.0f) {
      pScale *= (maxContentW / pW);
    }
    C2D_DrawText(&pathVal, C2D_WithColor, contentX, rightY + 138.0f, 0.5f,
                 pScale, pScale, CLR_TEXT_SECONDARY);
  } else {
    // Empty filter state card
    C2D_DrawRectSolid(12.0f, 40.0f, 0.5f, 376.0f, 168.0f, CLR_BORDER_SUBTLE);
    C2D_DrawRectSolid(13.0f, 41.0f, 0.5f, 374.0f, 166.0f, CLR_SURFACE_CARD);
    C2D_DrawRectSolid(12.0f, 40.0f, 0.5f, 4.0f, 168.0f, CLR_YELLOW);

    C2D_Text emptyTitle;
    C2D_TextParse(&emptyTitle, m_textBuf,
                  "No Installed Titles Match Current Filter");
    C2D_TextOptimize(&emptyTitle);
    C2D_DrawText(&emptyTitle, C2D_WithColor, 26.0f, 50.0f, 0.5f, 0.50f, 0.50f,
                 CLR_YELLOW);

    C2D_DrawRectSolid(26.0f, 72.0f, 0.5f, 348.0f, 1.0f, CLR_BORDER_SUBTLE);

    std::string msg = "- Active Filter: " + getActiveFilterDescription() +
                      "\n- Active Sort:   " +
                      std::string(getSortModeName(m_localSortMode)) + "\n";
    if (!m_localFilterText.empty()) {
      msg += "- Search Query:  \"" + m_localFilterText + "\"\n";
    }
    msg += "\nTap [Clr], [Sort], [Filter], or [ Menu ] on the bottom screen.";

    C2D_Text emptyText;
    C2D_TextParse(&emptyText, m_textBuf, msg.c_str());
    C2D_TextOptimize(&emptyText);
    C2D_DrawText(&emptyText, C2D_WithColor, 26.0f, 82.0f, 0.5f, 0.43f, 0.43f,
                 CLR_TEXT_SECONDARY);
  }

  // Footer HUD Bar (24px)
  C2D_DrawRectSolid(0.0f, 216.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 24.0f,
                    CLR_SURFACE_HEADER);
  C2D_DrawRectSolid(0.0f, 216.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 1.0f,
                    CLR_BORDER_SUBTLE);

  C2D_Text hudText;
  C2D_TextParse(
      &hudText, m_textBuf,
      "(A) Sync  (SEL) Actions  (X) Menu  (L/R) Up/Down  (Y) Search");
  C2D_TextOptimize(&hudText);
  C2D_DrawText(&hudText, C2D_WithColor, 10.0f, 221.0f, 0.5f, 0.38f, 0.38f,
               CLR_TEXT_SECONDARY);
}

void UIManager::renderMainMenuBottom() {
  // 1. Physics update
  if (!m_isDragging) {
    if (std::abs(m_velocityY) > 0.15f) {
      float maxScroll = std::max(
          0.0f, static_cast<float>(m_filteredLocalIndices.size()) * 32.0f - 160.0f);
      m_targetScrollY =
          std::clamp(m_targetScrollY + m_velocityY, 0.0f, maxScroll);
      m_scrollY = m_targetScrollY;
      m_velocityY *= 0.88f;
    } else {
      m_velocityY = 0.0f;
      if (std::abs(m_targetScrollY - m_scrollY) > 0.1f) {
        m_scrollY += (m_targetScrollY - m_scrollY) * 0.35f;
      } else {
        m_scrollY = m_targetScrollY;
      }
    }
  }

  if (m_activeSheet != ActiveSheet::NONE || m_sheetCurrentY < 239.5f) {
    m_sheetCurrentY += (m_sheetTargetY - m_sheetCurrentY) * 0.35f;
    if (m_sheetTargetY >= 239.0f && m_sheetCurrentY >= 238.5f) {
      m_activeSheet = ActiveSheet::NONE;
      m_sheetCurrentY = 240.0f;
    }
  }

  // 2. Base background
  C2D_DrawRectSolid(0.0f, 0.0f, 0.2f, RomM3DS::SCREEN_BOTTOM_WIDTH,
                    RomM3DS::SCREEN_BOTTOM_HEIGHT, CLR_BG_BOTTOM);

  size_t total = m_filteredLocalIndices.size();

  // 3. Render 5-row game list (z = 0.40f)
  if (total == 0) {
    C2D_DrawRectSolid(16.0f, 60.0f, 0.40f, 288.0f, 110.0f, CLR_BORDER_SUBTLE);
    C2D_DrawRectSolid(17.0f, 61.0f, 0.40f, 286.0f, 108.0f, CLR_SURFACE_CARD);
    C2D_DrawRectSolid(16.0f, 60.0f, 0.41f, 4.0f, 110.0f, CLR_YELLOW);

    C2D_Text emptyTitle;
    C2D_TextParse(&emptyTitle, m_textBuf, "No Matching Titles");
    C2D_TextOptimize(&emptyTitle);
    C2D_DrawText(&emptyTitle, C2D_WithColor, 28.0f, 72.0f, 0.42f, 0.46f, 0.46f,
                 CLR_YELLOW);

    C2D_Text emptyHint;
    C2D_TextParse(&emptyHint, m_textBuf,
                  "No installed games match the active filter or search query.\n"
                  "Tap [Filter] or [Search] to adjust your selection.");
    C2D_TextOptimize(&emptyHint);
    C2D_DrawText(&emptyHint, C2D_WithColor, 28.0f, 96.0f, 0.42f, 0.38f, 0.38f,
                 CLR_TEXT_SECONDARY);
  } else {
    int firstIdx = std::max(0, static_cast<int>(m_scrollY / 32.0f) - 1);
    int lastIdx =
        std::min(static_cast<int>(total) - 1,
                 static_cast<int>((m_scrollY + 160.0f) / 32.0f) + 1);

    for (int i = firstIdx; i <= lastIdx; ++i) {
      float rowY = 38.0f + static_cast<float>(i) * 32.0f - m_scrollY;
      if (rowY + 32.0f < 38.0f || rowY > 198.0f)
        continue;

      size_t actualIdx = m_filteredLocalIndices[i];
      const auto &item = m_syncMgr->getItems()[actualIdx];
      bool selected = (static_cast<size_t>(i) == m_selectedGameIndex);

      u32 rowBg = selected ? CLR_SURFACE_ELEVATED
                           : ((i % 2 == 0) ? CLR_SURFACE_CARD : CLR_SURFACE_ALT);
      u32 rowBorder = selected ? CLR_ACCENT_PRIMARY : CLR_BORDER_SUBTLE;

      // Row background & border
      C2D_DrawRectSolid(8.0f, rowY + 1.0f, 0.40f, 304.0f, 30.0f, rowBorder);
      C2D_DrawRectSolid(9.0f, rowY + 2.0f, 0.40f, 302.0f, 28.0f, rowBg);

      // Selected left purple indicator bar
      if (selected) {
        C2D_DrawRectSolid(8.0f, rowY + 1.0f, 0.42f, 3.5f, 30.0f,
                          CLR_ACCENT_PRIMARY);
      }

      // Status indicator dot (preserved green / yellow / gray per user rule)
      u32 dotColor =
          item.isIgnored ? CLR_TEXT_SECONDARY
                         : (item.isMatched ? CLR_GREEN : CLR_YELLOW);
      C2D_DrawRectSolid(16.0f, rowY + 13.0f, 0.43f, 6.0f, 6.0f, dotColor);

      // Console tag pill badge
      float consoleBadgeW = drawPillBadge(26.0f, rowY + 6.5f,
                                          item.localTitle.console.c_str(),
                                          CLR_SURFACE_HEADER,
                                          CLR_TEXT_SECONDARY, 0.35f, 0.43f);

      // Right-aligned status pills
      float pillRightX = 304.0f;
      if (item.isIgnored) {
        pillRightX -= 36.0f;
        drawPillBadge(pillRightX, rowY + 6.5f, "Hide", CLR_SURFACE_HEADER,
                      CLR_TEXT_DIM, 0.34f, 0.43f);
      }
      if (item.localTitle.hasSave && item.remoteHasSave) {
        time_t lt = item.localTitle.saveTime;
        time_t rt =
            item.remoteSaveTime != 0
                ? item.remoteSaveTime
                : SyncManager::parseIsoTimestamp(item.remoteSaveUpdatedAt);
        if (rt > lt + 5 && lt > 0) {
          pillRightX -= 22.0f;
          drawPillBadge(pillRightX, rowY + 6.5f, "v", CLR_ACCENT_SOFT,
                        CLR_GREEN, 0.34f, 0.43f);
        } else if (lt > rt + 5 && rt > 0) {
          pillRightX -= 22.0f;
          drawPillBadge(pillRightX, rowY + 6.5f, "^", CLR_ACCENT_SOFT,
                        CLR_YELLOW, 0.34f, 0.43f);
        } else {
          pillRightX -= 22.0f;
          drawPillBadge(pillRightX, rowY + 6.5f, "=", CLR_SURFACE_HEADER,
                        CLR_TEXT_DIM, 0.34f, 0.43f);
        }
      }
      if (item.localTitle.hasSave) {
        pillRightX -= 18.0f;
        drawPillBadge(pillRightX, rowY + 6.5f, "S", CLR_SURFACE_HEADER,
                      CLR_TEXT_SECONDARY, 0.34f, 0.43f);
      }

      // Title text (positioned between console badge and right pills)
      float titleStartX = 26.0f + consoleBadgeW + 6.0f;
      float titleMaxW = std::max(60.0f, pillRightX - titleStartX - 6.0f);
      float titleScale = 0.40f;

      std::string dispName = item.localTitle.name;
      C2D_Text titleText;
      C2D_TextParse(&titleText, m_textBuf, dispName.c_str());
      C2D_TextOptimize(&titleText);

      float textW = 0.0f, textH = 0.0f;
      C2D_TextGetDimensions(&titleText, titleScale, titleScale, &textW, &textH);
      if (textW > titleMaxW && !dispName.empty()) {
        int low = 0;
        int high = static_cast<int>(dispName.length());
        int bestLen = 0;
        while (low <= high) {
          int mid = low + (high - low) / 2;
          std::string testStr = dispName.substr(0, mid) + "..";
          C2D_Text testText;
          C2D_TextParse(&testText, m_textBuf, testStr.c_str());
          C2D_TextOptimize(&testText);
          float curW = 0.0f, curH = 0.0f;
          C2D_TextGetDimensions(&testText, titleScale, titleScale, &curW, &curH);
          if (curW <= titleMaxW) {
            bestLen = mid;
            low = mid + 1;
          } else {
            high = mid - 1;
          }
        }
        dispName = dispName.substr(0, bestLen) + "..";
        C2D_TextParse(&titleText, m_textBuf, dispName.c_str());
        C2D_TextOptimize(&titleText);
      }

      u32 titleColor = selected ? CLR_TEXT_PRIMARY : CLR_TEXT_SECONDARY;
      C2D_DrawText(&titleText, C2D_WithColor, titleStartX, rowY + 8.5f, 0.43f,
                   titleScale, titleScale, titleColor);
    }
  }

  // 4. Scrollbar (z = 0.56f)
  if (total > 5) {
    float maxScroll =
        std::max(0.0f, static_cast<float>(total) * 32.0f - 160.0f);
    C2D_DrawRectSolid(315.0f, 40.0f, 0.56f, 2.0f, 156.0f, CLR_BORDER_SUBTLE);
    float thumbH =
        std::max(16.0f, 156.0f * (160.0f / (static_cast<float>(total) * 32.0f)));
    float thumbY =
        40.0f + (156.0f - thumbH) * (m_scrollY / maxScroll);
    C2D_DrawRectSolid(314.0f, thumbY, 0.58f, 3.0f, thumbH, CLR_ACCENT_PRIMARY);
  }

  // 5. Top Control Bar (z = 0.50f..0.55f)
  C2D_DrawRectSolid(0.0f, 0.0f, 0.50f, RomM3DS::SCREEN_BOTTOM_WIDTH, 38.0f,
                    CLR_BG_BOTTOM);
  C2D_DrawRectSolid(0.0f, 37.0f, 0.52f, RomM3DS::SCREEN_BOTTOM_WIDTH, 1.0f,
                    CLR_BORDER_SUBTLE);

  std::string sortLabel = "Sort: " + std::string(getSortModeName(m_localSortMode));
  drawButton(8.0f, 4.0f, 92.0f, 30.0f, sortLabel.c_str(), false, 0, 0.55f);

  std::string modeLabel = getActiveFilterButtonLabel();
  u32 filterAccent = (m_localFilterMode == LocalFilterMode::ALL) ? 0 : CLR_ACCENT_PRIMARY;
  drawButton(104.0f, 4.0f, 114.0f, 30.0f, modeLabel.c_str(), false, filterAccent, 0.55f);

  if (m_localFilterText.empty()) {
    drawButton(222.0f, 4.0f, 90.0f, 30.0f, "[ Search (Y) ]", false, 0, 0.55f);
  } else {
    std::string searchDisplay = "\"" + m_localFilterText + "\"";
    drawButton(222.0f, 4.0f, 62.0f, 30.0f, searchDisplay.c_str(), false, CLR_ACCENT_PRIMARY, 0.55f);
    drawButton(286.0f, 4.0f, 26.0f, 30.0f, "[X]", false, 0, 0.55f);
  }

  // 6. Bottom Deck (z = 0.50f..0.55f)
  C2D_DrawRectSolid(0.0f, 198.0f, 0.50f, RomM3DS::SCREEN_BOTTOM_WIDTH, 42.0f,
                    CLR_BG_BOTTOM);
  C2D_DrawRectSolid(0.0f, 198.0f, 0.52f, RomM3DS::SCREEN_BOTTOM_WIDTH, 1.0f,
                    CLR_BORDER_SUBTLE);

  bool canSync = false;
  if (m_syncMgr && !m_filteredLocalIndices.empty() &&
      m_selectedGameIndex < m_filteredLocalIndices.size()) {
    size_t actualIdx = m_filteredLocalIndices[m_selectedGameIndex];
    const auto &item = m_syncMgr->getItems()[actualIdx];
    canSync = !item.isIgnored && item.isMatched &&
              (item.localTitle.hasSave || item.remoteHasSave);
  }

  drawButton(8.0f, 203.0f, 96.0f, 32.0f, "[ Sync (A) ]", false, CLR_ACCENT_PRIMARY, 0.55f, !canSync);
  drawButton(108.0f, 203.0f, 104.0f, 32.0f, "[ Actions (SEL) ]", false, 0, 0.55f);
  drawButton(216.0f, 203.0f, 96.0f, 32.0f, "[ Menu (X) ]", false, 0, 0.55f);

  // 7. Bottom Sheet Overlay (z = 0.75f..0.85f)
  if (m_activeSheet != ActiveSheet::NONE || m_sheetCurrentY < 239.0f) {
    renderBottomSheet();
  }
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
  C2D_DrawRectSolid(0.0f, 0.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 32.0f,
                    CLR_SURFACE_HEADER);
  C2D_DrawRectSolid(0.0f, 30.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 2.0f,
                    CLR_ACCENT_PRIMARY);

  C2D_Text headerText;
  C2D_TextParse(&headerText, m_textBuf, "romm3ds - Match RomM Console");
  C2D_TextOptimize(&headerText);
  C2D_DrawText(&headerText, C2D_WithColor, 12.0f, 7.0f, 0.5f, 0.58f, 0.58f,
               CLR_TEXT_PRIMARY);

  C2D_DrawRectSolid(12.0f, 40.0f, 0.5f, 376.0f, 168.0f, CLR_BORDER_SUBTLE);
  C2D_DrawRectSolid(13.0f, 41.0f, 0.5f, 374.0f, 166.0f, CLR_SURFACE_CARD);
  C2D_DrawRectSolid(12.0f, 40.0f, 0.5f, 4.0f, 168.0f, CLR_ACCENT_PRIMARY);

  if (m_syncMgr && !m_filteredLocalIndices.empty() &&
      m_selectedGameIndex < m_filteredLocalIndices.size()) {
    size_t localActualIdx = m_filteredLocalIndices[m_selectedGameIndex];
    const auto &item = m_syncMgr->getItems()[localActualIdx];

    C2D_Text targetLbl;
    C2D_TextParse(&targetLbl, m_textBuf, "Target Local Game:");
    C2D_TextOptimize(&targetLbl);
    C2D_DrawText(&targetLbl, C2D_WithColor, 24.0f, 48.0f, 0.5f, 0.40f, 0.40f,
                 CLR_TEXT_DIM);

    std::string targetName =
        item.localTitle.name + "  [" + item.localTitle.console + "]";
    C2D_Text nameTxt;
    C2D_TextParse(&nameTxt, m_textBuf, targetName.c_str());
    C2D_TextOptimize(&nameTxt);
    float nScale = 0.50f;
    float nW = 0.0f, nH = 0.0f;
    C2D_TextGetDimensions(&nameTxt, nScale, nScale, &nW, &nH);
    if (nW > 348.0f && nW > 0.0f) {
      nScale *= (348.0f / nW);
    }
    C2D_DrawText(&nameTxt, C2D_WithColor, 24.0f, 64.0f, 0.5f, nScale, nScale,
                 CLR_ACCENT_SOFT);

    C2D_DrawRectSolid(24.0f, 88.0f, 0.5f, 350.0f, 1.0f, CLR_BORDER_SUBTLE);

    std::string guide =
        "Select a platform on the bottom screen to browse its\n"
        "ROMs on your RomM server:\n\n"
        "- Press (A) or tap a console to load its games\n"
        "- Press (X) to refresh platforms & library from RomM\n"
        "- Press (B) to cancel and return to the Main Menu";
    if (!m_loadingMessage.empty()) {
      guide += "\n\n-> " + m_loadingMessage;
    }
    C2D_Text guideText;
    C2D_TextParse(&guideText, m_textBuf, guide.c_str());
    C2D_TextOptimize(&guideText);
    C2D_DrawText(&guideText, C2D_WithColor, 24.0f, 96.0f, 0.5f, 0.42f, 0.42f,
                 CLR_TEXT_SECONDARY);
  }

  C2D_DrawRectSolid(0.0f, 216.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 24.0f,
                    CLR_SURFACE_HEADER);
  C2D_DrawRectSolid(0.0f, 216.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 1.0f,
                    CLR_BORDER_SUBTLE);

  C2D_Text hudText;
  C2D_TextParse(&hudText, m_textBuf,
                "(A) Open Console   (X) Refresh Library   (B) Back to Library");
  C2D_TextOptimize(&hudText);
  C2D_DrawText(&hudText, C2D_WithColor, 12.0f, 221.0f, 0.5f, 0.40f, 0.40f,
               CLR_TEXT_SECONDARY);
}

void UIManager::renderMatchingConsolesBottom() {
  C2D_DrawRectSolid(8.0f, 4.0f, 0.5f, RomM3DS::SCREEN_BOTTOM_WIDTH - 16.0f,
                    RomM3DS::SCREEN_BOTTOM_HEIGHT - 8.0f, CLR_CARD);

  size_t consoleTotal = m_availableConsoles.size();
  std::string header =
      "RomM Consoles (" +
      std::to_string(consoleTotal > 0 ? (m_selectedConsoleIndex + 1) : 0) +
      "/" + std::to_string(consoleTotal) + ")";
  C2D_Text titleText;
  C2D_TextParse(&titleText, m_textBuf, header.c_str());
  C2D_TextOptimize(&titleText);
  C2D_DrawText(&titleText, C2D_WithColor, 16.0f, 9.0f, 0.5f, 0.48f, 0.48f,
               CLR_TEXT_WHITE);

  size_t pageOffset = (m_selectedConsoleIndex / 5) * 5;
  for (size_t i = 0; i < 5 && (pageOffset + i) < consoleTotal; ++i) {
    size_t idx = pageOffset + i;
    const auto &c = m_availableConsoles[idx];
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
    C2D_DrawText(&nameText, C2D_WithColor, 38.0f, y + 5.0f, 0.5f, 0.44f, 0.44f,
                 CLR_TEXT_WHITE);

    std::string countLabel = std::to_string(c.romCount) + " ROMs";
    C2D_Text countText;
    C2D_TextParse(&countText, m_textBuf, countLabel.c_str());
    C2D_TextOptimize(&countText);
    C2D_DrawText(&countText, C2D_WithColor, 230.0f, y + 6.0f, 0.5f, 0.40f,
                 0.40f, CLR_TEXT_MUTED);
  }

  if (consoleTotal == 0) {
    C2D_Text emptyTxt;
    C2D_TextParse(
        &emptyTxt, m_textBuf,
        "No consoles found in RomM library.\nCheck connection or refresh (X).");
    C2D_TextOptimize(&emptyTxt);
    C2D_DrawText(&emptyTxt, C2D_WithColor, 24.0f, 80.0f, 0.5f, 0.45f, 0.45f,
                 CLR_TEXT_MUTED);
  }

  drawButton(16.0f, 194.0f, 138.0f, 30.0f, "[ Open Console (A) ]", false);
  drawButton(166.0f, 194.0f, 138.0f, 30.0f, "[ Back (B) ]", false);
}

void UIManager::renderMatchingGamesTop() {
  C2D_DrawRectSolid(0.0f, 0.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 32.0f,
                    CLR_SURFACE_HEADER);
  C2D_DrawRectSolid(0.0f, 30.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 2.0f,
                    CLR_ACCENT_SOFT);

  std::string header = "romm3ds - " + m_selectedConsoleName;
  if (header.length() > 38)
    header = header.substr(0, 36) + "..";
  C2D_Text headerText;
  C2D_TextParse(&headerText, m_textBuf, header.c_str());
  C2D_TextOptimize(&headerText);
  C2D_DrawText(&headerText, C2D_WithColor, 12.0f, 7.0f, 0.5f, 0.58f, 0.58f,
               CLR_TEXT_PRIMARY);

  C2D_DrawRectSolid(12.0f, 40.0f, 0.5f, 376.0f, 168.0f, CLR_BORDER_SUBTLE);
  C2D_DrawRectSolid(13.0f, 41.0f, 0.5f, 374.0f, 166.0f, CLR_SURFACE_CARD);
  C2D_DrawRectSolid(12.0f, 40.0f, 0.5f, 4.0f, 168.0f, CLR_ACCENT_SOFT);

  if (m_syncMgr && !m_filteredLocalIndices.empty() &&
      m_selectedGameIndex < m_filteredLocalIndices.size()) {
    size_t localActualIdx = m_filteredLocalIndices[m_selectedGameIndex];
    const auto &item = m_syncMgr->getItems()[localActualIdx];

    C2D_Text targetLbl;
    C2D_TextParse(&targetLbl, m_textBuf, "Pairing Local Title:");
    C2D_TextOptimize(&targetLbl);
    C2D_DrawText(&targetLbl, C2D_WithColor, 24.0f, 48.0f, 0.5f, 0.40f, 0.40f,
                 CLR_TEXT_DIM);

    std::string targetName =
        item.localTitle.name + "  [" + item.localTitle.console + "]";
    C2D_Text nameTxt;
    C2D_TextParse(&nameTxt, m_textBuf, targetName.c_str());
    C2D_TextOptimize(&nameTxt);
    float nScale = 0.50f;
    float nW = 0.0f, nH = 0.0f;
    C2D_TextGetDimensions(&nameTxt, nScale, nScale, &nW, &nH);
    if (nW > 348.0f && nW > 0.0f) {
      nScale *= (348.0f / nW);
    }
    C2D_DrawText(&nameTxt, C2D_WithColor, 24.0f, 64.0f, 0.5f, nScale, nScale,
                 CLR_ACCENT_SOFT);

    C2D_DrawRectSolid(24.0f, 88.0f, 0.5f, 350.0f, 1.0f, CLR_BORDER_SUBTLE);

    std::string guide = "RomM Platform: " + m_selectedConsoleName + " (" +
                        m_selectedConsoleSlug + ")\n";
    if (!m_remoteFilterText.empty()) {
      guide += "Search Filter: \"" + m_remoteFilterText + "\"\n";
    }
    guide += "\nSelect the matching game on the bottom screen:\n"
             "- (A) Link Selected RomM Game\n"
             "- (Y) Filter by Title   (X) Clear Filter\n"
             "- (L) Reload Platform ROMs from Server";

    C2D_Text guideText;
    C2D_TextParse(&guideText, m_textBuf, guide.c_str());
    C2D_TextOptimize(&guideText);
    C2D_DrawText(&guideText, C2D_WithColor, 24.0f, 96.0f, 0.5f, 0.41f, 0.41f,
                 CLR_TEXT_SECONDARY);
  }

  C2D_DrawRectSolid(0.0f, 216.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 24.0f,
                    CLR_SURFACE_HEADER);
  C2D_DrawRectSolid(0.0f, 216.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 1.0f,
                    CLR_BORDER_SUBTLE);

  C2D_Text hudText;
  C2D_TextParse(
      &hudText, m_textBuf,
      "(A) Pair Game   (Y) Search   (X) Clear   (L) Refresh   (B) Consoles");
  C2D_TextOptimize(&hudText);
  C2D_DrawText(&hudText, C2D_WithColor, 12.0f, 221.0f, 0.5f, 0.39f, 0.39f,
               CLR_TEXT_SECONDARY);
}

void UIManager::renderMatchingGamesBottom() {
  C2D_DrawRectSolid(8.0f, 4.0f, 0.5f, RomM3DS::SCREEN_BOTTOM_WIDTH - 16.0f,
                    RomM3DS::SCREEN_BOTTOM_HEIGHT - 8.0f, CLR_CARD);

  size_t remoteTotal = m_filteredRemoteIndices.size();
  std::string header =
      m_selectedConsoleName + " (" +
      std::to_string(remoteTotal > 0 ? (m_selectedRemoteIndex + 1) : 0) + "/" +
      std::to_string(remoteTotal) + ")";
  C2D_Text titleText;
  C2D_TextParse(&titleText, m_textBuf, header.c_str());
  C2D_TextOptimize(&titleText);
  C2D_DrawText(&titleText, C2D_WithColor, 16.0f, 9.0f, 0.5f, 0.48f, 0.48f,
               CLR_TEXT_WHITE);

  std::string searchDisplay = m_remoteFilterText.empty()
                                  ? "Search: Tap to filter (Y)..."
                                  : ("Search: " + m_remoteFilterText);
  u32 searchBorder = m_remoteFilterText.empty() ? CLR_BORDER_OFF : CLR_ACCENT;
  u32 searchTextColor =
      m_remoteFilterText.empty() ? CLR_TEXT_MUTED : CLR_TEXT_WHITE;
  C2D_DrawRectSolid(16.0f, 28.0f, 0.5f, 230.0f, 22.0f, searchBorder);
  C2D_DrawRectSolid(17.5f, 29.5f, 0.5f, 227.0f, 19.0f, CLR_INPUT_BG);

  C2D_Text searchTxt;
  C2D_TextParse(&searchTxt, m_textBuf, searchDisplay.c_str());
  C2D_TextOptimize(&searchTxt);
  C2D_DrawText(&searchTxt, C2D_WithColor, 22.0f, 32.0f, 0.5f, 0.40f, 0.40f,
               searchTextColor);

  drawButton(252.0f, 28.0f, 52.0f, 22.0f, "[ Clr ]", false);

  const auto &remote =
      m_syncMgr ? m_syncMgr->getRemoteLibrary() : std::vector<RommGame>();
  size_t pageOffset = (m_selectedRemoteIndex / 5) * 5;
  for (size_t i = 0; i < 5 && (pageOffset + i) < remoteTotal; ++i) {
    size_t filteredIdx = pageOffset + i;
    size_t actualIdx = m_filteredRemoteIndices[filteredIdx];
    const auto &g = remote[actualIdx];
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

    C2D_DrawText(&rowText, C2D_WithColor, 24.0f, y + 5.0f, 0.5f, textScale,
                 textScale, CLR_TEXT_WHITE);
  }

  if (remoteTotal == 0) {
    C2D_Text noMatches;
    std::string noMsg =
        m_remoteFilterText.empty()
            ? "No games found for this console."
            : "No games match search query.\nTap [Clr] to reset filter.";
    C2D_TextParse(&noMatches, m_textBuf, noMsg.c_str());
    C2D_TextOptimize(&noMatches);
    C2D_DrawText(&noMatches, C2D_WithColor, 24.0f, 80.0f, 0.5f, 0.45f, 0.45f,
                 CLR_TEXT_MUTED);
  }

  drawButton(16.0f, 194.0f, 138.0f, 30.0f, "[ Pair Game (A) ]", false);
  drawButton(166.0f, 194.0f, 138.0f, 30.0f, "[ Consoles (B) ]", false);
}

void UIManager::renderConflictTop() {
  C2D_DrawRectSolid(0.0f, 0.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 32.0f,
                    CLR_SURFACE_HEADER);
  C2D_DrawRectSolid(0.0f, 30.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 2.0f,
                    CLR_YELLOW);

  C2D_Text headerText;
  C2D_TextParse(&headerText, m_textBuf, "romm3ds - Save Conflict Resolution");
  C2D_TextOptimize(&headerText);
  C2D_DrawText(&headerText, C2D_WithColor, 12.0f, 7.0f, 0.5f, 0.58f, 0.58f,
               CLR_TEXT_PRIMARY);

  if (!m_syncMgr || m_conflictGameIndex >= m_syncMgr->getItems().size()) {
    return;
  }

  const auto &item = m_syncMgr->getItems()[m_conflictGameIndex];

  std::string titleStr = "Game: " + item.localTitle.name;
  if (titleStr.length() > 42) {
    titleStr = titleStr.substr(0, 39) + "...";
  }
  C2D_Text titleText;
  C2D_TextParse(&titleText, m_textBuf, titleStr.c_str());
  C2D_TextOptimize(&titleText);
  C2D_DrawText(&titleText, C2D_WithColor, 14.0f, 38.0f, 0.5f, 0.50f, 0.50f,
               CLR_ACCENT_SOFT);

  std::string localTimeStr =
      SyncManager::formatTimestamp(item.localTitle.saveTime);
  std::string remoteTimeStr =
      SyncManager::formatIsoTimestamp(item.remoteSaveUpdatedAt);

  time_t localTime = item.localTitle.saveTime;
  time_t remoteTime =
      item.remoteSaveTime != 0
          ? item.remoteSaveTime
          : SyncManager::parseIsoTimestamp(item.remoteSaveUpdatedAt);

  bool remoteIsNewer =
      (remoteTime > 0 && (localTime == 0 || remoteTime > localTime + 5));
  bool localIsNewer =
      (localTime > 0 && (remoteTime == 0 || localTime > remoteTime + 5));

  // Card 1: Local (3DS) Save
  float card1Y = 62.0f;
  float cardH = 46.0f;
  u32 card1Border = localIsNewer ? CLR_YELLOW : CLR_BORDER_SUBTLE;
  C2D_DrawRectSolid(12.0f, card1Y, 0.5f, 376.0f, cardH, CLR_BORDER_SUBTLE);
  C2D_DrawRectSolid(13.0f, card1Y + 1.0f, 0.5f, 374.0f, cardH - 2.0f,
                    CLR_SURFACE_CARD);
  C2D_DrawRectSolid(12.0f, card1Y, 0.5f, 4.0f, cardH, card1Border);

  C2D_Text localLbl;
  C2D_TextParse(&localLbl, m_textBuf, "Local (3DS) Save:");
  C2D_TextOptimize(&localLbl);
  C2D_DrawText(&localLbl, C2D_WithColor, 24.0f, card1Y + 5.0f, 0.5f, 0.42f,
               0.42f, CLR_TEXT_SECONDARY);

  C2D_Text localDateText;
  C2D_TextParse(&localDateText, m_textBuf, localTimeStr.c_str());
  C2D_TextOptimize(&localDateText);
  C2D_DrawText(&localDateText, C2D_WithColor, 24.0f, card1Y + 23.0f, 0.5f,
               0.48f, 0.48f, CLR_TEXT_PRIMARY);

  const char *localTag =
      localIsNewer ? "NEWER [^]" : (remoteIsNewer ? "OLDER" : "SAME");
  drawPillBadge(300.0f, card1Y + 14.0f, localTag, CLR_SURFACE_ALT,
                localIsNewer ? CLR_YELLOW : CLR_TEXT_SECONDARY, 0.40f);

  // Card 2: Remote (RomM) Save
  float card2Y = 114.0f;
  u32 card2Border = remoteIsNewer ? CLR_GREEN : CLR_BORDER_SUBTLE;
  C2D_DrawRectSolid(12.0f, card2Y, 0.5f, 376.0f, cardH, CLR_BORDER_SUBTLE);
  C2D_DrawRectSolid(13.0f, card2Y + 1.0f, 0.5f, 374.0f, cardH - 2.0f,
                    CLR_SURFACE_CARD);
  C2D_DrawRectSolid(12.0f, card2Y, 0.5f, 4.0f, cardH, card2Border);

  C2D_Text remoteLbl;
  C2D_TextParse(&remoteLbl, m_textBuf, "Remote (RomM) Save:");
  C2D_TextOptimize(&remoteLbl);
  C2D_DrawText(&remoteLbl, C2D_WithColor, 24.0f, card2Y + 5.0f, 0.5f, 0.42f,
               0.42f, CLR_TEXT_SECONDARY);

  C2D_Text remoteDateText;
  C2D_TextParse(&remoteDateText, m_textBuf, remoteTimeStr.c_str());
  C2D_TextOptimize(&remoteDateText);
  C2D_DrawText(&remoteDateText, C2D_WithColor, 24.0f, card2Y + 23.0f, 0.5f,
               0.48f, 0.48f, CLR_TEXT_PRIMARY);

  const char *remoteTag =
      remoteIsNewer ? "NEWER [v]" : (localIsNewer ? "OLDER" : "SAME");
  drawPillBadge(300.0f, card2Y + 14.0f, remoteTag, CLR_SURFACE_ALT,
                remoteIsNewer ? CLR_GREEN : CLR_TEXT_SECONDARY, 0.40f);

  // Recommendation & Automatic Backup Notice (NSA Pattern C)
  time_t diff = (localTime > remoteTime) ? (localTime - remoteTime) : (remoteTime - localTime);
  std::string deltaStr;
  if (diff < 60) {
    deltaStr = "< 1 minute";
  } else if (diff < 3600) {
    deltaStr = std::to_string(diff / 60) + "m";
  } else if (diff < 86400) {
    long h = diff / 3600;
    long m = (diff % 3600) / 60;
    deltaStr = std::to_string(h) + "h " + std::to_string(m) + "m";
  } else {
    long d = diff / 86400;
    long h = (diff % 86400) / 3600;
    deltaStr = std::to_string(d) + "d " + std::to_string(h) + "h";
  }

  std::string summary;
  u32 summaryColor = CLR_TEXT_PRIMARY;
  if (remoteIsNewer) {
    summary = "Remote is " + deltaStr + " newer than Local (3DS)";
    summaryColor = CLR_GREEN;
  } else if (localIsNewer) {
    summary = "Local (3DS) is " + deltaStr + " newer than Remote";
    summaryColor = CLR_YELLOW;
  } else {
    summary = "Saves have identical timestamps";
    summaryColor = CLR_TEXT_SECONDARY;
  }

  drawPillBadge(14.0f, 166.0f, summary.c_str(), CLR_SURFACE_ALT,
                summaryColor, 0.40f);

  std::string bkpMsg =
      "Automatic backup will be created in sdmc:/3ds/romm3ds/backups/" +
      item.localTitle.titleIdHex + "/";
  C2D_Text backupNote;
  C2D_TextParse(&backupNote, m_textBuf, bkpMsg.c_str());
  C2D_TextOptimize(&backupNote);
  C2D_DrawText(&backupNote, C2D_WithColor, 14.0f, 192.0f, 0.5f, 0.35f, 0.35f,
               CLR_TEXT_DIM);

  // Footer HUD Bar
  C2D_DrawRectSolid(0.0f, 216.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 24.0f,
                    CLR_SURFACE_HEADER);
  C2D_DrawRectSolid(0.0f, 216.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 1.0f,
                    CLR_BORDER_SUBTLE);

  C2D_Text hudText;
  C2D_TextParse(&hudText, m_textBuf,
                "(Up/Down) Select Action   (A) Confirm Choice   (B) Skip Game");
  C2D_TextOptimize(&hudText);
  C2D_DrawText(&hudText, C2D_WithColor, 12.0f, 221.0f, 0.5f, 0.40f, 0.40f,
               CLR_TEXT_SECONDARY);
}

void UIManager::renderConflictBottom() {
  C2D_DrawRectSolid(8.0f, 6.0f, 0.5f, RomM3DS::SCREEN_BOTTOM_WIDTH - 16.0f,
                    RomM3DS::SCREEN_BOTTOM_HEIGHT - 12.0f, CLR_CARD);

  C2D_Text promptText;
  std::string promptStr = m_batchSyncActive ? "Batch Sync Conflict Action:"
                                            : "Select Resolution Action:";
  C2D_TextParse(&promptText, m_textBuf, promptStr.c_str());
  C2D_TextOptimize(&promptText);
  C2D_DrawText(&promptText, C2D_WithColor, 16.0f, 14.0f, 0.5f, 0.55f, 0.55f,
               CLR_TEXT_WHITE);

  bool remoteIsNewer = false;
  bool localIsNewer = false;
  if (m_syncMgr && m_conflictGameIndex < m_syncMgr->getItems().size()) {
    const auto &item = m_syncMgr->getItems()[m_conflictGameIndex];
    time_t localTime = item.localTitle.saveTime;
    time_t remoteTime =
        item.remoteSaveTime != 0
            ? item.remoteSaveTime
            : SyncManager::parseIsoTimestamp(item.remoteSaveUpdatedAt);
    remoteIsNewer =
        (remoteTime > 0 && (localTime == 0 || remoteTime > localTime + 5));
    localIsNewer =
        (localTime > 0 && (remoteTime == 0 || localTime > remoteTime + 5));
  }

  std::string btnLocal = remoteIsNewer ? "[ Download Save (Overwrite Local) * ]"
                                       : "[ Download Save (Overwrite Local) ]";
  std::string btnRemote = localIsNewer ? "[ Upload Save (Use Local Save) * ]"
                                       : "[ Upload Save (Use Local Save) ]";
  std::string btnSkip = m_batchSyncActive ? "[ Skip & Continue Batch Sync ]"
                                          : "[ Skip This Game (Cancel) ]";

  bool sel0 = (m_conflictChoiceIndex == 0);
  bool sel1 = (m_conflictChoiceIndex == 1);
  bool sel2 = (m_conflictChoiceIndex == 2);

  // NSA Pattern C: Destructive overwrites highlight with CLR_DANGER (#E11D48) left indicator bar & border
  u32 accent0 = sel0 ? CLR_DANGER : 0;
  u32 accent1 = sel1 ? CLR_DANGER : 0;
  u32 accent2 = sel2 ? CLR_ACCENT_PRIMARY : 0;

  drawButton(16.0f, 44.0f, 288.0f, 32.0f, btnLocal.c_str(), sel0, accent0);
  drawButton(16.0f, 84.0f, 288.0f, 32.0f, btnRemote.c_str(), sel1, accent1);
  drawButton(16.0f, 124.0f, 288.0f, 32.0f, btnSkip.c_str(), sel2, accent2);

  C2D_Text guideText;
  C2D_TextParse(&guideText, m_textBuf,
                "Use D-Pad Up/Down + (A) or tap with stylus.");
  C2D_TextOptimize(&guideText);
  C2D_DrawText(&guideText, C2D_WithColor, 16.0f, 180.0f, 0.5f, 0.44f, 0.44f,
               CLR_TEXT_MUTED);
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
  case UIState::SAVE_PICKER:
    renderSavePickerTop();
    break;
  case UIState::FILTER_PICKER:
    renderFilterPickerTop();
    break;
  case UIState::SORT_PICKER:
    renderSortPickerTop();
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
  case UIState::SAVE_PICKER:
    renderSavePickerBottom();
    break;
  case UIState::FILTER_PICKER:
    renderFilterPickerBottom();
    break;
  case UIState::SORT_PICKER:
    renderSortPickerBottom();
    break;
  }

  if (m_showLogDialog) {
    renderOperationLogOverlay();
  } else if (!m_loadingMessage.empty()) {
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

void UIManager::openSavePicker() {
  if (!m_syncMgr || m_filteredLocalIndices.empty())
    return;
  if (m_selectedGameIndex >= m_filteredLocalIndices.size())
    return;

  size_t actualIdx = m_filteredLocalIndices[m_selectedGameIndex];
  const auto &item = m_syncMgr->getItems()[actualIdx];

  std::string startDir = "sdmc:/3ds";
  if (!item.localTitle.savePath.empty()) {
    size_t slash = item.localTitle.savePath.find_last_of("/\\");
    if (slash != std::string::npos) {
      startDir = item.localTitle.savePath.substr(0, slash);
    }
  } else if (item.localTitle.isFileRom && !item.localTitle.romPath.empty()) {
    size_t slash = item.localTitle.romPath.find_last_of("/\\");
    if (slash != std::string::npos) {
      startDir = item.localTitle.romPath.substr(0, slash);
    }
  }

  struct stat st;
  if (stat(startDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
    startDir = "sdmc:/3ds";
    if (stat(startDir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
      startDir = "sdmc:/";
    }
  }

  loadBrowserDirectory(startDir);

  if (!item.localTitle.savePath.empty()) {
    size_t slash = item.localTitle.savePath.find_last_of("/\\");
    std::string fileName = (slash != std::string::npos)
                               ? item.localTitle.savePath.substr(slash + 1)
                               : item.localTitle.savePath;
    for (size_t i = 0; i < m_browserEntries.size(); ++i) {
      if (m_browserEntries[i].name == fileName) {
        m_browserSelectedIndex = i;
        break;
      }
    }
  }

  m_state = UIState::SAVE_PICKER;
}

void UIManager::loadBrowserDirectory(const std::string &dir) {
  m_browserCurrentDir = dir;
  if (m_browserCurrentDir.empty())
    m_browserCurrentDir = "sdmc:/";
  if (m_browserCurrentDir.back() != '/')
    m_browserCurrentDir += '/';

  m_browserEntries.clear();
  m_browserSelectedIndex = 0;

  if (m_browserCurrentDir != "sdmc:/" && m_browserCurrentDir != "/") {
    FileEntry parent;
    parent.name = "..";
    parent.isDir = true;
    m_browserEntries.push_back(parent);
  }

  DIR *d = opendir(m_browserCurrentDir.c_str());
  if (d) {
    struct dirent *entry = nullptr;
    std::vector<FileEntry> dirs;
    std::vector<FileEntry> files;

    while ((entry = readdir(d)) != nullptr) {
      const char *name = entry->d_name;
      if (!name || name[0] == '\0')
        continue;
      if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0)
        continue;
      if (name[0] == '.')
        continue;

      std::string full = m_browserCurrentDir + name;
      struct stat st;
      if (stat(full.c_str(), &st) != 0)
        continue;

      FileEntry fe;
      fe.name = name;
      fe.isDir = S_ISDIR(st.st_mode);
      fe.size = st.st_size;
      fe.mtime = st.st_mtime;

      if (fe.isDir) {
        dirs.push_back(fe);
      } else {
        files.push_back(fe);
      }
    }
    closedir(d);

    std::sort(
        dirs.begin(), dirs.end(),
        [](const FileEntry &a, const FileEntry &b) { return a.name < b.name; });

    auto isSaveExt = [](const std::string &n) {
      size_t dot = n.find_last_of('.');
      if (dot == std::string::npos)
        return false;
      std::string ext = n.substr(dot);
      for (char &c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return (ext == ".sav" || ext == ".srm" || ext == ".dsv");
    };

    std::sort(files.begin(), files.end(),
              [&](const FileEntry &a, const FileEntry &b) {
                bool aSave = isSaveExt(a.name);
                bool bSave = isSaveExt(b.name);
                if (aSave != bSave)
                  return aSave > bSave;
                return a.name < b.name;
              });

    for (const auto &de : dirs)
      m_browserEntries.push_back(de);
    for (const auto &fe : files)
      m_browserEntries.push_back(fe);
  }
}

void UIManager::applyCustomSavePath(const std::string &path) {
  if (!m_syncMgr || m_filteredLocalIndices.empty())
    return;
  if (m_selectedGameIndex >= m_filteredLocalIndices.size())
    return;

  size_t actualIdx = m_filteredLocalIndices[m_selectedGameIndex];
  m_syncMgr->setCustomSavePath(actualIdx, path);
  m_statusMessage = m_syncMgr->getLastMessage();
  updateLocalFilteredList();
  m_state = UIState::MAIN_MENU;
}

void UIManager::handleSavePickerInput(u32 kDown, const touchPosition &touch) {
  size_t total = m_browserEntries.size();

  if (total > 0) {
    if (kDown & KEY_UP) {
      m_browserSelectedIndex = (m_browserSelectedIndex > 0)
                                   ? (m_browserSelectedIndex - 1)
                                   : (total - 1);
    } else if (kDown & KEY_DOWN) {
      m_browserSelectedIndex = (m_browserSelectedIndex + 1 < total)
                                   ? (m_browserSelectedIndex + 1)
                                   : 0;
    } else if (kDown & KEY_LEFT) {
      m_browserSelectedIndex =
          (m_browserSelectedIndex >= 5) ? (m_browserSelectedIndex - 5) : 0;
    } else if (kDown & KEY_RIGHT) {
      m_browserSelectedIndex = (m_browserSelectedIndex + 5 < total)
                                   ? (m_browserSelectedIndex + 5)
                                   : (total - 1);
    }
  }

  if (kDown & KEY_A) {
    if (total > 0 && m_browserSelectedIndex < total) {
      const auto &fe = m_browserEntries[m_browserSelectedIndex];
      if (fe.name == "..") {
        std::string cur = m_browserCurrentDir;
        while (!cur.empty() && cur.back() == '/')
          cur.pop_back();
        size_t slash = cur.find_last_of("/\\");
        if (slash != std::string::npos) {
          std::string parent = cur.substr(0, slash + 1);
          if (parent.empty() || parent == "sdmc:" || parent == "sdmc")
            parent = "sdmc:/";
          loadBrowserDirectory(parent);
        } else {
          loadBrowserDirectory("sdmc:/");
        }
      } else if (fe.isDir) {
        std::string next = m_browserCurrentDir;
        if (!next.empty() && next.back() != '/')
          next += '/';
        next += fe.name;
        loadBrowserDirectory(next);
      } else {
        std::string fullPath = m_browserCurrentDir;
        if (!fullPath.empty() && fullPath.back() != '/')
          fullPath += '/';
        fullPath += fe.name;
        applyCustomSavePath(fullPath);
      }
    }
  } else if (kDown & KEY_X) {
    std::string initText = m_browserCurrentDir;
    if (!m_filteredLocalIndices.empty() &&
        m_selectedGameIndex < m_filteredLocalIndices.size()) {
      size_t actualIdx = m_filteredLocalIndices[m_selectedGameIndex];
      const auto &item = m_syncMgr->getItems()[actualIdx];
      if (!item.localTitle.savePath.empty()) {
        initText = item.localTitle.savePath;
      }
    }
    std::string entered =
        launchKeyboard("Enter save path on SD (sdmc:/...)", initText, 256);
    if (!entered.empty() && entered != initText) {
      if (entered.rfind("sdmc:", 0) != 0 && entered.rfind("/", 0) == 0) {
        entered = "sdmc:" + entered;
      } else if (entered.rfind("sdmc:", 0) != 0) {
        entered = "sdmc:/" + entered;
      }
      applyCustomSavePath(entered);
    }
  } else if (kDown & KEY_Y) {
    applyCustomSavePath("");
  } else if (kDown & KEY_B) {
    m_state = UIState::MAIN_MENU;
  }

  if (kDown & KEY_TOUCH) {
    if (isTouchInside(touch, 8.0f, 206.0f, 72.0f, 26.0f)) {
      if (total > 0 && m_browserSelectedIndex < total) {
        const auto &fe = m_browserEntries[m_browserSelectedIndex];
        if (fe.name == "..") {
          std::string cur = m_browserCurrentDir;
          while (!cur.empty() && cur.back() == '/')
            cur.pop_back();
          size_t slash = cur.find_last_of("/\\");
          if (slash != std::string::npos) {
            std::string parent = cur.substr(0, slash + 1);
            if (parent.empty() || parent == "sdmc:" || parent == "sdmc")
              parent = "sdmc:/";
            loadBrowserDirectory(parent);
          } else {
            loadBrowserDirectory("sdmc:/");
          }
        } else if (fe.isDir) {
          std::string next = m_browserCurrentDir;
          if (!next.empty() && next.back() != '/')
            next += '/';
          next += fe.name;
          loadBrowserDirectory(next);
        } else {
          std::string fullPath = m_browserCurrentDir;
          if (!fullPath.empty() && fullPath.back() != '/')
            fullPath += '/';
          fullPath += fe.name;
          applyCustomSavePath(fullPath);
        }
      }
    } else if (isTouchInside(touch, 84.0f, 206.0f, 82.0f, 26.0f)) {
      std::string initText = m_browserCurrentDir;
      if (!m_filteredLocalIndices.empty() &&
          m_selectedGameIndex < m_filteredLocalIndices.size()) {
        size_t actualIdx = m_filteredLocalIndices[m_selectedGameIndex];
        const auto &item = m_syncMgr->getItems()[actualIdx];
        if (!item.localTitle.savePath.empty()) {
          initText = item.localTitle.savePath;
        }
      }
      std::string entered =
          launchKeyboard("Enter save path on SD (sdmc:/...)", initText, 256);
      if (!entered.empty() && entered != initText) {
        if (entered.rfind("sdmc:", 0) != 0 && entered.rfind("/", 0) == 0) {
          entered = "sdmc:" + entered;
        } else if (entered.rfind("sdmc:", 0) != 0) {
          entered = "sdmc:/" + entered;
        }
        applyCustomSavePath(entered);
      }
    } else if (isTouchInside(touch, 170.0f, 206.0f, 68.0f, 26.0f)) {
      applyCustomSavePath("");
    } else if (isTouchInside(touch, 242.0f, 206.0f, 70.0f, 26.0f)) {
      m_state = UIState::MAIN_MENU;
    } else {
      size_t pageOffset = (m_browserSelectedIndex / 5) * 5;
      for (size_t i = 0; i < 5 && (pageOffset + i) < total; ++i) {
        float rowY = 32.0f + static_cast<float>(i * 34.0f);
        if (isTouchInside(touch, 8.0f, rowY,
                          RomM3DS::SCREEN_BOTTOM_WIDTH - 16.0f, 32.0f)) {
          size_t target = pageOffset + i;
          if (m_browserSelectedIndex == target) {
            const auto &fe = m_browserEntries[target];
            if (fe.name == "..") {
              std::string cur = m_browserCurrentDir;
              while (!cur.empty() && cur.back() == '/')
                cur.pop_back();
              size_t slash = cur.find_last_of("/\\");
              if (slash != std::string::npos) {
                std::string parent = cur.substr(0, slash + 1);
                if (parent.empty() || parent == "sdmc:" || parent == "sdmc")
                  parent = "sdmc:/";
                loadBrowserDirectory(parent);
              } else {
                loadBrowserDirectory("sdmc:/");
              }
            } else if (fe.isDir) {
              std::string next = m_browserCurrentDir;
              if (!next.empty() && next.back() != '/')
                next += '/';
              next += fe.name;
              loadBrowserDirectory(next);
            } else {
              std::string fullPath = m_browserCurrentDir;
              if (!fullPath.empty() && fullPath.back() != '/')
                fullPath += '/';
              fullPath += fe.name;
              applyCustomSavePath(fullPath);
            }
          } else {
            m_browserSelectedIndex = target;
          }
          break;
        }
      }
    }
  }
}

void UIManager::renderSavePickerTop() {
  C2D_DrawRectSolid(0.0f, 0.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 32.0f,
                    CLR_SURFACE_HEADER);
  C2D_DrawRectSolid(0.0f, 30.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 2.0f,
                    CLR_ACCENT_PRIMARY);

  C2D_Text titleText;
  C2D_TextParse(&titleText, m_textBuf, "romm3ds - Custom Save Path");
  C2D_TextOptimize(&titleText);
  C2D_DrawText(&titleText, C2D_WithColor, 12.0f, 7.0f, 0.5f, 0.58f, 0.58f,
               CLR_TEXT_PRIMARY);

  std::string gameName = "Unknown Title";
  std::string curSavePath = "(None / Auto-detect)";
  bool hasSave = false;

  if (m_syncMgr && !m_filteredLocalIndices.empty() &&
      m_selectedGameIndex < m_filteredLocalIndices.size()) {
    size_t actualIdx = m_filteredLocalIndices[m_selectedGameIndex];
    const auto &item = m_syncMgr->getItems()[actualIdx];
    gameName = item.localTitle.name;
    if (!item.localTitle.savePath.empty()) {
      curSavePath = item.localTitle.savePath;
    }
    hasSave = item.localTitle.hasSave;
  }

  C2D_DrawRectSolid(12.0f, 40.0f, 0.5f, 376.0f, 82.0f, CLR_BORDER_SUBTLE);
  C2D_DrawRectSolid(13.0f, 41.0f, 0.5f, 374.0f, 80.0f, CLR_SURFACE_CARD);
  C2D_DrawRectSolid(12.0f, 40.0f, 0.5f, 4.0f, 82.0f,
                    hasSave ? CLR_GREEN : CLR_YELLOW);

  C2D_Text nameText;
  C2D_TextParse(&nameText, m_textBuf, gameName.c_str());
  C2D_TextOptimize(&nameText);
  float nScale = 0.50f;
  float nW = 0.0f, nH = 0.0f;
  C2D_TextGetDimensions(&nameText, nScale, nScale, &nW, &nH);
  if (nW > 350.0f && nW > 0.0f)
    nScale *= (350.0f / nW);
  C2D_DrawText(&nameText, C2D_WithColor, 24.0f, 46.0f, 0.5f, nScale, nScale,
               CLR_TEXT_PRIMARY);

  std::string pathLine = "Active Path: " + curSavePath;
  C2D_Text pathText;
  C2D_TextParse(&pathText, m_textBuf, pathLine.c_str());
  C2D_TextOptimize(&pathText);
  float pScale = 0.39f;
  float pW = 0.0f, pH = 0.0f;
  C2D_TextGetDimensions(&pathText, pScale, pScale, &pW, &pH);
  if (pW > 350.0f && pW > 0.0f)
    pScale *= (350.0f / pW);
  C2D_DrawText(&pathText, C2D_WithColor, 24.0f, 68.0f, 0.5f, pScale, pScale,
               CLR_TEXT_SECONDARY);

  std::string statusLine = hasSave ? "Save file verified on SD (Ready for sync)"
                                   : "No save file found at active path";
  u32 statusColor = hasSave ? CLR_GREEN : CLR_YELLOW;
  C2D_Text stText;
  C2D_TextParse(&stText, m_textBuf, statusLine.c_str());
  C2D_TextOptimize(&stText);
  C2D_DrawText(&stText, C2D_WithColor, 24.0f, 88.0f, 0.5f, 0.41f, 0.41f,
               statusColor);

  C2D_DrawRectSolid(12.0f, 128.0f, 0.5f, 376.0f, 80.0f, CLR_BORDER_SUBTLE);
  C2D_DrawRectSolid(13.0f, 129.0f, 0.5f, 374.0f, 78.0f, CLR_SURFACE_CARD);
  C2D_DrawRectSolid(12.0f, 128.0f, 0.5f, 4.0f, 80.0f, CLR_ACCENT_PRIMARY);

  std::string inst = "File Browser Controls:\n"
                     "- (A) Select highlighted .sav/.srm file or open folder\n"
                     "- (X) Manual keyboard path entry (sdmc:/...)\n"
                     "- (Y) Reset override to auto-detected save path";
  C2D_Text instText;
  C2D_TextParse(&instText, m_textBuf, inst.c_str());
  C2D_TextOptimize(&instText);
  C2D_DrawText(&instText, C2D_WithColor, 24.0f, 134.0f, 0.5f, 0.40f, 0.40f,
               CLR_TEXT_SECONDARY);

  C2D_DrawRectSolid(0.0f, 216.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 24.0f,
                    CLR_SURFACE_HEADER);
  C2D_DrawRectSolid(0.0f, 216.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 1.0f,
                    CLR_BORDER_SUBTLE);

  C2D_Text hudText;
  C2D_TextParse(&hudText, m_textBuf,
                "(A) Select/Open   (X) Keyboard   (Y) Reset Auto   (B) Back");
  C2D_TextOptimize(&hudText);
  C2D_DrawText(&hudText, C2D_WithColor, 12.0f, 221.0f, 0.5f, 0.40f, 0.40f,
               CLR_TEXT_SECONDARY);
}

void UIManager::renderSavePickerBottom() {
  C2D_DrawRectSolid(0.0f, 0.0f, 0.5f, RomM3DS::SCREEN_BOTTOM_WIDTH,
                    RomM3DS::SCREEN_BOTTOM_HEIGHT, CLR_BG_BOTTOM);

  C2D_DrawRectSolid(8.0f, 6.0f, 0.5f, RomM3DS::SCREEN_BOTTOM_WIDTH - 16.0f,
                    22.0f, CLR_CARD);

  std::string dirLabel = "Folder: " + m_browserCurrentDir;
  C2D_Text dirText;
  C2D_TextParse(&dirText, m_textBuf, dirLabel.c_str());
  C2D_TextOptimize(&dirText);
  float dScale = 0.38f;
  float dW = 0.0f, dH = 0.0f;
  C2D_TextGetDimensions(&dirText, dScale, dScale, &dW, &dH);
  if (dW > 290.0f && dW > 0.0f)
    dScale *= (290.0f / dW);
  C2D_DrawText(&dirText, C2D_WithColor, 14.0f, 10.0f, 0.5f, dScale, dScale,
               CLR_ACCENT);

  size_t total = m_browserEntries.size();
  if (total == 0) {
    C2D_Text emptyText;
    C2D_TextParse(&emptyText, m_textBuf, "(Directory is empty)");
    C2D_TextOptimize(&emptyText);
    C2D_DrawText(&emptyText, C2D_WithColor, 16.0f, 60.0f, 0.5f, 0.44f, 0.44f,
                 CLR_TEXT_MUTED);
  } else {
    size_t pageOffset = (m_browserSelectedIndex / 5) * 5;
    for (size_t i = 0; i < 5 && (pageOffset + i) < total; ++i) {
      size_t idx = pageOffset + i;
      const auto &fe = m_browserEntries[idx];
      bool selected = (idx == m_browserSelectedIndex);
      float y = 32.0f + static_cast<float>(i * 34.0f);

      u32 rowBg = selected ? CLR_HEADER : CLR_INPUT_BG;
      u32 borderColor = selected ? CLR_ACCENT : CLR_BORDER_OFF;
      C2D_DrawRectSolid(8.0f, y, 0.5f, RomM3DS::SCREEN_BOTTOM_WIDTH - 16.0f,
                        30.0f, borderColor);
      C2D_DrawRectSolid(9.5f, y + 1.5f, 0.5f,
                        RomM3DS::SCREEN_BOTTOM_WIDTH - 19.0f, 27.0f, rowBg);

      std::string prefix = fe.isDir ? "[DIR] " : "      ";
      auto isSaveExt = [](const std::string &n) {
        size_t dot = n.find_last_of('.');
        if (dot == std::string::npos)
          return false;
        std::string ext = n.substr(dot);
        for (char &c : ext)
          c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return (ext == ".sav" || ext == ".srm" || ext == ".dsv");
      };
      if (!fe.isDir && isSaveExt(fe.name)) {
        prefix = "[SAV] ";
      }

      u32 nameColor = fe.isDir
                          ? CLR_YELLOW
                          : (isSaveExt(fe.name) ? CLR_GREEN : CLR_TEXT_WHITE);
      std::string rowLabel = prefix + fe.name;

      C2D_Text rowText;
      C2D_TextParse(&rowText, m_textBuf, rowLabel.c_str());
      C2D_TextOptimize(&rowText);
      float rScale = 0.42f;
      float rW = 0.0f, rH = 0.0f;
      C2D_TextGetDimensions(&rowText, rScale, rScale, &rW, &rH);
      float maxNameW = fe.isDir ? 280.0f : 210.0f;
      if (rW > maxNameW && rW > 0.0f)
        rScale *= (maxNameW / rW);
      C2D_DrawText(&rowText, C2D_WithColor, 14.0f, y + 6.0f, 0.5f, rScale,
                   rScale, nameColor);

      if (!fe.isDir) {
        std::string sizeStr;
        if (fe.size < 1024)
          sizeStr = std::to_string(fe.size) + " B";
        else if (fe.size < 1024 * 1024)
          sizeStr = std::to_string(fe.size / 1024) + " KB";
        else
          sizeStr = std::to_string(fe.size / (1024 * 1024)) + " MB";

        C2D_Text sizeText;
        C2D_TextParse(&sizeText, m_textBuf, sizeStr.c_str());
        C2D_TextOptimize(&sizeText);
        float sW = 0.0f, sH = 0.0f;
        C2D_TextGetDimensions(&sizeText, 0.38f, 0.38f, &sW, &sH);
        C2D_DrawText(&sizeText, C2D_WithColor,
                     RomM3DS::SCREEN_BOTTOM_WIDTH - 16.0f - sW, y + 7.0f, 0.5f,
                     0.38f, 0.38f, CLR_TEXT_MUTED);
      }
    }
  }

  drawButton(8.0f, 206.0f, 72.0f, 26.0f, "[ Select ]", false);
  drawButton(84.0f, 206.0f, 82.0f, 26.0f, "[ Keyboard ]", false);
  drawButton(170.0f, 206.0f, 68.0f, 26.0f, "[ Reset ]", false);
  drawButton(242.0f, 206.0f, 70.0f, 26.0f, "[ Back ]", false);
}

void UIManager::handleFilterPickerInput(u32 kDown, const touchPosition &touch) {
  size_t total = m_filterOptions.size();
  if (total > 0) {
    if (kDown & KEY_UP) {
      m_selectedFilterPickerIndex = (m_selectedFilterPickerIndex > 0)
                                        ? (m_selectedFilterPickerIndex - 1)
                                        : (total - 1);
    } else if (kDown & KEY_DOWN) {
      m_selectedFilterPickerIndex = (m_selectedFilterPickerIndex + 1 < total)
                                        ? (m_selectedFilterPickerIndex + 1)
                                        : 0;
    } else if (kDown & KEY_LEFT) {
      m_selectedFilterPickerIndex = (m_selectedFilterPickerIndex >= 5)
                                        ? (m_selectedFilterPickerIndex - 5)
                                        : 0;
    } else if (kDown & KEY_RIGHT) {
      m_selectedFilterPickerIndex = (m_selectedFilterPickerIndex + 5 < total)
                                        ? (m_selectedFilterPickerIndex + 5)
                                        : (total - 1);
    }
  }

  auto applySelected = [&]() {
    if (m_selectedFilterPickerIndex < total) {
      const auto &opt = m_filterOptions[m_selectedFilterPickerIndex];
      m_localFilterMode = opt.mode;
      m_localFilterConsole = opt.console;
      updateLocalFilteredList();
    }
    m_state = UIState::MAIN_MENU;
  };

  if (kDown & KEY_A) {
    applySelected();
  } else if (kDown & KEY_B) {
    m_state = UIState::MAIN_MENU;
  }

  if (kDown & KEY_TOUCH) {
    if (isTouchInside(touch, 16.0f, 194.0f, 138.0f, 30.0f)) {
      applySelected();
    } else if (isTouchInside(touch, 166.0f, 194.0f, 138.0f, 30.0f)) {
      m_state = UIState::MAIN_MENU;
    } else {
      size_t pageOffset = (m_selectedFilterPickerIndex / 5) * 5;
      for (size_t i = 0; i < 5 && (pageOffset + i) < total; ++i) {
        float rowY = 36.0f + static_cast<float>(i * 30.0f);
        if (isTouchInside(touch, 16.0f, rowY, 288.0f, 26.0f)) {
          m_selectedFilterPickerIndex = pageOffset + i;
          applySelected();
          break;
        }
      }
    }
  }
}

void UIManager::handleSortPickerInput(u32 kDown, const touchPosition &touch) {
  size_t total = m_sortOptions.size();
  if (total > 0) {
    if (kDown & KEY_UP) {
      m_selectedSortPickerIndex = (m_selectedSortPickerIndex > 0)
                                      ? (m_selectedSortPickerIndex - 1)
                                      : (total - 1);
    } else if (kDown & KEY_DOWN) {
      m_selectedSortPickerIndex = (m_selectedSortPickerIndex + 1 < total)
                                      ? (m_selectedSortPickerIndex + 1)
                                      : 0;
    }
  }

  auto applySelected = [&]() {
    if (m_selectedSortPickerIndex < total) {
      m_localSortMode = m_sortOptions[m_selectedSortPickerIndex].mode;
      updateLocalFilteredList();
    }
    m_state = UIState::MAIN_MENU;
  };

  if (kDown & KEY_A) {
    applySelected();
  } else if (kDown & KEY_B) {
    m_state = UIState::MAIN_MENU;
  }

  if (kDown & KEY_TOUCH) {
    if (isTouchInside(touch, 16.0f, 194.0f, 138.0f, 30.0f)) {
      applySelected();
    } else if (isTouchInside(touch, 166.0f, 194.0f, 138.0f, 30.0f)) {
      m_state = UIState::MAIN_MENU;
    } else {
      size_t pageOffset = (m_selectedSortPickerIndex / 5) * 5;
      for (size_t i = 0; i < 5 && (pageOffset + i) < total; ++i) {
        float rowY = 36.0f + static_cast<float>(i * 30.0f);
        if (isTouchInside(touch, 16.0f, rowY, 288.0f, 26.0f)) {
          m_selectedSortPickerIndex = pageOffset + i;
          applySelected();
          break;
        }
      }
    }
  }
}

void UIManager::renderFilterPickerTop() {
  C2D_DrawRectSolid(0.0f, 0.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 32.0f,
                    CLR_SURFACE_HEADER);
  C2D_DrawRectSolid(0.0f, 30.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 2.0f,
                    CLR_ACCENT_PRIMARY);

  C2D_Text headerText;
  C2D_TextParse(&headerText, m_textBuf, "romm3ds - Filter Local Library");
  C2D_TextOptimize(&headerText);
  C2D_DrawText(&headerText, C2D_WithColor, 12.0f, 7.0f, 0.5f, 0.58f, 0.58f,
               CLR_TEXT_PRIMARY);

  C2D_DrawRectSolid(12.0f, 40.0f, 0.5f, 376.0f, 168.0f, CLR_BORDER_SUBTLE);
  C2D_DrawRectSolid(13.0f, 41.0f, 0.5f, 374.0f, 166.0f, CLR_SURFACE_CARD);
  C2D_DrawRectSolid(12.0f, 40.0f, 0.5f, 4.0f, 168.0f, CLR_ACCENT_PRIMARY);

  std::string guide = "Active Filter: " + getActiveFilterDescription() +
                      "\n\n"
                      "Select a filter option on the bottom screen:\n"
                      "- (A) or Tap Option: Apply Selected Filter\n"
                      "- (B) or Back: Cancel & Return to Library\n"
                      "- D-Pad Up/Down to navigate, Left/Right for page jumps";
  C2D_Text guideText;
  C2D_TextParse(&guideText, m_textBuf, guide.c_str());
  C2D_TextOptimize(&guideText);
  C2D_DrawText(&guideText, C2D_WithColor, 24.0f, 52.0f, 0.5f, 0.44f, 0.44f,
               CLR_TEXT_SECONDARY);

  C2D_DrawRectSolid(0.0f, 216.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 24.0f,
                    CLR_SURFACE_HEADER);
  C2D_DrawRectSolid(0.0f, 216.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 1.0f,
                    CLR_BORDER_SUBTLE);

  C2D_Text hudText;
  C2D_TextParse(&hudText, m_textBuf,
                "(Up/Down) Navigate   (A) Apply Filter   (B) Cancel & Return");
  C2D_TextOptimize(&hudText);
  C2D_DrawText(&hudText, C2D_WithColor, 12.0f, 221.0f, 0.5f, 0.40f, 0.40f,
               CLR_TEXT_SECONDARY);
}

void UIManager::renderFilterPickerBottom() {
  C2D_DrawRectSolid(8.0f, 4.0f, 0.5f, RomM3DS::SCREEN_BOTTOM_WIDTH - 16.0f,
                    RomM3DS::SCREEN_BOTTOM_HEIGHT - 8.0f, CLR_CARD);

  size_t total = m_filterOptions.size();
  std::string header =
      "Select Filter (" +
      std::to_string(total > 0 ? (m_selectedFilterPickerIndex + 1) : 0) + "/" +
      std::to_string(total) + ")";
  C2D_Text titleText;
  C2D_TextParse(&titleText, m_textBuf, header.c_str());
  C2D_TextOptimize(&titleText);
  C2D_DrawText(&titleText, C2D_WithColor, 16.0f, 9.0f, 0.5f, 0.48f, 0.48f,
               CLR_TEXT_WHITE);

  size_t pageOffset = (m_selectedFilterPickerIndex / 5) * 5;
  for (size_t i = 0; i < 5 && (pageOffset + i) < total; ++i) {
    size_t idx = pageOffset + i;
    const auto &opt = m_filterOptions[idx];
    bool selected = (idx == m_selectedFilterPickerIndex);
    bool active =
        (opt.mode == m_localFilterMode &&
         (opt.mode != LocalFilterMode::CONSOLE ||
          strcasecmp(opt.console.c_str(), m_localFilterConsole.c_str()) == 0));

    float y = 36.0f + static_cast<float>(i * 30.0f);
    u32 rowBg = selected ? CLR_HEADER : CLR_INPUT_BG;
    u32 borderColor = selected ? CLR_ACCENT : CLR_BORDER_OFF;

    C2D_DrawRectSolid(16.0f, y, 0.5f, 288.0f, 26.0f, borderColor);
    C2D_DrawRectSolid(17.5f, y + 1.5f, 0.5f, 285.0f, 23.0f, rowBg);

    u32 dotColor =
        active ? CLR_GREEN : (selected ? CLR_YELLOW : CLR_BORDER_OFF);
    C2D_DrawRectSolid(24.0f, y + 8.0f, 0.5f, 8.0f, 8.0f, dotColor);

    C2D_Text rowText;
    C2D_TextParse(&rowText, m_textBuf, opt.label.c_str());
    C2D_TextOptimize(&rowText);
    C2D_DrawText(&rowText, C2D_WithColor, 38.0f, y + 5.0f, 0.5f, 0.44f, 0.44f,
                 CLR_TEXT_WHITE);
  }

  drawButton(16.0f, 194.0f, 138.0f, 30.0f, "[ Apply (A) ]", false);
  drawButton(166.0f, 194.0f, 138.0f, 30.0f, "[ Back (B) ]", false);
}

void UIManager::renderSortPickerTop() {
  C2D_DrawRectSolid(0.0f, 0.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 32.0f,
                    CLR_SURFACE_HEADER);
  C2D_DrawRectSolid(0.0f, 30.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 2.0f,
                    CLR_ACCENT_PRIMARY);

  C2D_Text headerText;
  C2D_TextParse(&headerText, m_textBuf, "romm3ds - Sort Local Library");
  C2D_TextOptimize(&headerText);
  C2D_DrawText(&headerText, C2D_WithColor, 12.0f, 7.0f, 0.5f, 0.58f, 0.58f,
               CLR_TEXT_PRIMARY);

  C2D_DrawRectSolid(12.0f, 40.0f, 0.5f, 376.0f, 168.0f, CLR_BORDER_SUBTLE);
  C2D_DrawRectSolid(13.0f, 41.0f, 0.5f, 374.0f, 166.0f, CLR_SURFACE_CARD);
  C2D_DrawRectSolid(12.0f, 40.0f, 0.5f, 4.0f, 168.0f, CLR_ACCENT_PRIMARY);

  std::string guide =
      "Active Sort: " + std::string(getSortModeName(m_localSortMode)) +
      "\n\n"
      "Select a sort option on the bottom screen:\n"
      "- (A) or Tap Option: Apply Selected Sort\n"
      "- (B) or Back: Cancel & Return to Library";
  C2D_Text guideText;
  C2D_TextParse(&guideText, m_textBuf, guide.c_str());
  C2D_TextOptimize(&guideText);
  C2D_DrawText(&guideText, C2D_WithColor, 24.0f, 52.0f, 0.5f, 0.44f, 0.44f,
               CLR_TEXT_SECONDARY);

  C2D_DrawRectSolid(0.0f, 216.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 24.0f,
                    CLR_SURFACE_HEADER);
  C2D_DrawRectSolid(0.0f, 216.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 1.0f,
                    CLR_BORDER_SUBTLE);

  C2D_Text hudText;
  C2D_TextParse(&hudText, m_textBuf,
                "(Up/Down) Navigate   (A) Apply Sort   (B) Cancel & Return");
  C2D_TextOptimize(&hudText);
  C2D_DrawText(&hudText, C2D_WithColor, 12.0f, 221.0f, 0.5f, 0.40f, 0.40f,
               CLR_TEXT_SECONDARY);
}

void UIManager::renderSortPickerBottom() {
  C2D_DrawRectSolid(8.0f, 4.0f, 0.5f, RomM3DS::SCREEN_BOTTOM_WIDTH - 16.0f,
                    RomM3DS::SCREEN_BOTTOM_HEIGHT - 8.0f, CLR_CARD);

  size_t total = m_sortOptions.size();
  std::string header =
      "Select Sort (" +
      std::to_string(total > 0 ? (m_selectedSortPickerIndex + 1) : 0) + "/" +
      std::to_string(total) + ")";
  C2D_Text titleText;
  C2D_TextParse(&titleText, m_textBuf, header.c_str());
  C2D_TextOptimize(&titleText);
  C2D_DrawText(&titleText, C2D_WithColor, 16.0f, 9.0f, 0.5f, 0.48f, 0.48f,
               CLR_TEXT_WHITE);

  size_t pageOffset = (m_selectedSortPickerIndex / 5) * 5;
  for (size_t i = 0; i < 5 && (pageOffset + i) < total; ++i) {
    size_t idx = pageOffset + i;
    const auto &opt = m_sortOptions[idx];
    bool selected = (idx == m_selectedSortPickerIndex);
    bool active = (opt.mode == m_localSortMode);

    float y = 36.0f + static_cast<float>(i * 30.0f);
    u32 rowBg = selected ? CLR_HEADER : CLR_INPUT_BG;
    u32 borderColor = selected ? CLR_ACCENT : CLR_BORDER_OFF;

    C2D_DrawRectSolid(16.0f, y, 0.5f, 288.0f, 26.0f, borderColor);
    C2D_DrawRectSolid(17.5f, y + 1.5f, 0.5f, 285.0f, 23.0f, rowBg);

    u32 dotColor =
        active ? CLR_GREEN : (selected ? CLR_YELLOW : CLR_BORDER_OFF);
    C2D_DrawRectSolid(24.0f, y + 8.0f, 0.5f, 8.0f, 8.0f, dotColor);

    C2D_Text rowText;
    C2D_TextParse(&rowText, m_textBuf, opt.label.c_str());
    C2D_TextOptimize(&rowText);
    C2D_DrawText(&rowText, C2D_WithColor, 38.0f, y + 5.0f, 0.5f, 0.44f, 0.44f,
                 CLR_TEXT_WHITE);
  }

  drawButton(16.0f, 194.0f, 138.0f, 30.0f, "[ Apply (A) ]", false);
  drawButton(166.0f, 194.0f, 138.0f, 30.0f, "[ Back (B) ]", false);
}

void UIManager::freeGameLogo() {
  if (m_hasLogo) {
    C3D_TexDelete(&m_logoTex);
    m_hasLogo = false;
  }
  m_currentLogoKey.clear();
}

void UIManager::updateGameLogo(const InstalledTitle &title) {
  std::string key =
      title.titleIdHex + ":" + title.romPath + ":" + title.savePath;
  if (key == m_currentLogoKey) {
    return;
  }

  freeGameLogo();
  m_currentLogoKey = key;

  std::vector<uint8_t> iconData;
  if (!SaveManager::getTitleIcon(title, iconData) || iconData.size() < 0x1200) {
    return;
  }

  if (C3D_TexInit(&m_logoTex, 64, 64, GPU_RGB565)) {
    std::memset(m_logoTex.data, 0, 64 * 64 * 2);
    const uint8_t *src = iconData.data();
    uint8_t *dst = static_cast<uint8_t *>(m_logoTex.data);
    for (int ty = 0; ty < 6; ++ty) {
      for (int tx = 0; tx < 6; ++tx) {
        std::memcpy(dst + (ty * 8 + tx) * 128, src + (ty * 6 + tx) * 128, 128);
      }
    }
    C3D_TexFlush(&m_logoTex);

    m_logoSubTex.width = 48;
    m_logoSubTex.height = 48;
    m_logoSubTex.left = 0.0f;
    m_logoSubTex.top = 1.0f;
    m_logoSubTex.right = 48.0f / 64.0f;
    m_logoSubTex.bottom = 1.0f - (48.0f / 64.0f);

    m_logoImage.tex = &m_logoTex;
    m_logoImage.subtex = &m_logoSubTex;
    m_hasLogo = true;
  }
}

} // namespace RomM3DS
