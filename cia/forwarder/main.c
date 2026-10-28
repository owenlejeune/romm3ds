#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define PRIMARY_TARGET_PATH   "/3ds/romm3ds/romm3ds.3dsx"
#define PRIMARY_TARGET_SD     "sdmc:" PRIMARY_TARGET_PATH
#define SECONDARY_TARGET_PATH "/3ds/romm3ds.3dsx"
#define SECONDARY_TARGET_SD   "sdmc:" SECONDARY_TARGET_PATH

#define DEFAULT_HBLDR_TID     0x000400000D921E00ULL

typedef struct {
    u64 hbldr_3dsx_tid;
    u64 selected_hbldr_3dsx_tid;
    bool use_hbldr;
} LumaSharedConfig;

#define LUMA_SHARED_CONFIG ((const volatile LumaSharedConfig *)(OS_SHAREDCFG_VADDR + 0x800))

static char sTarget[0x400] __attribute__((aligned(4)));
static u32 sArgv[0x400 / 4];

static Result HbldrSetTarget(Handle h, const char *path) {
    u32 *cmd = getThreadCommandBuffer();
    strncpy(sTarget, path, sizeof(sTarget) - 1);
    sTarget[sizeof(sTarget) - 1] = '\0';
    u32 pathLen = (u32)(strlen(sTarget) + 1);
    cmd[0] = IPC_MakeHeader(2, 0, 2);
    cmd[1] = IPC_Desc_StaticBuffer(pathLen, 0);
    cmd[2] = (u32)sTarget;
    Result res = svcSendSyncRequest(h);
    return R_FAILED(res) ? res : (Result)cmd[1];
}

static Result HbldrSetArgv(Handle h, const char *targetSd) {
    u32 *cmd = getThreadCommandBuffer();
    sArgv[0] = 1; // argc = 1
    char *p = (char *)&sArgv[1];
    strcpy(p, targetSd);
    size_t len = strlen(targetSd) + 1;
    u32 totalSize = (u32)(sizeof(u32) + len);

    cmd[0] = IPC_MakeHeader(3, 0, 2);
    cmd[1] = IPC_Desc_StaticBuffer(totalSize, 1);
    cmd[2] = (u32)sArgv;
    Result res = svcSendSyncRequest(h);
    return R_FAILED(res) ? res : (Result)cmd[1];
}

static bool IsLuma(void) {
    s64 version = 0;
    return R_SUCCEEDED(svcGetSystemInfo(&version, 0x10000, 0));
}

static Result Forward(const char **why, u64 *chainTid, u8 *mediaType) {
    const char *targetPath = PRIMARY_TARGET_PATH;
    const char *targetSd = PRIMARY_TARGET_SD;

    struct stat st;
    if (stat(PRIMARY_TARGET_SD, &st) != 0) {
        if (stat(SECONDARY_TARGET_SD, &st) == 0) {
            targetPath = SECONDARY_TARGET_PATH;
            targetSd = SECONDARY_TARGET_SD;
        } else {
            *why = "romm3ds.3dsx was not found on your SD card.\n\n"
                   "Please ensure the file exists at:\n"
                   "  sdmc:/3ds/romm3ds/romm3ds.3dsx";
            return -1;
        }
    }

    if (!IsLuma()) {
        *why = "This forwarder requires Luma3DS CFW.\n\n"
               "Please launch romm3ds from the Homebrew Launcher.";
        return -1;
    }

    u64 targetTid = 0;
    if (LUMA_SHARED_CONFIG->use_hbldr) {
        targetTid = LUMA_SHARED_CONFIG->hbldr_3dsx_tid;
    }
    if (targetTid == 0) {
        targetTid = DEFAULT_HBLDR_TID;
    }

    Handle hbldr = 0;
    Result res = svcConnectToPort(&hbldr, "hb:ldr");
    if (R_SUCCEEDED(res)) {
        res = HbldrSetTarget(hbldr, targetPath);
        if (R_SUCCEEDED(res)) {
            res = HbldrSetArgv(hbldr, targetSd);
        }
        svcCloseHandle(hbldr);
    }

    if (R_FAILED(res)) {
        *why = "Luma3DS homebrew loader (hb:ldr) error.\n\n"
               "Please ensure Luma3DS is up-to-date.";
        return res;
    }

    *chainTid = targetTid;
    *mediaType = (((targetTid >> 32) & 0xFFFFFFFF) == 0x00040010) ? MEDIATYPE_NAND : MEDIATYPE_SD;
    return 0;
}

int main(void) {
    const char *why = NULL;
    u64 chainTid = 0;
    u8 mediaType = MEDIATYPE_SD;
    Result res = Forward(&why, &chainTid, &mediaType);

    if (R_SUCCEEDED(res)) {
        aptSetChainloader(chainTid, mediaType);
        return 0;
    }

    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

    printf("\n  === romm3ds Forwarder ===\n\n");
    if (why) {
        printf("  %s\n", why);
    }
    if (res != -1) {
        printf("\n  Error: 0x%08lX\n", (unsigned long)res);
    }
    printf("\n\n  Press START or (A) to exit.\n");

    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        if (kDown & (KEY_START | KEY_A | KEY_B)) {
            break;
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    gfxExit();
    return 0;
}
