#include "ui_manager.hpp"
#include "network_manager.hpp"
#include "config_manager.hpp"
#include "common.hpp"

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
constexpr u32 CLR_ERROR        = C2D_Color32(0xEF, 0x44, 0x44, 0xFF);

} // namespace

UIManager::UIManager() = default;

UIManager::~UIManager() {
    cleanup();
}

void UIManager::init(C3D_RenderTarget* topTarget, C3D_RenderTarget* bottomTarget, ConfigManager* configMgr, NetworkManager* netMgr) {
    m_topTarget = topTarget;
    m_bottomTarget = bottomTarget;
    m_configMgr = configMgr;
    m_netMgr = netMgr;

    m_textBuf = C2D_TextBufNew(4096);

    if (m_configMgr && m_configMgr->hasValidConfig()) {
        const auto& cfg = m_configMgr->getConfig();
        m_enteredEndpoint = cfg.serverEndpoint;
        m_enteredApiKey = cfg.apiKey;
        m_state = UIState::MAIN_MENU;
        m_statusMessage = "Configuration loaded from SD card.";
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
        } else {
            m_statusMessage = "Error: Failed to write config to SD.";
        }
    }
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
        if (kDown & KEY_X) {
            runConnectionTest();
        }

        if (kDown & KEY_Y) {
            m_state = UIState::FIRST_LAUNCH;
            m_statusMessage = "Editing configuration.";
        }

        if (kDown & KEY_TOUCH) {
            if (isTouchInside(touch, 16.0f, 126.0f, 288.0f, 34.0f)) {
                runConnectionTest();
            } else if (isTouchInside(touch, 16.0f, 174.0f, 288.0f, 34.0f)) {
                m_state = UIState::FIRST_LAUNCH;
                m_statusMessage = "Editing configuration.";
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

void UIManager::drawButton(float x, float y, float w, float h, const char* label, bool selected) {
    u32 btnBg = selected ? CLR_ACCENT : CLR_CARD;
    u32 borderColor = selected ? CLR_TEXT_WHITE : CLR_BORDER_OFF;

    C2D_DrawRectSolid(x, y, 0.5f, w, h, borderColor);
    C2D_DrawRectSolid(x + 1.5f, y + 1.5f, 0.5f, w - 3.0f, h - 3.0f, btnBg);

    C2D_Text btnText;
    C2D_TextParse(&btnText, m_textBuf, label);
    C2D_TextOptimize(&btnText);
    C2D_DrawText(&btnText, C2D_WithColor, x + 16.0f, y + 9.0f, 0.5f, 0.52f, 0.52f, CLR_TEXT_WHITE);
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

    u32 statusColor = (m_statusMessage.find("Error") != std::string::npos) ? CLR_ERROR : CLR_GREEN;
    C2D_Text statusText;
    C2D_TextParse(&statusText, m_textBuf, m_statusMessage.c_str());
    C2D_TextOptimize(&statusText);
    C2D_DrawText(&statusText, C2D_WithColor, 16.0f, 198.0f, 0.5f, 0.44f, 0.44f, statusColor);
}

void UIManager::renderMainMenuTop() {
    C2D_DrawRectSolid(0.0f, 0.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 42.0f, CLR_HEADER);
    C2D_DrawRectSolid(0.0f, 40.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 2.0f, CLR_GREEN);

    C2D_Text headerText;
    C2D_TextParse(&headerText, m_textBuf, "romm3ds - Main Menu");
    C2D_TextOptimize(&headerText);
    C2D_DrawText(&headerText, C2D_WithColor, 12.0f, 10.0f, 0.5f, 0.75f, 0.75f, CLR_TEXT_WHITE);

    std::string info = "Active RomM Server:\n  " +
                       (m_configMgr ? m_configMgr->getConfig().serverEndpoint : "") +
                       "\n\nStatus: Configured & Network Ready.\n" +
                       "Next Step: Title scan & save mounting (Step 4).\n\n" +
                       "Controls:\n" +
                       "  (X) Test Server Connection\n" +
                       "  (Y) Edit Configuration\n" +
                       "  (START) Exit to Homebrew Menu";

    C2D_Text infoText;
    C2D_TextParse(&infoText, m_textBuf, info.c_str());
    C2D_TextOptimize(&infoText);
    C2D_DrawText(&infoText, C2D_WithColor, 16.0f, 56.0f, 0.5f, 0.48f, 0.48f, CLR_TEXT_WHITE);
}

void UIManager::renderMainMenuBottom() {
    C2D_DrawRectSolid(8.0f, 6.0f, 0.5f, RomM3DS::SCREEN_BOTTOM_WIDTH - 16.0f, RomM3DS::SCREEN_BOTTOM_HEIGHT - 12.0f, CLR_CARD);

    C2D_Text titleText;
    C2D_TextParse(&titleText, m_textBuf, "Network & Sync Hub");
    C2D_TextOptimize(&titleText);
    C2D_DrawText(&titleText, C2D_WithColor, 16.0f, 14.0f, 0.5f, 0.65f, 0.65f, CLR_TEXT_WHITE);

    drawButton(16.0f, 126.0f, 288.0f, 34.0f, "[ Test Connection (X) ]", false);
    drawButton(16.0f, 174.0f, 288.0f, 34.0f, "[ Edit Settings (Y) ]", false);

    u32 statusColor = (m_statusMessage.find("Error") != std::string::npos || m_statusMessage.find("Failed") != std::string::npos) ? CLR_ERROR : CLR_GREEN;
    C2D_Text statusText;
    C2D_TextParse(&statusText, m_textBuf, m_statusMessage.c_str());
    C2D_TextOptimize(&statusText);
    C2D_DrawText(&statusText, C2D_WithColor, 16.0f, 52.0f, 0.5f, 0.46f, 0.46f, statusColor);
}

void UIManager::renderTopScreen() {
    if (m_state == UIState::FIRST_LAUNCH) {
        renderFirstLaunchTop();
    } else {
        renderMainMenuTop();
    }
}

void UIManager::renderBottomScreen() {
    if (m_state == UIState::FIRST_LAUNCH) {
        renderFirstLaunchBottom();
    } else {
        renderMainMenuBottom();
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

    C3D_FrameEnd();
}

} // namespace RomM3DS
