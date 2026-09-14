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
#include "gui/BrowserController.h"
#include "../../common/twlwram.h"

static DTCM_BSS fv_player_t sPlayer;

static PlayerController* sPlayerController;

extern u8 gDldiStub[];

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

// standalone mode: no argv[1] at boot, so the built-in browser is the entry
// point (and B during playback returns to it instead of quitting)
static bool sStandalone = false;
static char sBrowserDir[FV_MAX_PATH_LEN];
static char sBrowserPick[FV_MAX_PATH_LEN];

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
    snprintf(line2, sizeof(line2), "ALEA:%s  BOUCLE:%s", sRandomEnabled ? "ON " : "OFF", sLoopEnabled ? "ON" : "OFF");
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

static void DestroyCurrentPlayer()
{
    if (sPlayerController)
    {
        fv_pausePlayer(&sPlayer); // stop audio cleanly (stopAudioClearQueue) before teardown
        delete sPlayerController;
        sPlayerController = NULL;
        fv_destroyPlayer(&sPlayer);
    }
}

// "sd:/a/b" -> "sd:/a", "sd:/a" -> "sd:/", "sd:/" -> "sd:/"
static void GetParentDir(const char* path, char* out, size_t outMax)
{
    const char* slash = strrchr(path, '/');
    if (!slash)
    {
        strncpy(out, path, outMax - 1);
        out[outMax - 1] = 0;
        return;
    }
    size_t len = (size_t)(slash - path);
    if (len == 0)
    {
        strncpy(out, "/", outMax - 1);
        out[outMax - 1] = 0;
        return;
    }
    if (len >= outMax)
        len = outMax - 1;
    memcpy(out, path, len);
    out[len] = 0;
    // "sd:" -> "sd:/" (device root)
    if (strchr(out, ':') && !strchr(out, '/'))
    {
        if (len + 2 < outMax)
        {
            out[len] = '/';
            out[len + 1] = 0;
        }
    }
}

// Runs the built-in browser until the user picks a video (returns 1, path
// written to outPath and last dir to outDir) or asks to quit (returns 0).
// selectName, if non-NULL, is the filename the cursor should be positioned
// on after listing (used when returning from playback).
static int RunBrowser(char* outPath, size_t outPathMax, char* outDir, size_t outDirMax, const char* selectName)
{
    BrowserController browser;
    if (!browser.OpenDir(sBrowserDir))
        return 0;
    if (selectName)
        browser.SelectEntryByName(selectName);

    char tmp[FV_MAX_PATH_LEN];
    for (;;)
    {
        BrowserController::Action action = browser.Update();
        switch (action)
        {
            case BrowserController::ACT_EXIT:
                return 0;

            case BrowserController::ACT_PLAY:
                browser.GetSelectedPath(outPath, outPathMax);
                strncpy(outDir, browser.GetCurDir(), outDirMax - 1);
                outDir[outDirMax - 1] = 0;
                consoleClear();
                return 1;

            case BrowserController::ACT_OPEN_DIR:
                browser.GetSelectedPath(tmp, sizeof(tmp));
                browser.OpenDir(tmp);
                break;

            case BrowserController::ACT_PARENT:
                GetParentDir(browser.GetCurDir(), tmp, sizeof(tmp));
                browser.OpenDir(tmp);
                break;

            default:
                break;
        }
    }
}

// Runs the player until the user exits. Returns true if the whole app
// should quit, false if control should go back to the browser.
static bool RunPlayerLoop(bool canReturnToBrowser)
{
    bool shouldExit = false;
    bool backToBrowser = false;

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
                backToBrowser = canReturnToBrowser;
                break;

            default:
                break;
        }
    }

    DestroyCurrentPlayer();
    return !backToBrowser;
}

// free-running 32-bit tick counter, used as a reliable timing reference for
// input debouncing (see PlayerController::UpdateKeys()). This CANNOT be
// based on IRQ_VBLANK/a vblank counter incremented from here: fvPlayer.c's
// fv_startPlayer() calls its own irqSet(IRQ_VBLANK, ...) every time
// playback (re)starts or seeks (needed for its own A/V sync), which
// silently replaces whatever handler main.cpp installs - so a
// vblank-counter approach freezes the instant the first video starts,
// making any single-instance debounce state permanently stick after its
// first use. TIMER0+TIMER1 (cascaded) are not touched anywhere else in
// this codebase, so they give PlayerController an independent, always-
// ticking time base regardless of what the FastVideo core does with
// IRQ_VBLANK.
static void InitDebounceTimer()
{
    TIMER0_DATA = 0;
    TIMER0_CR = TIMER_DIV_1024 | TIMER_ENABLE;
    TIMER1_DATA = 0;
    TIMER1_CR = TIMER_CASCADE | TIMER_ENABLE;
}

// ticks at BUS_CLOCK/1024 (~32728.5 Hz on NDS), i.e. ~30.5us/tick
u32 GetDebounceTicks()
{
    return ((u32)TIMER1_DATA << 16) | (u32)TIMER0_DATA;
}

int main(int argc, char** argv)
{
    DC_FlushAll();

    mpu_enableVramCache();

    // IRQ_VBLANK itself still needs to be enabled at the CPU level here -
    // fvPlayer.c relies on it already being on when it installs its own
    // handler, it never calls irqEnable() itself
    irqEnable(IRQ_VBLANK);
    InitDebounceTimer();

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

    vramSetBankA(VRAM_A_LCD);
    vramSetBankB(VRAM_B_LCD);
    vramSetBankC(VRAM_C_LCD);
    vramSetBankD(VRAM_D_LCD);
    vramSetBankE(VRAM_E_LCD);

    for (int i = 0; i < 3 * 128 * 1024; i += 4)
        *(vu32*)((u32)VRAM_A + i) = 0x80008000;

    // launched with a video path (TWiLight Menu++ etc.): play it directly.
    // launched without: standalone mode, the built-in browser is the entry
    // point and B during playback returns to it.
    const char* filePath = NULL;
    if (argc >= 2)
        filePath = argv[1];
    sStandalone = (filePath == NULL);

    strncpy(sBrowserDir, isDSiMode() ? "sd:/" : "fat:/", sizeof(sBrowserDir) - 1);
    sBrowserDir[sizeof(sBrowserDir) - 1] = 0;

    bool quit = false;
    while (!quit)
    {
        if (filePath)
        {
            if (loadAndStartVideo(filePath))
                quit = RunPlayerLoop(sStandalone);
            else if (!sStandalone)
                quit = true; // could not load the (initial) video: nothing to do but wait
            filePath = NULL;
        }
        else
        {
            if (!sStandalone)
            {
                quit = true;
                break;
            }
            // position the cursor on the video that was just playing (if any)
            if (RunBrowser(sBrowserPick, sizeof(sBrowserPick), sBrowserDir, sizeof(sBrowserDir),
                           sCurPath[0] ? GetFileName(sCurPath) : NULL))
                filePath = sBrowserPick;
            else
                quit = true;
        }
    }

    DestroyCurrentPlayer();

    // hand control back to the launcher (TWiLight Menu++, nds-bootstrap, ...)
    exit(0);
}
