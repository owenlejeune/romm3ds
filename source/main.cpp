#include <3ds.h>
#include <citro2d.h>

#include "common.hpp"
#include "config_manager.hpp"
#include "network_manager.hpp"
#include "save_manager.hpp"
#include "sync_manager.hpp"
#include "ui_manager.hpp"

#include <cstdio>
#include <sys/stat.h>
#include <vector>

namespace {

void consolidateExecutable() {
  const char *rootPath = "sdmc:/3ds/romm3ds.3dsx";
  const char *subDir = "sdmc:/3ds/romm3ds";
  const char *subPath = "sdmc:/3ds/romm3ds/romm3ds.3dsx";

  struct stat stRoot, stSub;
  bool hasRoot = (stat(rootPath, &stRoot) == 0 && stRoot.st_size > 0);
  bool hasSub = (stat(subPath, &stSub) == 0 && stSub.st_size > 0);

  if (hasRoot) {
    RomM3DS::ConfigManager::ensureDirectories(subDir);
    if (!hasSub || stRoot.st_mtime >= stSub.st_mtime) {
      FILE *src = fopen(rootPath, "rb");
      if (src) {
        FILE *dst = fopen(subPath, "wb");
        if (dst) {
          std::vector<char> buf(64 * 1024);
          size_t bytes = 0;
          while ((bytes = fread(buf.data(), 1, buf.size(), src)) > 0) {
            fwrite(buf.data(), 1, bytes, dst);
          }
          fclose(dst);
        }
        fclose(src);
      }
    }
    remove(rootPath);
  }
}

constexpr const char *FORWARDER_RESTORE_ARG = "romm3ds-forwarder:hbldr-tid=";

void restoreLumaHbldrTitle(int argc, char *argv[]) {
  const size_t prefixLen = strlen(FORWARDER_RESTORE_ARG);
  for (int i = 1; i < argc; i++) {
    const char *arg = argv[i];
    if (arg == nullptr || strncmp(arg, FORWARDER_RESTORE_ARG, prefixLen) != 0) {
      continue;
    }
    char *end = nullptr;
    u64 tid = strtoull(arg + prefixLen, &end, 16);
    s64 luma = 0;
    if (end && *end == 0 && tid != 0 && R_SUCCEEDED(svcGetSystemInfo(&luma, 0x10000, 0))) {
      *(volatile u64 *)(OS_SHAREDCFG_VADDR + 0x808) = tid;
    }
  }
}

} // namespace

int main(int argc, char *argv[]) {
  restoreLumaHbldrTitle(argc, argv);
  gfxInitDefault();

  bool romfsInitialized = R_SUCCEEDED(romfsInit());
  bool fsInitialized = R_SUCCEEDED(fsInit());
  bool amInitialized = R_SUCCEEDED(amInit());
  bool cfguInitialized = R_SUCCEEDED(cfguInit());
  bool ndmuInitialized = R_SUCCEEDED(ndmuInit());
  bool netInitialized = RomM3DS::NetworkManager::initNetwork();

  if (fsInitialized) {
    consolidateExecutable();
  }

  C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
  C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
  C2D_Prepare();

  C3D_RenderTarget *topTarget = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
  C3D_RenderTarget *bottomTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

  RomM3DS::ConfigManager configMgr;
  configMgr.loadConfig();

  RomM3DS::NetworkManager netMgr(&configMgr);
  RomM3DS::SaveManager saveMgr;
  RomM3DS::SyncManager syncMgr(&configMgr, &netMgr, &saveMgr);

  RomM3DS::UIManager uiMgr;
  uiMgr.init(topTarget, bottomTarget, &configMgr, &netMgr, &saveMgr, &syncMgr);

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
  if (cfguInitialized)
    cfguExit();
  if (amInitialized)
    amExit();
  if (fsInitialized)
    fsExit();
  if (romfsInitialized)
    romfsExit();

  gfxExit();
  return 0;
}
