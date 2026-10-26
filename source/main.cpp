#include <3ds.h>
#include <citro2d.h>

#include "common.hpp"
#include <cstdio>

int main(int argc, char *argv[]) {
  gfxInitDefault();

  bool romfsInitialized = R_SUCCEEDED(romfsInit());
  bool fsInitialized = R_SUCCEEDED(fsInit());
  bool amInitialized = R_SUCCEEDED(amInit());
  bool ndmuInitialized = R_SUCCEEDED(ndmuInit());

  C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
  C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
  C2D_Prepare();

  C3D_RenderTarget *topTarget = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
  C3D_RenderTarget *bottomTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

  C2D_TextBuf textBuf = C2D_TextBufNew(4096);

  C2D_Text titleText;
  C2D_TextParse(&titleText, textBuf, "romm3ds");
  C2D_TextOptimize(&titleText);

  C2D_Text subText;
  C2D_TextParse(&subText, textBuf, "Step 1: Scaffold & citro2d Loop Ready");
  C2D_TextOptimize(&subText);

  char statusBuf[128];
  std::snprintf(statusBuf, sizeof(statusBuf),
                "Services: FS=%s | AM=%s | NDMU=%s | RomFS=%s",
                fsInitialized ? "OK" : "FAIL", amInitialized ? "OK" : "FAIL",
                ndmuInitialized ? "OK" : "FAIL",
                romfsInitialized ? "OK" : "FAIL");
  C2D_Text statusText;
  C2D_TextParse(&statusText, textBuf, statusBuf);
  C2D_TextOptimize(&statusText);

  C2D_Text exitPromptText;
  C2D_TextParse(&exitPromptText, textBuf, "Press START to exit");
  C2D_TextOptimize(&exitPromptText);

  C2D_Text bottomTitleText;
  C2D_TextParse(&bottomTitleText, textBuf, "System Status & Ready Check");
  C2D_TextOptimize(&bottomTitleText);

  C2D_Text bottomInfoText;
  C2D_TextParse(&bottomInfoText, textBuf,
                "Hardware Initialized:\n"
                "- Screen Target (Top: 400x240, Bottom: 320x240)\n"
                "- libctru Services (FS, AM, NDMU, RomFS)\n"
                "- Citro2D / Citro3D Graphics Pipeline\n\n"
                "Ready for Step 2: ConfigManager & UIManager.");
  C2D_TextOptimize(&bottomInfoText);

  const u32 clrBgTop = C2D_Color32(0x1A, 0x1D, 0x24, 0xFF);
  const u32 clrBgBottom = C2D_Color32(0x12, 0x14, 0x1A, 0xFF);
  const u32 clrHeader = C2D_Color32(0x23, 0x28, 0x34, 0xFF);
  const u32 clrAccent = C2D_Color32(0x40, 0x98, 0xD7, 0xFF);
  const u32 clrCard = C2D_Color32(0x1F, 0x24, 0x30, 0xFF);
  const u32 clrTextPrimary = C2D_Color32(0xFA, 0xFA, 0xFC, 0xFF);
  const u32 clrTextSec = C2D_Color32(0x8D, 0x99, 0xAE, 0xFF);
  const u32 clrStatusGreen = C2D_Color32(0x3B, 0xD6, 0x71, 0xFF);

  while (aptMainLoop()) {
    hidScanInput();

    u32 kDown = hidKeysDown();
    if (kDown & KEY_START) {
      break;
    }

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

    C2D_TargetClear(topTarget, clrBgTop);
    C2D_SceneBegin(topTarget);

    C2D_DrawRectSolid(0.0f, 0.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 44.0f,
                      clrHeader);
    C2D_DrawRectSolid(0.0f, 42.0f, 0.5f, RomM3DS::SCREEN_TOP_WIDTH, 2.0f,
                      clrAccent);

    C2D_DrawText(&titleText, C2D_WithColor, 12.0f, 10.0f, 0.5f, 0.80f, 0.80f,
                 clrTextPrimary);
    C2D_DrawText(&subText, C2D_WithColor, 16.0f, 62.0f, 0.5f, 0.60f, 0.60f,
                 clrAccent);
    C2D_DrawText(&statusText, C2D_WithColor, 16.0f, 102.0f, 0.5f, 0.52f, 0.52f,
                 clrStatusGreen);
    C2D_DrawText(&exitPromptText, C2D_WithColor, 16.0f, 206.0f, 0.5f, 0.50f,
                 0.50f, clrTextSec);

    C2D_TargetClear(bottomTarget, clrBgBottom);
    C2D_SceneBegin(bottomTarget);

    C2D_DrawRectSolid(10.0f, 10.0f, 0.5f, RomM3DS::SCREEN_BOTTOM_WIDTH - 20.0f,
                      RomM3DS::SCREEN_BOTTOM_HEIGHT - 20.0f, clrCard);
    C2D_DrawRectSolid(10.0f, 10.0f, 0.5f, RomM3DS::SCREEN_BOTTOM_WIDTH - 20.0f,
                      32.0f, clrHeader);
    C2D_DrawText(&bottomTitleText, C2D_WithColor, 18.0f, 16.0f, 0.5f, 0.58f,
                 0.58f, clrTextPrimary);
    C2D_DrawText(&bottomInfoText, C2D_WithColor, 18.0f, 52.0f, 0.5f, 0.48f,
                 0.48f, clrTextSec);

    C3D_FrameEnd();
  }

  C2D_TextBufDelete(textBuf);
  C2D_Fini();
  C3D_Fini();

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
