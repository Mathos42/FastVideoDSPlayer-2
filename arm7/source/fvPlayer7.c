#include <nds.h>
#include <string.h>
#include <strings.h>
#include <nds/fifocommon.h>
#include "isdprint.h"
#include "../../common/ipc.h"
#include "fat.h"
#include "adpcm.h"
#include "irqWait.h"
#include "../../common/twlwram.h"
#include "fvPlayer7.h"
#include <stdlib.h>

#define FV_AUDIO_START_OFFSET 12
#define FV_AUDIO_CH_LEFT 1
#define FV_AUDIO_CH_RIGHT 3

static fv_player7_t sPlayer;
extern volatile u32 gFrameCounter; // défini dans main.c, incrémenté à chaque VBlank

static void handleFindFile(u32 value, void *userdata);

void fv_init(void)
{
    memset(&sPlayer, 0, sizeof(sPlayer));
    fifoSetValue32Handler(FIFO_USER_02, handleFindFile, NULL);
}

static inline int getAudioTimerValue(int rate)
{
    return (16756991 + ((rate + 1) >> 1)) / rate;
}

static void audioFrameIrq(void)
{
}

static void decodeAudioFrame(void)
{
    adpcm_decompress(sPlayer.audioQueueL[sPlayer.queueReadPtr], FV_AUDIO_FRAME_SIZE,
                     sPlayer.audioRingL[sPlayer.ringPos]);
    adpcm_decompress(sPlayer.audioQueueR[sPlayer.queueReadPtr], FV_AUDIO_FRAME_SIZE,
                     sPlayer.audioRingR[sPlayer.ringPos]);
    if (++sPlayer.ringPos == FV_AUDIO_RING_FRAMES)
        sPlayer.ringPos = 0;
    if (++sPlayer.queueReadPtr == FV_AUDIO_QUEUE_FRAMES)
        sPlayer.queueReadPtr = 0;
    sPlayer.queueFrameCount--;
    sPlayer.audioFramesNeeded--;
    sPlayer.audioFramesProvided++;
}

static void startAudio(void)
{
    if (sPlayer.audioStarted)
        return;
    sPlayer.ringPos = 0;
    sPlayer.audioFramesNeeded = 0;
    sPlayer.audioFramesProvided = 0;
    for (int i = 0; i < FV_AUDIO_START_OFFSET; i++) {
        if (sPlayer.queueFrameCount == 0)
            break;
        decodeAudioFrame();
    }
    sPlayer.audioFramesProvided -= FV_AUDIO_START_OFFSET;
    int tmr = getAudioTimerValue(FV_AUDIO_RATE);
    SCHANNEL_SOURCE(FV_AUDIO_CH_LEFT) = (u32)sPlayer.audioRingL;
    SCHANNEL_REPEAT_POINT(FV_AUDIO_CH_LEFT) = 0;
    SCHANNEL_LENGTH(FV_AUDIO_CH_LEFT) = sizeof(sPlayer.audioRingL) >> 2;
    SCHANNEL_TIMER(FV_AUDIO_CH_LEFT) = -tmr;
    SCHANNEL_SOURCE(FV_AUDIO_CH_RIGHT) = (u32)sPlayer.audioRingR;
    SCHANNEL_REPEAT_POINT(FV_AUDIO_CH_RIGHT) = 0;
    SCHANNEL_LENGTH(FV_AUDIO_CH_RIGHT) = sizeof(sPlayer.audioRingR) >> 2;
    SCHANNEL_TIMER(FV_AUDIO_CH_RIGHT) = -tmr;
    TIMER_CR(0) = 0;
    TIMER_CR(1) = 0;
    TIMER_CR(2) = 0;
    TIMER_CR(3) = 0;
    TIMER_DATA(0) = -2;
    TIMER_DATA(1) = -tmr;
    TIMER_DATA(2) = -256;
    TIMER_DATA(3) = 0;
    TIMER_CR(3) = TIMER_CASCADE | TIMER_ENABLE;
    TIMER_CR(2) = TIMER_CASCADE | TIMER_ENABLE | TIMER_IRQ_REQ;
    TIMER_CR(1) = TIMER_CASCADE | TIMER_ENABLE;
    irqSet(IRQ_TIMER2, audioFrameIrq);
    irqEnable(IRQ_TIMER2);
    SCHANNEL_CR(FV_AUDIO_CH_LEFT) = SCHANNEL_ENABLE | SOUND_VOL(0x7F) | SOUND_PAN(0) | SOUND_FORMAT_16BIT | SOUND_REPEAT;
    SCHANNEL_CR(FV_AUDIO_CH_RIGHT) = SCHANNEL_ENABLE | SOUND_VOL(0x7F) | SOUND_PAN(0x7F) | SOUND_FORMAT_16BIT | SOUND_REPEAT;
    TIMER_CR(0) = TIMER_ENABLE;
    sPlayer.audioStarted = true;
}

static void stopAudio(void)
{
    if (!sPlayer.audioStarted)
        return;
    sPlayer.audioStarted = false;
    TIMER_CR(0) = 0;
    irqDisable(IRQ_TIMER2);
    SCHANNEL_CR(FV_AUDIO_CH_LEFT) = 0;
    SCHANNEL_CR(FV_AUDIO_CH_RIGHT) = 0;
    TIMER_CR(1) = 0;
    TIMER_CR(2) = 0;
    TIMER_CR(3) = 0;
    sPlayer.ringPos = 0;
    memset(&sPlayer.audioRingL[0][0], 0, sizeof(sPlayer.audioRingL));
    memset(&sPlayer.audioRingR[0][0], 0, sizeof(sPlayer.audioRingR));
}

static void updateAudio(void)
{
    if (!sPlayer.audioStarted)
        return;
    int audioBlocks = TIMER_DATA(3);
    int needed = audioBlocks - (sPlayer.audioFramesProvided & 0xFFFF);
    if (needed < 0)
        needed += 65536;
    while (needed > 0 && sPlayer.queueFrameCount > 0) {
        decodeAudioFrame();
        needed--;
    }
}

static void gotoKeyFrameDirect(const fv_keyframe_t *keyFrameData)
{
    f_lseek(&sPlayer.file, keyFrameData->offset);
    stopAudio();
    sPlayer.ringPos = 0;
    sPlayer.queueReadPtr = 0;
    sPlayer.queueWritePtr = 0;
    sPlayer.queueFrameCount = 0;
    sPlayer.audioFramesNeeded = 0;
    sPlayer.audioFramesProvided = 0;
    memset(&sPlayer.audioQueueL[0][0], 0, sizeof(sPlayer.audioQueueL));
    memset(&sPlayer.audioQueueR[0][0], 0, sizeof(sPlayer.audioQueueR));
}

static void readKeyFrame(u32 index, fv_keyframe_t *out)
{
    UINT br;
    f_lseek(&sPlayer.file, sizeof(fv_header_t) + sizeof(fv_keyframe_t) * index);
    f_read(&sPlayer.file, out, sizeof(fv_keyframe_t), &br);
}

static u32 gotoKeyFrame(u32 keyFrame)
{
    fv_keyframe_t keyFrameData;
    if (keyFrame >= sPlayer.nrKeyFrames)
        keyFrame = sPlayer.nrKeyFrames - 1;
    readKeyFrame(keyFrame, &keyFrameData);
    gotoKeyFrameDirect(&keyFrameData);
    return keyFrameData.frame;
}

static u32 gotoNearestKeyFrame(u32 frame, u32 *resultFrame)
{
    if (sPlayer.nrKeyFrames == 0) {
        if (resultFrame) *resultFrame = 0;
        return 0;
    }
    u32 lo = 0;
    u32 hi = sPlayer.nrKeyFrames;
    u32 best = 0;
    fv_keyframe_t kf;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        readKeyFrame(mid, &kf);
        if (kf.frame <= frame) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    readKeyFrame(best, &kf);
    gotoKeyFrameDirect(&kf);
    if (resultFrame)
        *resultFrame = kf.frame;
    return best;
}

static void rememberCurPath(const char *path)
{
    const char *lastSlash = strrchr(path, '/');
    if (lastSlash) {
        int dirLen = lastSlash - path;
        if (dirLen >= (int)sizeof(sPlayer.curDir))
            dirLen = sizeof(sPlayer.curDir) - 1;
        memcpy(sPlayer.curDir, path, dirLen);
        sPlayer.curDir[dirLen] = 0;
        strncpy(sPlayer.curName, lastSlash + 1, sizeof(sPlayer.curName) - 1);
        sPlayer.curName[sizeof(sPlayer.curName) - 1] = 0;
    } else {
        sPlayer.curDir[0] = 0;
        strncpy(sPlayer.curName, path, sizeof(sPlayer.curName) - 1);
        sPlayer.curName[sizeof(sPlayer.curName) - 1] = 0;
    }
}

static bool hasFvExtension(const char *name)
{
    int len = strlen(name);
    return len > 3 && strcasecmp(name + len - 3, ".fv") == 0;
}

// Lists a directory for the standalone browser: fills req->entries with up
// to req->maxEntries entries (directories and .fv files only, dotfiles
// skipped). Returns 1 if the directory could be opened, 0 otherwise.
// Sorting is done on the arm9 side (qsort), the arm7 only fills the buffer.
static u32 listDirInto(fv_listdir_req_t* req)
{
    DIR dir;
    FILINFO info;

    req->count = 0;
    req->total = 0;

    if (f_opendir(&dir, req->path) != FR_OK)
        return 0;

    while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0)
    {
        int isDir = (info.fattrib & AM_DIR) != 0;
        if (info.fname[0] == '.')
            continue;
        if (!isDir && !hasFvExtension(info.fname))
            continue;

        req->total++;
        if (req->count < req->maxEntries)
        {
            strncpy(req->entries[req->count].name, info.fname, FV_BROWSER_NAME_LEN - 1);
            req->entries[req->count].name[FV_BROWSER_NAME_LEN - 1] = 0;
            req->entries[req->count].isDir = isDir ? 1 : 0;
            req->count++;
        }
    }
    f_closedir(&dir);
    return 1;
}

static void joinCurDirAndName(const char *name, char *outPath)
{
    if (sPlayer.curDir[0]) {
        size_t dirLen = strlen(sPlayer.curDir);
        if (dirLen > FV_MAX_PATH_LEN - 2)
            dirLen = FV_MAX_PATH_LEN - 2;
        memcpy(outPath, sPlayer.curDir, dirLen);
        outPath[dirLen] = '/';
        size_t avail = FV_MAX_PATH_LEN - dirLen - 2;
        size_t nameLen = strlen(name);
        if (nameLen > avail)
            nameLen = avail;
        memcpy(outPath + dirLen + 1, name, nameLen);
        outPath[dirLen + 1 + nameLen] = 0;
    } else {
        strncpy(outPath, name, FV_MAX_PATH_LEN - 1);
        outPath[FV_MAX_PATH_LEN - 1] = 0;
    }
}

static bool findAdjacentFvFile(int direction, char *outPath)
{
    DIR dir;
    FILINFO info;
    static char best[FV_MAX_PATH_LEN];
    static char edge[FV_MAX_PATH_LEN];
    bool haveBest = false;
    bool haveEdge = false;
    const char *dirPath = sPlayer.curDir[0] ? sPlayer.curDir : ".";
    if (f_opendir(&dir, dirPath) != FR_OK)
        return false;
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0) {
        if (info.fattrib & AM_DIR)
            continue;
        if (!hasFvExtension(info.fname))
            continue;
        int cmpToCur = strcasecmp(info.fname, sPlayer.curName);
        if (direction > 0) {
            if (cmpToCur > 0 && (!haveBest || strcasecmp(info.fname, best) < 0)) {
                strncpy(best, info.fname, sizeof(best) - 1);
                best[sizeof(best) - 1] = 0;
                haveBest = true;
            }
            if (!haveEdge || strcasecmp(info.fname, edge) < 0) {
                strncpy(edge, info.fname, sizeof(edge) - 1);
                edge[sizeof(edge) - 1] = 0;
                haveEdge = true;
            }
        } else {
            if (cmpToCur < 0 && (!haveBest || strcasecmp(info.fname, best) > 0)) {
                strncpy(best, info.fname, sizeof(best) - 1);
                best[sizeof(best) - 1] = 0;
                haveBest = true;
            }
            if (!haveEdge || strcasecmp(info.fname, edge) > 0) {
                strncpy(edge, info.fname, sizeof(edge) - 1);
                edge[sizeof(edge) - 1] = 0;
                haveEdge = true;
            }
        }
    }
    f_closedir(&dir);
    const char *chosen = haveBest ? best : (haveEdge ? edge : NULL);
    if (!chosen || strcasecmp(chosen, sPlayer.curName) == 0)
        return false;
    joinCurDirAndName(chosen, outPath);
    return true;
}

static u32 hash32(u32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// NOUVEAU : Construit le "bag" d'indices mélangés
static void buildShuffleBag(void)
{
    DIR dir;
    FILINFO info;
    const char *dirPath = sPlayer.curDir[0] ? sPlayer.curDir : ".";
    
    // Passage 1 : compter les fichiers éligibles (hors vidéo en cours)
    if (f_opendir(&dir, dirPath) != FR_OK) {
        sPlayer.shuffleRemaining = 0;
        return;
    }
    
    u32 count = 0;
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0) {
        if (info.fattrib & AM_DIR) continue;
        if (!hasFvExtension(info.fname)) continue;
        // EXCLUSION CRITIQUE : empêche la dernière vidéo lue d'être dans le nouveau bag
        if (strcasecmp(info.fname, sPlayer.curName) == 0) continue;
        count++;
    }
    f_closedir(&dir);
    
    if (count == 0) {
        sPlayer.shuffleCount = 0;
        sPlayer.shuffleRemaining = 0;
        return;
    }
    
    sPlayer.shuffleCount = (count > MAX_SHUFFLE_FILES) ? MAX_SHUFFLE_FILES : (u16)count;
    
    // Passage 2 : initialiser les indices et mélanger (Fisher-Yates)
    for (u16 i = 0; i < sPlayer.shuffleCount; i++) {
        sPlayer.shuffleIndices[i] = i;
    }
    
    for (u16 i = sPlayer.shuffleCount - 1; i > 0; i--) {
        u32 seed = hash32(gFrameCounter + i + sPlayer.shuffleCount);
        u16 j = seed % (i + 1);
        u16 temp = sPlayer.shuffleIndices[i];
        sPlayer.shuffleIndices[i] = sPlayer.shuffleIndices[j];
        sPlayer.shuffleIndices[j] = temp;
    }
    
    sPlayer.shuffleRemaining = sPlayer.shuffleCount;
    strncpy(sPlayer.shuffleDir, sPlayer.curDir, FV_MAX_PATH_LEN - 1);
    sPlayer.shuffleDir[FV_MAX_PATH_LEN - 1] = 0;
    // Fige le nom exclu utilisé pour compter/indexer ci-dessus : curName va
    // changer à chaque tirage suivant, mais l'énumération du dossier doit
    // rester identique pendant toute la durée de vie de ce bag
    strncpy(sPlayer.shuffleExcludedName, sPlayer.curName, FV_MAX_PATH_LEN - 1);
    sPlayer.shuffleExcludedName[FV_MAX_PATH_LEN - 1] = 0;
}

// MODIFIÉ : Utilise le bag au lieu d'un tirage avec remise
static bool findRandomFvFile(char *outPath)
{
    // Reconstruire le bag si le dossier a changé ou s'il est épuisé
    if (sPlayer.shuffleRemaining == 0 || strcmp(sPlayer.shuffleDir, sPlayer.curDir) != 0) {
        buildShuffleBag();
    }
    
    if (sPlayer.shuffleRemaining == 0) {
        return false;
    }
    
    // Piocher l'indice cible dans le bag mélangé (de la fin vers le début)
    u16 targetIdx = sPlayer.shuffleIndices[sPlayer.shuffleRemaining - 1];
    sPlayer.shuffleRemaining--;
    
    // Passage 3 : retrouver le fichier correspondant à l'indice cible
    DIR dir;
    FILINFO info;
    const char *dirPath = sPlayer.curDir[0] ? sPlayer.curDir : ".";
    
    if (f_opendir(&dir, dirPath) != FR_OK) return false;
    
    bool found = false;
    u16 currentIdx = 0;
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0) {
        if (info.fattrib & AM_DIR) continue;
        if (!hasFvExtension(info.fname)) continue;
        if (strcasecmp(info.fname, sPlayer.shuffleExcludedName) == 0) continue;
        
        if (currentIdx == targetIdx) {
            joinCurDirAndName(info.fname, outPath);
            found = true;
            break;
        }
        currentIdx++;
    }
    f_closedir(&dir);
    
    return found;
}

static void handleFindFile(u32 value, void *userdata)
{
    switch (value >> IPC_CMD_CMD_SHIFT) {
        case IPC_CMD_FIND_NEXT_FILE: {
            char *outPath = (char *)(value & IPC_CMD_ARG_MASK);
            bool found = findAdjacentFvFile(1, outPath);
            fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(IPC_CMD_FIND_NEXT_FILE, found ? 1 : 0));
            break;
        }
        case IPC_CMD_FIND_PREV_FILE: {
            char *outPath = (char *)(value & IPC_CMD_ARG_MASK);
            bool found = findAdjacentFvFile(-1, outPath);
            fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(IPC_CMD_FIND_PREV_FILE, found ? 1 : 0));
            break;
        }
        case IPC_CMD_FIND_RANDOM_FILE: {
            char *outPath = (char *)(value & IPC_CMD_ARG_MASK);
            bool found = findRandomFvFile(outPath);
            fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(IPC_CMD_FIND_RANDOM_FILE, found ? 1 : 0));
            break;
        }
        case IPC_CMD_LIST_DIR:
        {
            fv_listdir_req_t* req = (fv_listdir_req_t*)(value & IPC_CMD_ARG_MASK);
            u32 ok = listDirInto(req);
            fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(IPC_CMD_LIST_DIR, ok));
            break;
        }
    }
}

static void handleFifo(u32 value)
{
    UINT br;
    switch (value >> IPC_CMD_CMD_SHIFT) {
        case IPC_CMD_READ_FRAME: {
            u32 len;
            if (f_read(&sPlayer.file, &len, 4, &br) != FR_OK || br != 4) {
                fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_READ_FRAME, 0));
                break;
            }
            f_read(&sPlayer.file, (void *)(value & IPC_CMD_ARG_MASK), len & 0x1FFFF, &br);
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_READ_FRAME, len & 0x1FFFF));
            u32 audioFrames = len >> 17;
            for (int i = 0; i < audioFrames; i++) {
                f_read(&sPlayer.file, sPlayer.audioQueueL[sPlayer.queueWritePtr], FV_AUDIO_FRAME_SIZE, &br);
                f_read(&sPlayer.file, sPlayer.audioQueueR[sPlayer.queueWritePtr], FV_AUDIO_FRAME_SIZE, &br);
                if (++sPlayer.queueWritePtr == FV_AUDIO_QUEUE_FRAMES)
                    sPlayer.queueWritePtr = 0;
                sPlayer.queueFrameCount++;
            }
            break;
        }
        case IPC_CMD_OPEN_FILE: {
            const char *path = (const char *)(value & IPC_CMD_ARG_MASK);
            f_close(&sPlayer.file);
            FRESULT result = f_open(&sPlayer.file, path, FA_OPEN_EXISTING | FA_READ);
            if (result != FR_OK)
                fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_OPEN_FILE, 0));
            else {
                rememberCurPath(path);
                fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_OPEN_FILE, 1));
            }
            break;
        }
        case IPC_CMD_READ_HEADER: {
            fv_header_t *header = (fv_header_t *)(value & IPC_CMD_ARG_MASK);
            f_read(&sPlayer.file, header, sizeof(fv_header_t), &br);
            sPlayer.nrKeyFrames = header->nrKeyFrames;
            gotoKeyFrame(0);
            u32 num = header->fpsNum;
            u32 den = header->fpsDen;
            int vblankCount = 1;
            while (num * (vblankCount + 1) / den < 62)
                vblankCount++;
            fpsa_stop(&sPlayer.fpsa);
            if (num * vblankCount / den < 62) {
                fpsa_init(&sPlayer.fpsa);
                fpsa_setTargetFpsFraction(&sPlayer.fpsa, num * vblankCount, den);
                sPlayer.fpsa.targetCycles = (double)sPlayer.fpsa.targetCycles * getAudioTimerValue(FV_AUDIO_RATE) * FV_AUDIO_RATE / 16756991.0;
                fpsa_start(&sPlayer.fpsa);
            }
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_READ_HEADER, vblankCount));
            break;
        }
        case IPC_CMD_GOTO_KEYFRAME: {
            u32 frame = gotoKeyFrame(value & IPC_CMD_ARG_MASK);
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_GOTO_KEYFRAME, frame));
            break;
        }
        case IPC_CMD_GOTO_NEAREST_KEYFRAME: {
            u32 frame = value & IPC_CMD_ARG_MASK;
            u32 resultFrame;
            u32 keyFrame = gotoNearestKeyFrame(frame, &resultFrame);
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_GOTO_NEAREST_KEYFRAME, keyFrame));
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_GOTO_KEYFRAME, resultFrame));
            break;
        }
        case IPC_CMD_CONTROL_AUDIO: {
            if ((value & IPC_CMD_ARG_MASK) == IPC_ARG_CONTROL_AUDIO_START)
                startAudio();
            else if ((value & IPC_CMD_ARG_MASK) == IPC_ARG_CONTROL_AUDIO_STOP)
                stopAudio();
            else if ((value & IPC_CMD_ARG_MASK) == IPC_ARG_CONTROL_AUDIO_STOP_CLEAR) {
                stopAudio();
                sPlayer.ringPos = 0;
                sPlayer.queueReadPtr = 0;
                sPlayer.queueWritePtr = 0;
                sPlayer.queueFrameCount = 0;
                sPlayer.audioFramesNeeded = 0;
                sPlayer.audioFramesProvided = 0;
                memset(&sPlayer.audioQueueL[0][0], 0, sizeof(sPlayer.audioQueueL));
                memset(&sPlayer.audioQueueR[0][0], 0, sizeof(sPlayer.audioQueueR));
            }
            break;
        }
        case IPC_CMD_SETUP_DLDI: {
            if (!isDSiMode()) {
                memcpy((void *)0x037F8000, (void *)(value & IPC_CMD_ARG_MASK), 16 * 1024);
                fat_mountDldi();
            }
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_SETUP_DLDI, 0));
            break;
        }
        case IPC_CMD_HANDSHAKE: {
            bool canUseWram = false;
            if (isDSiMode())
                canUseWram = twr_isUnlocked();
            fifoSendValue32(FIFO_USER_01, IPC_CMD_PACK(IPC_CMD_HANDSHAKE, canUseWram ? 1 : 0));
            break;
        }
    }
}

void fv_main(void)
{
    if (!fifoCheckValue32(FIFO_USER_01))
        irq_wait(false, IRQ_TIMER2 | IRQ_FIFO_NOT_EMPTY);
    updateAudio();
    if (fifoCheckValue32(FIFO_USER_01))
        handleFifo(fifoGetValue32(FIFO_USER_01));
}
