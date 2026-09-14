#pragma once

#define IPC_CMD_READ_FRAME            1
#define IPC_CMD_OPEN_FILE             2
#define IPC_CMD_CONTROL_AUDIO         3
#define IPC_CMD_READ_HEADER           4
#define IPC_CMD_GOTO_KEYFRAME         5
#define IPC_CMD_GOTO_NEAREST_KEYFRAME 6
#define IPC_CMD_FIND_NEXT_FILE        7
#define IPC_CMD_FIND_PREV_FILE        8
#define IPC_CMD_FIND_RANDOM_FILE      9
#define IPC_CMD_LIST_DIR              10
#define IPC_CMD_SETUP_DLDI            13
#define IPC_CMD_HANDSHAKE             15

// max length (including null terminator) of a path written by
// IPC_CMD_FIND_NEXT_FILE / IPC_CMD_FIND_PREV_FILE into the buffer
// whose address is passed as the command argument
#define FV_MAX_PATH_LEN 256

// one directory entry returned by IPC_CMD_LIST_DIR
#define FV_BROWSER_NAME_LEN 64

typedef struct
{
    char name[FV_BROWSER_NAME_LEN];
    u32 isDir;
} fv_dir_entry_t;

// request block for IPC_CMD_LIST_DIR, filled by the arm9, processed by the
// arm7. The arm9 passes the address of this struct as the command argument.
typedef struct
{
    char path[FV_MAX_PATH_LEN];   // in:  directory to list
    fv_dir_entry_t* entries;      // in:  output buffer (arm9 main RAM)
    u32 maxEntries;               // in:  capacity of entries[]
    u32 count;                    // out: entries actually written
    u32 total;                    // out: total matching entries in the dir
} fv_listdir_req_t;

#define IPC_CMD_ARG_MASK       0x0FFFFFFF
#define IPC_CMD_CMD_SHIFT      28
#define IPC_CMD_CMD_MASK       0xF0000000
#define IPC_CMD_PACK(cmd, arg) ((((u32)(cmd) << IPC_CMD_CMD_SHIFT) & IPC_CMD_CMD_MASK) | ((u32)(arg)&IPC_CMD_ARG_MASK))

#define IPC_ARG_CONTROL_AUDIO_STOP       0
#define IPC_ARG_CONTROL_AUDIO_START      1
#define IPC_ARG_CONTROL_AUDIO_STOP_CLEAR 2
