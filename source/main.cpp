#include <3ds.h>
#include <citro2d.h>

#include "common.hpp"
#include "config_manager.hpp"
#include "network_manager.hpp"
#include "ui_manager.hpp"

int main(int argc, char *argv[]) {
  gfxInitDefault();

  bool romfsInitialized = R_SUCCEEDED(romfsInit());
  bool fsInitialized = R_SUCCEEDED(fsInit());
  bool amInitialized = R_SUCCEEDED(amInit());
  bool ndmuInitialized = R_SUCCEEDED(ndmuInit());
  bool netInitialized = RomM3DS::NetworkManager::initNetwork();

  C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
  C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
  C2D_Prepare();

  C3D_RenderTarget *topTarget = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
  C3D_RenderTarget *bottomTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

  RomM3DS::ConfigManager configMgr;
  configMgr.loadConfig();

  RomM3DS::NetworkManager netMgr(&configMgr);

  RomM3DS::UIManager uiMgr;
  uiMgr.init(topTarget, bottomTarget, &configMgr, &netMgr);

  touchPosition touch;

  while (aptMainLoop()) {
    hidScanInput();

    u32 kDown = hidKeysDown();
    if (kDown & KEY_START) {
      break;
    }

    hidTouchRead(&touch);
    uiMgr.handleInput(kDown, touch);

    uiMgr.render();
  }

  uiMgr.cleanup();

  C2D_Fini();
  C3D_Fini();

  if (netInitialized)
    RomM3DS::NetworkManager::exitNetwork();
  if (ndmuInitialized)
    ndmuExit();
  if (amInitialized)
    amExit();
  if (fsInitialized)
    fsExit();
  if (romfsInitialized)
    romfsExit();

  gfxExit();
  return 0;
}
