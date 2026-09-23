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

// Récupération de la variable d'inversion des écrans depuis PlayerController.cpp
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
// --------------------------------

u32 GetDebounceTicks();

static void DestroyCurrentPlayer();

// Seed du LCG utilisé par le reservoir sampling en mode ALL. Persistante
// (au lieu d'être recalculée à chaque appel depuis GetDebounceTicks) pour
// éviter que deux appels rapprochés (vidéo courte, appuis rapides sur
// suivant) ne partent d'un état de timer quasi identique et ne retirent
// des séquences de tirages corrélées.
static u32 sShuffleSeed = 0;
static bool sShuffleSeedInit = false;

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
    // "ALL" et non "ON ALL" : les 3 états doivent tenir sur 3 caractères
    // pour que le pire cas ("ALEA:xxx  BOUCLE:OFF") reste <= MAX_MSG_CHARS
    // (20, voir PlayerView.h) et ne soit pas tronqué par RenderTextLine.
    if (sRandomMode == 2) randomStateStr = "ALL";
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

    // sCurPath/l'historique ne sont mis à jour qu'en cas de succès : sinon
    // un fv_initPlayer en échec pollue sCurPath avec un chemin jamais
    // réellement joué (et l'ajoute à l'historique), ce qui fausse les
    // exclusions "vidéo courante"/"déjà vue récemment" ailleurs dans le
    // fichier - particulièrement sensible en mode ALL où switchToRandomVideoAll()
    // peut désormais retenter loadAndStartVideo() plusieurs fois d'affilée.
    if (!fv_initPlayer(&sPlayer, path, sCanUseWram))
    {
        fv_destroyPlayer(&sPlayer);
        return false; // Échec du chargement
    }

    strncpy(sCurPath, path, sizeof(sCurPath) - 1);
    sCurPath[sizeof(sCurPath) - 1] = 0;
    AddToHistory(sCurPath);

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

#define MAX_DIR_STACK 256
static char sDirStack[MAX_DIR_STACK][FV_MAX_PATH_LEN];
static fv_listdir_req_t sListReq ALIGN(32);
static fv_dir_entry_t sListEntries[256] ALIGN(32);

// Index complet des .fv de la carte SD, construit une seule fois par
// lancement de l'app (au premier passage en mode ALL), puis gardé en RAM
// pour toute la session : plus aucun IPC_CMD_LIST_DIR au moment de choisir
// une vidéo, juste une lecture de tableau. Stockage en chemins concaténés
// (plutôt qu'un tableau [N][FV_MAX_PATH_LEN] qui gâcherait ~200 octets par
// entrée en moyenne) pour rester large sans peser sur la RAM ARM9.
#define VIDEO_INDEX_MAX_ENTRIES 4096
#define VIDEO_INDEX_BUF_SIZE    (256 * 1024)
static char sIndexBuf[VIDEO_INDEX_BUF_SIZE];
static u32  sIndexOffset[VIDEO_INDEX_MAX_ENTRIES];
static u32  sIndexCount = 0;
static u32  sIndexBufUsed = 0;
static bool sIndexBuilt = false;

// Bag mélangé (Fisher-Yates) sur les indices [0..sIndexCount) : tirage sans
// remise jusqu'à épuisement, puis remélange - même principe que le shuffle
// bag déjà utilisé côté ARM7 pour le mode aléatoire par dossier.
static u32 sShuffleBag[VIDEO_INDEX_MAX_ENTRIES];
static u32 sShuffleBagRemaining = 0;

static void RebuildShuffleBag()
{
    sShuffleBagRemaining = sIndexCount;
    for (u32 i = 0; i < sIndexCount; i++) sShuffleBag[i] = i;
    for (u32 i = sIndexCount; i > 1; i--) {
        sShuffleSeed = (1103515245 * sShuffleSeed + 12345);
        u32 j = (sShuffleSeed >> 16) % i;
        u32 tmp = sShuffleBag[i - 1];
        sShuffleBag[i - 1] = sShuffleBag[j];
        sShuffleBag[j] = tmp;
    }
}

// Parcours BFS exhaustif de la carte (plus de plafond de dossiers ni de
// profondeur : c'est un scan unique par session, pas un scan par pression
// de touche comme avant, donc son coût ponctuel est acceptable). Remplit
// sIndexBuf/sIndexOffset/sIndexCount. Retourne le nombre de dossiers dont
// IPC_CMD_LIST_DIR a échoué (utilisé pour décider un retry côté appelant).
static int BuildVideoIndex()
{
    sIndexCount = 0;
    sIndexBufUsed = 0;
    int failedDirs = 0;
    int truncatedDirs = 0;

    int queueHead = 0;
    int queueTail = 0;
    strncpy(sDirStack[queueTail], isDSiMode() ? "sd:/" : "fat:/", FV_MAX_PATH_LEN - 1);
    queueTail++;

    while (queueHead < queueTail) {
        char currentDir[FV_MAX_PATH_LEN];
        strncpy(currentDir, sDirStack[queueHead], FV_MAX_PATH_LEN - 1);
        currentDir[FV_MAX_PATH_LEN - 1] = '\0';
        queueHead++;

        strncpy(sListReq.path, currentDir, FV_MAX_PATH_LEN - 1);
        sListReq.path[FV_MAX_PATH_LEN - 1] = '\0';
        sListReq.entries = sListEntries;
        sListReq.maxEntries = 256;

        DC_FlushRange(&sListReq, sizeof(sListReq));
        DC_FlushRange(sListEntries, sizeof(sListEntries));

        fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(IPC_CMD_LIST_DIR, (u32)&sListReq));

        // Attente bloquante : le protocole IPC_CMD_LIST_DIR n'autorise
        // qu'une seule requête en vol à la fois (sListReq/sListEntries sont
        // des buffers partagés, la réponse ne porte aucun identifiant de
        // requête). Un timeout qui abandonnerait puis enverrait la requête
        // suivante désynchroniserait durablement le protocole : la réponse
        // tardive de l'ARM7 à la requête abandonnée serait alors lue comme
        // la réponse d'une requête ultérieure, avec des données de mauvais
        // dossier dans sListEntries -> crash quasi garanti en aval.
        while (!fifoCheckValue32(FIFO_USER_02)) {
            swiWaitForVBlank();
        }

        u32 ok = fifoGetValue32(FIFO_USER_02) & IPC_CMD_ARG_MASK;

        DC_InvalidateRange(&sListReq, sizeof(sListReq));
        DC_InvalidateRange(sListEntries, sizeof(sListEntries));

        if (!ok) { failedDirs++; continue; }
        if (sListReq.total > sListReq.count) truncatedDirs++;

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
                if (queueTail < MAX_DIR_STACK) {
                    strncpy(sDirStack[queueTail], fullPath, FV_MAX_PATH_LEN - 1);
                    queueTail++;
                }
            } else if (nameLen > 3 && strcasecmp(dName + nameLen - 3, ".fv") == 0) {
                size_t pathLen = strlen(fullPath);
                if (sIndexCount < VIDEO_INDEX_MAX_ENTRIES &&
                    sIndexBufUsed + pathLen + 1 <= VIDEO_INDEX_BUF_SIZE)
                {
                    memcpy(sIndexBuf + sIndexBufUsed, fullPath, pathLen + 1);
                    sIndexOffset[sIndexCount] = sIndexBufUsed;
                    sIndexBufUsed += (u32)(pathLen + 1);
                    sIndexCount++;
                }
            }
        }
    }

    return failedDirs;
}

static void switchToRandomVideoAll()
{
    // Sauvegarde AVANT loadAndStartVideo(), qui écrase sCurPath dès l'appel
    // (avant même de savoir si le chargement réussit). Sans ça, le filet de
    // sécurité plus bas rejoue le fichier en échec au lieu de l'ancienne vidéo.
    char prevPath[FV_MAX_PATH_LEN];
    strncpy(prevPath, sCurPath, FV_MAX_PATH_LEN - 1);
    prevPath[FV_MAX_PATH_LEN - 1] = '\0';

    DestroyCurrentPlayer();

    // SÉCURITÉ IPC : On vide les anciens messages bloqués pour éviter une désynchronisation
    while (fifoCheckValue32(FIFO_USER_02)) {
        fifoGetValue32(FIFO_USER_02);
    }

    // Petite pause pour laisser la SD fermer proprement le fichier vidéo
    for (int i = 0; i < 10; i++) swiWaitForVBlank();

    if (!sShuffleSeedInit) {
        sShuffleSeed = GetDebounceTicks() ^ 0x13579BDF;
        sShuffleSeedInit = true;
    }

    if (!sIndexBuilt) {
        // Réinitialise la console texte sur l'écran du bas : PlayerView::Initialize()
        // a repris BG0/BG1/BG2/sprites pour son propre affichage (fond, compteur de
        // temps, légende...) pendant la lecture. Sans ce nettoyage, l'écran
        // "Indexation..." s'afficherait par-dessus/mélangé à ces résidus.
        oamClear(&oamSub, 0, 128);
        REG_DISPCNT_SUB = MODE_0_2D;
        consoleInit(NULL, 2, BgType_Text4bpp, BgSize_T_256x256, 2, 1, false, true);
        REG_DISPCNT_SUB = MODE_0_2D | DISPLAY_BG2_ACTIVE;

        consoleClear();
        printf("\n\n\n\n    Indexation de la carte SD...\n\n    Veuillez patienter...");
        swiWaitForVBlank();

        int failedDirs = BuildVideoIndex();
        // Index vide ET au moins un dossier en échec IPC (pas juste "carte
        // sans .fv") -> probable glitch I/O transitoire, on retente une fois.
        if (sIndexCount == 0 && failedDirs > 0) {
            for (int i = 0; i < 10; i++) swiWaitForVBlank();
            BuildVideoIndex();
        }

        RebuildShuffleBag();
        // Construit une seule fois par lancement de l'app : un échec I/O
        // persistant ou une bibliothèque vide ne redéclenchera pas de
        // nouvelle tentative avant un redémarrage du logiciel.
        sIndexBuilt = true;

        consoleClear();
        printf("\n\n\n\n    %lu videos indexees.", (unsigned long)sIndexCount);
        swiWaitForVBlank();
        for (int i = 0; i < 30; i++) swiWaitForVBlank();
        consoleClear();
    }

    bool loadSuccess = false;

    if (sIndexCount > 0) {
        // Pioche dans le bag en sautant la vidéo courante et l'historique
        // récent, et RETENTE avec un autre candidat si le chargement échoue
        // (fichier illisible, glitch I/O ponctuel) - sans ce retry, un seul
        // échec de chargement faisait retomber sur prevPath juste en dessous,
        // ce qui donnait l'impression que "next" relançait la même vidéo.
        // Jamais plus d'un tour complet du bag pour rester borné si la
        // bibliothèque est petite et très couverte par l'historique.
        u32 attemptsLeft = sIndexCount;
        while (attemptsLeft-- > 0 && !loadSuccess) {
            if (sShuffleBagRemaining == 0)
                RebuildShuffleBag();
            u32 idx = sShuffleBag[--sShuffleBagRemaining];
            const char* candidate = sIndexBuf + sIndexOffset[idx];
            if (strcasecmp(candidate, sCurPath) == 0) continue;
            if (IsInHistory(candidate)) continue;

            char selected[FV_MAX_PATH_LEN];
            strncpy(selected, candidate, FV_MAX_PATH_LEN - 1);
            selected[FV_MAX_PATH_LEN - 1] = '\0';
            loadSuccess = loadAndStartVideo(selected);
        }
    }

    // SÉCURITÉ DE SECOURS : rien trouvé ou échec du chargement -> relance l'ancienne vidéo
    if (!loadSuccess) {
        if (sIndexCount == 0) sHistoryCount = 0; // vide l'historique s'il est plein
        if (prevPath[0]) loadAndStartVideo(prevPath);
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
    // --- NOUVEAUTÉ : On force les écrans dans le bon sens (vidéo en haut, menu en bas) ---
    gScreenSwapped = false;
    lcdMainOnTop();
    // -----------------------------------------------------------------------------------

    PlayerController::RestoreSubScreen();
    BrowserController browser;
    if (!browser.OpenDir(sBrowserDir))
        return 0;

    browser.SetModes(sLoopEnabled, sRandomMode);
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
                browser.SetModes(sLoopEnabled, sRandomMode);
                break;

            case BrowserController::ACT_TOGGLE_RANDOM:
                sRandomMode = (sRandomMode == 0) ? 1 : 0; 
                browser.SetModes(sLoopEnabled, sRandomMode);
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
