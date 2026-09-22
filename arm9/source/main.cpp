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

static char sAdjacentPath[FV_MAX_PATH_LEN] ALIGN(32);
static char sCurPath[FV_MAX_PATH_LEN];

static bool sCanUseWram;
static bool sLoopEnabled = false;

// 0: OFF, 1: ON (Dossier), 2: ON ALL (Toute la SD)
static int sRandomMode = 0; 

static bool sStandalone = false;
static char sBrowserDir[FV_MAX_PATH_LEN];
static char sBrowserPick[FV_MAX_PATH_LEN];

extern bool gScreenSwapped;

// --- GESTION DE L'HISTORIQUE ---
#define HISTORY_SIZE 20
static char sHistory[HISTORY_SIZE][FV_MAX_PATH_LEN];
static int sHistoryCount = 0;
static int sHistoryIdx = 0;

static void AddToHistory(const char* path) {
    strncpy(sHistory[sHistoryIdx], path, FV_MAX_PATH_LEN - 1);
    sHistory[sHistoryIdx][FV_MAX_PATH_LEN - 1] = '\0';
    sHistoryIdx = (sHistoryIdx + 1) % HISTORY_SIZE;
    if (sHistoryCount < HISTORY_SIZE) sHistoryCount++;
}

static bool IsInHistory(const char* path) {
    for (int i = 0; i < sHistoryCount; i++) {
        if (strcasecmp(sHistory[i], path) == 0) return true;
    }
    return false;
}

// --- GESTION DU CACHE GLOBAL ---
#define MAX_GLOBAL_VIDEOS 1000 
static char (*sGlobalVideoCache)[FV_MAX_PATH_LEN] = NULL;
static int sGlobalVideoCount = 0;
static bool sGlobalCacheBuilt = false;

#define MAX_DIR_STACK 256
static char sDirStack[MAX_DIR_STACK][FV_MAX_PATH_LEN];
static int sDirDepth[MAX_DIR_STACK]; 
static fv_listdir_req_t sListReq ALIGN(32);
static fv_dir_entry_t sListEntries[256] ALIGN(32);

static void BuildGlobalVideoCache()
{
    if (sGlobalVideoCache == NULL) {
        sGlobalVideoCache = (char(*)[FV_MAX_PATH_LEN])malloc(MAX_GLOBAL_VIDEOS * FV_MAX_PATH_LEN);
    }
    if (!sGlobalVideoCache) return; 
    
    sGlobalVideoCount = 0;

    int stackTop = 0;
    strncpy(sDirStack[stackTop], isDSiMode() ? "sd:/" : "fat:/", FV_MAX_PATH_LEN - 1);
    sDirDepth[stackTop] = 0; 
    stackTop++;
    
    int maxFoldersToScan = 350; 

    while (stackTop > 0 && maxFoldersToScan > 0) {
        maxFoldersToScan--;
        stackTop--;
        
        char currentDir[FV_MAX_PATH_LEN];
        strncpy(currentDir, sDirStack[stackTop], FV_MAX_PATH_LEN - 1);
        currentDir[FV_MAX_PATH_LEN - 1] = '\0';
        int currentDepth = sDirDepth[stackTop];
        
        strncpy(sListReq.path, currentDir, FV_MAX_PATH_LEN - 1);
        sListReq.path[FV_MAX_PATH_LEN - 1] = '\0';
        sListReq.entries = sListEntries;
        sListReq.maxEntries = 256; 
        
        DC_FlushRange(&sListReq, sizeof(sListReq));
        DC_FlushRange(sListEntries, sizeof(sListEntries));
        
        fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(IPC_CMD_LIST_DIR, (u32)&sListReq));
        
        while (!fifoCheckValue32(FIFO_USER_02)) {
            swiWaitForVBlank(); 
        }
        
        u32 ok = fifoGetValue32(FIFO_USER_02) & IPC_CMD_ARG_MASK;
        
        DC_InvalidateRange(&sListReq, sizeof(sListReq));
        DC_InvalidateRange(sListEntries, sizeof(sListEntries));
        
        if (!ok) continue;
        
        for (u32 i = 0; i < sListReq.count; i++) {
            const char* dName = sListEntries[i].name;
            
            if (dName[0] == '.') continue;
            if (strcasecmp(dName, "System Volume Information") == 0) continue;
            if (strcasecmp(dName, "_nds") == 0) continue;
            if (strcasecmp(dName, "Nintendo 3DS") == 0) continue;
            if (strcasecmp(dName, "Nintendo DSi") == 0) continue;
            if (strcasecmp(dName, "TWiLightMenu") == 0) continue;
            if (strcasecmp(dName, "luma") == 0) continue;
            if (strcasecmp(dName, "DCIM") == 0) continue;
            
            char fullPath[FV_MAX_PATH_LEN];
            int dirLen = strlen(currentDir);
            int nameLen = strlen(dName);
            
            if (dirLen + nameLen + 2 >= FV_MAX_PATH_LEN) continue;
            
            strcpy(fullPath, currentDir);
            if (dirLen > 0 && fullPath[dirLen - 1] != '/') strcat(fullPath, "/");
            strcat(fullPath, dName);
            
            if (sListEntries[i].isDir) {
                if (stackTop < MAX_DIR_STACK && currentDepth < 4) {
                    strncpy(sDirStack[stackTop], fullPath, FV_MAX_PATH_LEN - 1);
                    sDirDepth[stackTop] = currentDepth + 1;
                    stackTop++;
                }
            } else {
                if (nameLen > 3 && strcasecmp(dName + nameLen - 3, ".fv") == 0) {
                    if (sGlobalVideoCount < MAX_GLOBAL_VIDEOS) {
                        strncpy(sGlobalVideoCache[sGlobalVideoCount], fullPath, FV_MAX_PATH_LEN - 1);
                        sGlobalVideoCache[sGlobalVideoCount][FV_MAX_PATH_LEN - 1] = '\0';
                        
                        sGlobalVideoCount++;
                        
                        // ANIMATION DU COMPTEUR : On se place sur la ligne 10, colonne 1 pour mettre à jour
                        printf("\x1b[10;1H    %d videos indexees", sGlobalVideoCount);
                    }
                }
            }
        }
    }
    sGlobalCacheBuilt = true;
}
// ----------------------------------------------------------------

u32 GetDebounceTicks();

static const char* GetFileName(const char* path)
{
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void ShowVideoMessage()
{
    if (!sPlayerController)
        return;

    char displayName[64];
    strncpy(displayName, GetFileName(sCurPath), sizeof(displayName) - 1);
    displayName[sizeof(displayName) - 1] = 0;
    size_t len = strlen(displayName);
    if (len > 3 && strcasecmp(displayName + len - 3, ".fv") == 0)
        displayName[len - 3] = 0;

    char line2[32];
    const char* randomStateStr;
    if (sRandomMode == 2) randomStateStr = "ON ALL";
    else if (sRandomMode == 1) randomStateStr = "ON ";
    else randomStateStr = "OFF";

    snprintf(line2, sizeof(line2), "ALEA:%s  BOUCLE:%s", randomStateStr, sLoopEnabled ? "ON" : "OFF");
    sPlayerController->ShowMessage(displayName, line2);
}

static bool loadAndStartVideo(const char* path)
{
    if (sPlayerController)
    {
        fv_pausePlayer(&sPlayer);
        delete sPlayerController;
        sPlayerController = NULL;
        fv_destroyPlayer(&sPlayer);
    }

    strncpy(sCurPath, path, sizeof(sCurPath) - 1);
    sCurPath[sizeof(sCurPath) - 1] = 0;

    AddToHistory(sCurPath);

    if (!fv_initPlayer(&sPlayer, sCurPath, sCanUseWram))
    {
        fv_destroyPlayer(&sPlayer);
        return false;
    }

    sPlayerController = new PlayerController(&sPlayer);
    sPlayerController->Initialize();
    fv_startPlayer(&sPlayer);
    return true;
}

static void switchToAdjacentVideo(bool next)
{
    fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(next ? IPC_CMD_FIND_NEXT_FILE : IPC_CMD_FIND_PREV_FILE,
                                               (u32)sAdjacentPath));
    fifoWaitValue32(FIFO_USER_02);
    u32 found = fifoGetValue32(FIFO_USER_02) & IPC_CMD_ARG_MASK;
    if (!found) return;

    DC_InvalidateRange(sAdjacentPath, sizeof(sAdjacentPath));
    loadAndStartVideo(sAdjacentPath);
}

static void switchToRandomVideo()
{
    fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(IPC_CMD_FIND_RANDOM_FILE, (u32)sAdjacentPath));
    fifoWaitValue32(FIFO_USER_02);
    u32 found = fifoGetValue32(FIFO_USER_02) & IPC_CMD_ARG_MASK;
    if (!found) return;

    DC_InvalidateRange(sAdjacentPath, sizeof(sAdjacentPath));
    loadAndStartVideo(sAdjacentPath);
}

static void switchToRandomVideoAll()
{
    if (sPlayerController)
    {
        fv_pausePlayer(&sPlayer);
        delete sPlayerController;
        sPlayerController = NULL;
        fv_destroyPlayer(&sPlayer);
    }
    
    while (fifoCheckValue32(FIFO_USER_02)) {
        fifoGetValue32(FIFO_USER_02);
    }
    
    for (int i = 0; i < 10; i++) swiWaitForVBlank();
    
    // Si le cache n'a pas encore été construit, on l'affiche
    if (!sGlobalCacheBuilt) {
        consoleClear();
        // Le texte initial s'affiche, le "0 videos indexees" se trouve sur la 10ème ligne
        printf("\n\n\n\n    Creation de l'index\n    des videos SD...\n\n    Veuillez patienter !\n\n    0 videos indexees");
        swiWaitForVBlank();
        
        BuildGlobalVideoCache();
        
        // Petite pause d'une demi-seconde à la fin pour voir le score final !
        for (int i = 0; i < 30; i++) swiWaitForVBlank();
        
        consoleClear();
    }
    
    bool loadSuccess = false;
    
    if (sGlobalVideoCount > 0) {
        u32 seed = GetDebounceTicks() ^ 0x13579BDF;
        
        int attempts = 0;
        char selectedPath[FV_MAX_PATH_LEN];
        selectedPath[0] = '\0';
        
        while (attempts < 50) {
            seed = (1103515245 * seed + 12345);
            int randIdx = (seed >> 16) % sGlobalVideoCount;
            
            if (strcasecmp(sGlobalVideoCache[randIdx], sCurPath) != 0 &&
                !IsInHistory(sGlobalVideoCache[randIdx])) {
                strncpy(selectedPath, sGlobalVideoCache[randIdx], FV_MAX_PATH_LEN - 1);
                break;
            }
            attempts++;
        }
        
        if (selectedPath[0] != '\0') {
            loadSuccess = loadAndStartVideo(selectedPath);
        }
    } 
    
    if (!loadSuccess) {
        if (sGlobalVideoCount == 0) sHistoryCount = 0; 
        if (sCurPath[0]) loadAndStartVideo(sCurPath);
    }
}

static void DestroyCurrentPlayer()
{
    if (sPlayerController)
    {
        fv_pausePlayer(&sPlayer);
        delete sPlayerController;
        sPlayerController = NULL;
        fv_destroyPlayer(&sPlayer);
    }
}

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
    if (strchr(out, ':') && !strchr(out, '/'))
    {
        if (len + 2 < outMax)
        {
            out[len] = '/';
            out[len + 1] = 0;
        }
    }
}

static int RunBrowser(char* outPath, size_t outPathMax, char* outDir, size_t outDirMax, const char* selectName)
{
    gScreenSwapped = false;
    lcdMainOnTop();

    PlayerController::RestoreSubScreen();
    BrowserController browser;
    if (!browser.OpenDir(sBrowserDir))
        return 0;

    browser.SetModes(sLoopEnabled, sRandomMode > 0);
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

            case BrowserController::ACT_TOGGLE_LOOP:
                sLoopEnabled = !sLoopEnabled;
                browser.SetModes(sLoopEnabled, sRandomMode > 0);
                break;

            case BrowserController::ACT_TOGGLE_RANDOM:
                sRandomMode = (sRandomMode == 0) ? 1 : 0; 
                browser.SetModes(sLoopEnabled, sRandomMode > 0);
                break;

            default:
                break;
        }
    }
}

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
                if (sRandomMode == 2) switchToRandomVideoAll();
                else if (sRandomMode == 1) switchToRandomVideo();
                else switchToAdjacentVideo(true);
                break;

            case PlayerController::NAV_ACTION_PREV:
                if (sRandomMode == 2) switchToRandomVideoAll();
                else if (sRandomMode == 1) switchToRandomVideo();
                else switchToAdjacentVideo(false);
                break;

            case PlayerController::NAV_ACTION_VIDEO_ENDED:
                if (sLoopEnabled) loadAndStartVideo(sCurPath);
                else if (sRandomMode == 2) switchToRandomVideoAll();
                else if (sRandomMode == 1) switchToRandomVideo();
                else switchToAdjacentVideo(true);
                break;

            case PlayerController::NAV_ACTION_TOGGLE_LOOP:
                sLoopEnabled = !sLoopEnabled;
                ShowVideoMessage();
                break;

            case PlayerController::NAV_ACTION_TOGGLE_RANDOM:
                if (sStandalone || !isDSiMode()) {
                    sRandomMode = (sRandomMode == 0) ? 1 : 0;
                } else {
                    sRandomMode++;
                    if (sRandomMode > 2) sRandomMode = 0;
                }
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

static void InitDebounceTimer()
{
    TIMER0_DATA = 0;
    TIMER0_CR = TIMER_DIV_1024 | TIMER_ENABLE;
    TIMER1_DATA = 0;
    TIMER1_CR = TIMER_CASCADE | TIMER_ENABLE;
}

u32 GetDebounceTicks()
{
    return ((u32)TIMER1_DATA << 16) | (u32)TIMER0_DATA;
}

int main(int argc, char** argv)
{
    DC_FlushAll();

    mpu_enableVramCache();
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

    fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_HANDSHAKE, 0));
    fifoWaitValue32(FIFO_USER_01);
    u32 handShake = fifoGetValue32(FIFO_USER_01);

    if (canUseWram && (handShake & IPC_CMD_ARG_MASK) == 0)
        canUseWram = false;

    sCanUseWram = canUseWram;

    if (!isDSiMode())
    {
        DC_FlushRange(gDldiStub, 16 * 1024);
        fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_SETUP_DLDI, (u32)gDldiStub));
        fifoWaitValue32(FIFO_USER_01);
        fifoGetValue32(FIFO_USER_01);
    }

    videoSetModeSub(MODE_0_2D);
    vramSetBankH(VRAM_H_SUB_BG);
    vramSetBankI(VRAM_I_SUB_SPRITE);

    consoleInit(NULL, 2, BgType_Text4bpp, BgSize_T_256x256, 2, 1, false, true);

    vramSetBankA(VRAM_A_LCD);
    vramSetBankB(VRAM_B_LCD);
    vramSetBankC(VRAM_C_LCD);
    vramSetBankD(VRAM_D_LCD);
    vramSetBankE(VRAM_E_LCD);

    for (int i = 0; i < 3 * 128 * 1024; i += 4)
        *(vu32*)((u32)VRAM_A + i) = 0x80008000;

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
                quit = true; 
            filePath = NULL;
        }
        else
        {
            if (!sStandalone)
            {
                quit = true;
                break;
            }
            if (RunBrowser(sBrowserPick, sizeof(sBrowserPick), sBrowserDir, sizeof(sBrowserDir),
                           sCurPath[0] ? GetFileName(sCurPath) : NULL))
                filePath = sBrowserPick;
            else
                quit = true;
        }
    }

    DestroyCurrentPlayer();
    PlayerController::RestoreSubScreen();
    
    exit(0);
}
