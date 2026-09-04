#include <nds.h>
#include <nds/fifocommon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "../../common/ipc.h"
#include "FastVideo/fvDecoder.h"
#include "FastVideo/fvMcData.h"
#include "FastVideo/fvPlayer.h"
#include "mpu.h"
#include "gui/PlayerController.h"
#include "../../common/twlwram.h"

static DTCM_BSS fv_player_t sPlayer;

static PlayerController* sPlayerController;

extern u8 gDldiStub[];

// free-running VBlank counter, used as a reliable timing reference for
// input debouncing: while a video plays, PlayerController::Update() spins
// in a tight, unthrottled loop (unlike while paused, which waits for
// VBlank), so it can poll the physical buttons far faster than normal -
// fast enough to catch a brief contact bounce on aging hardware as two
// separate presses. VBlankCounter() gives PlayerController a time base
// that doesn't depend on how fast that loop happens to be spinning.
volatile u32 gVBlankCount = 0;

static void vblankHandler()
{
    gVBlankCount++;
}

// scratch buffer used to receive the previous/next/random path found by the
// arm7 (must be writable by the arm7 CPU, so plain main RAM, and cacheline
// aligned so we can safely invalidate it)
static char sAdjacentPath[FV_MAX_PATH_LEN] ALIGN(32);

// path of the video currently playing, kept around so we can: show its
// filename in the on-screen toast, and reload it when "loop" is enabled
static char sCurPath[FV_MAX_PATH_LEN];

static bool sCanUseWram;
static bool sLoopEnabled = false;
static bool sRandomEnabled = false;

// returns the filename part of a path (after the last '/'), for display
static const char* GetFileName(const char* path)
{
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

// shows the filename of the video at sCurPath plus the current loop/random
// state as a brief on-screen toast
static void ShowVideoMessage()
{
    if (!sPlayerController)
        return;

    // strip the ".fv" extension for display (doesn't touch sCurPath itself,
    // which still needs it to actually reload the file)
    char displayName[64];
    strncpy(displayName, GetFileName(sCurPath), sizeof(displayName) - 1);
    displayName[sizeof(displayName) - 1] = 0;
    size_t len = strlen(displayName);
    if (len > 3 && strcasecmp(displayName + len - 3, ".fv") == 0)
        displayName[len - 3] = 0;

    char line2[32];
    snprintf(line2, sizeof(line2), "BOUCLE:%s  ALEA:%s", sLoopEnabled ? "ON " : "OFF", sRandomEnabled ? "ON" : "OFF");
    sPlayerController->ShowMessage(displayName, line2);
}

// Loads and starts the video at path. Destroys/replaces the current player
// and controller as needed. Returns false if the video could not be loaded
// (in which case there is no active player/controller anymore).
static bool loadAndStartVideo(const char* path)
{
    if (sPlayerController)
    {
        fv_pausePlayer(&sPlayer); // stop audio cleanly before tearing down
        delete sPlayerController;
        sPlayerController = NULL;
        fv_destroyPlayer(&sPlayer);
    }

    strncpy(sCurPath, path, sizeof(sCurPath) - 1);
    sCurPath[sizeof(sCurPath) - 1] = 0;

    if (!fv_initPlayer(&sPlayer, sCurPath, sCanUseWram))
    {
        fv_destroyPlayer(&sPlayer); // free whatever fv_initPlayer allocated before failing
        return false;
    }

    sPlayerController = new PlayerController(&sPlayer);
    sPlayerController->Initialize();
    fv_startPlayer(&sPlayer);
    return true;
}

// Asks the arm7 for the previous/next ".fv" file (alphabetically) in the
// same folder as the video currently playing, and switches to it if found.
// If no other ".fv" file exists, the current video keeps playing.
static void switchToAdjacentVideo(bool next)
{
    fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(next ? IPC_CMD_FIND_NEXT_FILE : IPC_CMD_FIND_PREV_FILE,
                                               (u32)sAdjacentPath));
    fifoWaitValue32(FIFO_USER_02);
    u32 found = fifoGetValue32(FIFO_USER_02) & IPC_CMD_ARG_MASK;
    if (!found)
        return; // no other video found next to the current one, keep playing

    DC_InvalidateRange(sAdjacentPath, sizeof(sAdjacentPath));
    loadAndStartVideo(sAdjacentPath);
}

// Asks the arm7 for a random ".fv" file (other than the current one) in the
// same folder as the video currently playing, and switches to it if found.
static void switchToRandomVideo()
{
    fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(IPC_CMD_FIND_RANDOM_FILE, (u32)sAdjacentPath));
    fifoWaitValue32(FIFO_USER_02);
    u32 found = fifoGetValue32(FIFO_USER_02) & IPC_CMD_ARG_MASK;
    if (!found)
        return; // no other video found next to the current one, keep playing

    DC_InvalidateRange(sAdjacentPath, sizeof(sAdjacentPath));
    loadAndStartVideo(sAdjacentPath);
}

int main(int argc, char** argv)
{
    DC_FlushAll();

    mpu_enableVramCache();

    irqSet(IRQ_VBLANK, vblankHandler);
    irqEnable(IRQ_VBLANK);

    bool canUseWram = false;
    if (isDSiMode() && twr_isUnlocked())
    {
        twr_setBlockMapping(TWR_WRAM_BLOCK_A, 0x03000000, 0x40000, TWR_WRAM_BLOCK_IMAGE_SIZE_256K);
        twr_setBlockMapping(TWR_WRAM_BLOCK_B, 0x03100000, 0x40000, TWR_WRAM_BLOCK_IMAGE_SIZE_256K);
        twr_setBlockMapping(TWR_WRAM_BLOCK_C, 0x03140000, 0x40000, TWR_WRAM_BLOCK_IMAGE_SIZE_256K);
        mpu_enableTwlWramCache();
        canUseWram = true;
    }

    fifoSetValue32Handler(FIFO_USER_01, NULL, NULL);

    // handshake
    fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_HANDSHAKE, 0));
    fifoWaitValue32(FIFO_USER_01);
    u32 handShake = fifoGetValue32(FIFO_USER_01);

    if (canUseWram && (handShake & IPC_CMD_ARG_MASK) == 0)
        canUseWram = false;

    sCanUseWram = canUseWram;

    if (!isDSiMode())
    {
        // setup dldi on arm7 if not on dsi
        DC_FlushRange(gDldiStub, 16 * 1024);
        fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_SETUP_DLDI, (u32)gDldiStub));
        fifoWaitValue32(FIFO_USER_01);
        fifoGetValue32(FIFO_USER_01);
    }

    videoSetModeSub(MODE_0_2D);
    vramSetBankH(VRAM_H_SUB_BG);
    vramSetBankI(VRAM_I_SUB_SPRITE);

    consoleInit(NULL, 2, BgType_Text4bpp, BgSize_T_256x256, /*0, 1*/ 2, 1, false, true);

    // iprintf("FastVideoDS Player by Gericom\n\n");

    vramSetBankA(VRAM_A_LCD);
    vramSetBankB(VRAM_B_LCD);
    vramSetBankC(VRAM_C_LCD);
    vramSetBankD(VRAM_D_LCD);
    vramSetBankE(VRAM_E_LCD);

    for (int i = 0; i < 3 * 128 * 1024; i += 4)
        *(vu32*)((u32)VRAM_A + i) = 0x80008000;

    const char* filePath;

    if (isDSiMode())
        filePath = "sd:/testVideo.fv";
    else
        filePath = "fat:/testVideo.fv";

    if (argc >= 2)
        filePath = argv[1];

    if (loadAndStartVideo(filePath))
    {
        bool shouldExit = false;
        while (sPlayerController && !shouldExit)
        {
            PlayerController::NavAction action = sPlayerController->Update();
            switch (action)
            {
                case PlayerController::NAV_ACTION_NEXT:
                    if (sRandomEnabled)
                        switchToRandomVideo();
                    else
                        switchToAdjacentVideo(true);
                    break;

                case PlayerController::NAV_ACTION_PREV:
                    if (sRandomEnabled)
                        switchToRandomVideo();
                    else
                        switchToAdjacentVideo(false);
                    break;

                case PlayerController::NAV_ACTION_VIDEO_ENDED:
                    if (sLoopEnabled)
                        loadAndStartVideo(sCurPath);
                    else if (sRandomEnabled)
                        switchToRandomVideo();
                    else
                        switchToAdjacentVideo(true);
                    break;

                case PlayerController::NAV_ACTION_TOGGLE_LOOP:
                    sLoopEnabled = !sLoopEnabled;
                    ShowVideoMessage();
                    break;

                case PlayerController::NAV_ACTION_TOGGLE_RANDOM:
                    sRandomEnabled = !sRandomEnabled;
                    ShowVideoMessage();
                    break;

                case PlayerController::NAV_ACTION_SHOW_INFO:
                    ShowVideoMessage();
                    break;

                case PlayerController::NAV_ACTION_EXIT:
                    shouldExit = true;
                    break;

                default:
                    break;
            }
        }
    }
    else
    {
        // could not load the (initial) video: nothing to do but wait
    }
    if (sPlayerController)
    {
        delete sPlayerController;
        fv_destroyPlayer(&sPlayer);
    }

    // hand control back to the launcher (TWiLight Menu++, nds-bootstrap, ...)
    exit(0);
}
