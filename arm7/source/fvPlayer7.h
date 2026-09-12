#pragma once

#include "../../common/fastVideo.h"
#include "../../common/ipc.h"
#include "fat/ff.h"
#include "fpsAdjust.h"

#define FV_AUDIO_QUEUE_FRAMES 64
#define FV_AUDIO_RING_FRAMES 24
#define MAX_SHUFFLE_FILES 1024 // ~2 Ko en RAM ARM7, suffisant pour la plupart des dossiers

typedef struct
{
    bool audioStarted;
    FIL file;
    fpsa_t fpsa;
    u32 nrKeyFrames;
    
    // directory (without trailing slash) and file name of the currently
    // open video, kept so we can look up the previous/next video file
    char curDir[FV_MAX_PATH_LEN];
    char curName[FV_MAX_PATH_LEN];
    
    int queueReadPtr;
    int queueWritePtr;
    int queueFrameCount;
    int audioFramesNeeded;
    int audioFramesProvided;
    int ringPos;
    
    u8 audioQueueL[FV_AUDIO_QUEUE_FRAMES][FV_AUDIO_FRAME_SIZE] ALIGN(4);
    u8 audioQueueR[FV_AUDIO_QUEUE_FRAMES][FV_AUDIO_FRAME_SIZE] ALIGN(4);
    s16 audioRingL[FV_AUDIO_RING_FRAMES][FV_AUDIO_FRAME_SAMPLES] ALIGN(4);
    s16 audioRingR[FV_AUDIO_RING_FRAMES][FV_AUDIO_FRAME_SAMPLES] ALIGN(4);

    // État du shuffle bag (tirage sans remise)
    u16 shuffleIndices[MAX_SHUFFLE_FILES];
    u16 shuffleCount;
    u16 shuffleRemaining;
    char shuffleDir[FV_MAX_PATH_LEN];
    // nom exclu au moment du build du bag - figé pour toute la durée de vie
    // du bag, ne doit PAS être relu depuis curName pendant les tirages
    char shuffleExcludedName[FV_MAX_PATH_LEN];
} fv_player7_t;

void fv_init(void);
void fv_main(void);
