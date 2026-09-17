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

// Variables pour scanner la SD sans exploser la RAM
#define MAX_DIR_STACK 256
static char sDirStack[MAX_DIR_STACK][FV_MAX_PATH_LEN];
static fv_listdir_req_t sListReq ALIGN(32);
static fv_dir_entry_t sListEntries[256] ALIGN(32);

// Générateur pseudo-aléatoire léger pour éviter de surcharger la mémoire
static u32 getRand(u32 maxVal) {
    static u32 seed = 0;
    if (seed == 0) seed = GetDebounceTicks() ^ 0x55555555;
    seed = (1103515245 * seed + 12345);
    return (seed >> 16) % maxVal;
}

// Nouvelle fonction qui scanne TOUTE la carte SD depuis l'ARM9
static void switchToRandomVideoAll()
{
    // On ferme et détruit COMPLÈTEMENT le lecteur vidéo avant la recherche.
    // Cela libère la carte SD et empêche le crash/retour menu dû aux conflits d'accès.
    if (sPlayerController)
    {
        fv_pausePlayer(&sPlayer);
        delete sPlayerController;
        sPlayerController = NULL;
        fv_destroyPlayer(&sPlayer);
    }
    
    // Comme on a supprimé l'interface du lecteur, on affiche un message direct sur la console texte
    consoleClear();
    printf("\n\n\n\n    Recherche de videos sur\n    toute la carte SD...\n\n    Veuillez patienter...");
    swiWaitForVBlank();
    
    int count = 0;
    char selectedPath[FV_MAX_PATH_LEN];
    selectedPath[0] = '\0';
    
    int stackTop = 0;
    strncpy(sDirStack[stackTop++], isDSiMode() ? "sd:/" : "fat:/", FV_MAX_PATH_LEN - 1);
    
    int loops = 0;
    while (stackTop > 0) {
        
        // On laisse respirer la console tous les 5 dossiers
        // (Évite à 100% que nds-bootstrap ne panique avec son watchdog)
        if (++loops % 5 == 0) {
            swiWaitForVBlank();
        }

        char currentDir[FV_MAX_PATH_LEN];
        strncpy(currentDir, sDirStack[--stackTop], FV_MAX_PATH_LEN - 1);
        
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
            // Ignorer dossiers systèmes
            if (sListEntries[i].name[0] == '.') continue;
            if (strcasecmp(sListEntries[i].name, "System Volume Information") == 0) continue;
            if (strcasecmp(sListEntries[i].name, "_nds") == 0) continue;
            
            char fullPath[FV_MAX_PATH_LEN];
            int dirLen = strlen(currentDir);
            int nameLen = strlen(sListEntries[i].name);
            
            if (dirLen + nameLen + 2 >= FV_MAX_PATH_LEN) continue;
            
            strcpy(fullPath, currentDir);
            if (dirLen > 0 && fullPath[dirLen - 1] != '/') strcat(fullPath, "/");
            strcat(fullPath, sListEntries[i].name);
            
            if (sListEntries[i].isDir) {
                if (stackTop < MAX_DIR_STACK) {
                    strncpy(sDirStack[stackTop++], fullPath, FV_MAX_PATH_LEN - 1);
                }
            } else {
                if (nameLen > 3 && strcasecmp(sListEntries[i].name + nameLen - 3, ".fv") == 0) {
                    if (strcasecmp(fullPath, sCurPath) == 0) continue; 
                    
                    count++;
                    if (getRand(count) == 0) {
                        strncpy(selectedPath, fullPath, FV_MAX_PATH_LEN - 1);
                        selectedPath[FV_MAX_PATH_LEN - 1] = '\0';
                    }
                }
            }
        }
    }
    
    if (count > 0) {
        // La recherche est finie, on recrée et lance la nouvelle vidéo
        loadAndStartVideo(selectedPath);
    } else {
        // Si aucune autre vidéo n'a été trouvée sur la SD, on relance l'ancienne
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
                if (sStandalone) {
                    // Navigateur autonome : 0 -> 1 -> 0
                    sRandomMode = (sRandomMode == 0) ? 1 : 0;
                } else {
                    // TWiLight Menu++ : 0 -> 1 -> 2 -> 0
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
