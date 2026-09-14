#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "BrowserController.h"

#define BROWSER_MAX_ENTRIES 512

static bool IsRootDir(const char* path)
{
    // Root = "sd:/", "fat:/", or "/": the only '/' is at the end of the device
    const char* colon = strchr(path, ':');
    if (colon)
        return (colon[1] == '/' && colon[2] == '\0');
    return (strcmp(path, "/") == 0);
}

static fv_dir_entry_t sEntries[BROWSER_MAX_ENTRIES] ALIGN(32);
static fv_listdir_req_t sListReq ALIGN(32);

// directories first, then case-insensitive alphabetical
static int CompareEntries(const void* a, const void* b)
{
    const fv_dir_entry_t* ea = (const fv_dir_entry_t*)a;
    const fv_dir_entry_t* eb = (const fv_dir_entry_t*)b;
    if (ea->isDir != eb->isDir)
        return (int)eb->isDir - (int)ea->isDir;
    return strcasecmp(ea->name, eb->name);
}

// synchronous directory listing via the arm7 (same pattern as the
// FIND_NEXT/PREV/RANDOM commands on FIFO_USER_02)
static bool RequestListDir(const char* path)
{
    strncpy(sListReq.path, path, FV_MAX_PATH_LEN - 1);
    sListReq.path[FV_MAX_PATH_LEN - 1] = 0;
    sListReq.entries = sEntries;
    sListReq.maxEntries = BROWSER_MAX_ENTRIES;
    sListReq.count = 0;
    sListReq.total = 0;

    DC_FlushRange(sEntries, sizeof(sEntries));
    DC_FlushRange(&sListReq, sizeof(sListReq));

    fifoSendValue32(FIFO_USER_02, IPC_CMD_PACK(IPC_CMD_LIST_DIR, (u32)&sListReq));
    fifoWaitValue32(FIFO_USER_02);
    u32 ok = fifoGetValue32(FIFO_USER_02) & IPC_CMD_ARG_MASK;
    if (!ok)
        return false;

    DC_InvalidateRange(&sListReq, sizeof(sListReq));
    DC_InvalidateRange(sEntries, sizeof(fv_dir_entry_t) * sListReq.count);

    if (sListReq.count > 1)
        qsort(sEntries, sListReq.count, sizeof(fv_dir_entry_t), CompareEntries);
    return true;
}

BrowserController::BrowserController()
    : _inputRepeater(KEY_UP | KEY_DOWN, 12, 3),
      _entries(sEntries), _count(0), _total(0), _cursor(0), _topLine(0), _dirty(true)
{
    _view.Initialize();
    _curDir[0] = 0;
    _inputProvider.PrimeCurrentState();
}

bool BrowserController::OpenDir(const char* path)
{
    if (!RequestListDir(path))
        return false;

    strncpy(_curDir, path, FV_MAX_PATH_LEN - 1);
    _curDir[FV_MAX_PATH_LEN - 1] = 0;
    _count = sListReq.count;
    _total = sListReq.total;
    _cursor = 0;
    _topLine = 0;
    _dirty = true;
    RenderIfNeeded();
    return true;
}
void BrowserController::SelectEntryByName(const char* name)
{
    if (!name || _count == 0)
        return;

    for (u32 i = 0; i < _count; i++)
    {
        if (_entries[i].isDir != 0)
            continue;
        if (strcasecmp(_entries[i].name, name) == 0)
        {
            _cursor = i;
            if (_cursor < _topLine)
                _topLine = _cursor;
            if (_cursor >= _topLine + BROWSER_VISIBLE_LINES)
                _topLine = _cursor - BROWSER_VISIBLE_LINES + 1;
            _dirty = true;
            RenderIfNeeded();
            return;
        }
    }
}
void BrowserController::MoveCursor(int delta)
{
    if (_count == 0)
        return;
    int cur = (int)_cursor + delta;
    if (cur < 0)
        cur = 0;
    if (cur >= (int)_count)
        cur = (int)_count - 1;
    _cursor = (u32)cur;
    if (_cursor < _topLine)
        _topLine = _cursor;
    if (_cursor >= _topLine + BROWSER_VISIBLE_LINES)
        _topLine = _cursor - BROWSER_VISIBLE_LINES + 1;
    _dirty = true;
}

void BrowserController::RenderIfNeeded()
{
    if (!_dirty)
        return;
    _view.Render(_curDir, _entries, _count, _total, _cursor, _topLine);
    _dirty = false;
}

BrowserController::Action BrowserController::Update()
{
    swiWaitForVBlank();

    _inputProvider.Sample();
    _inputProvider.Update();
    _inputRepeater.Update(&_inputProvider);

    Action action = ACT_NONE;

    if (_inputProvider.Triggered(KEY_DOWN) || _inputRepeater.Triggered(KEY_DOWN))
        MoveCursor(+1);
    else if (_inputProvider.Triggered(KEY_UP) || _inputRepeater.Triggered(KEY_UP))
        MoveCursor(-1);
    else if (_inputProvider.Triggered(KEY_R))
        MoveCursor(+BROWSER_VISIBLE_LINES);
    else if (_inputProvider.Triggered(KEY_L))
        MoveCursor(-BROWSER_VISIBLE_LINES);
    else if (_inputProvider.Triggered(KEY_A))
    {
        if (_cursor < _count)
            action = _entries[_cursor].isDir ? ACT_OPEN_DIR : ACT_PLAY;
    }
    else if (_inputProvider.Triggered(KEY_B))
        action = IsRootDir(_curDir) ? ACT_EXIT : ACT_PARENT;
    else if (_inputProvider.Triggered(KEY_START))
        action = ACT_TOGGLE_LOOP;
    else if (_inputProvider.Triggered(KEY_SELECT))
        action = ACT_TOGGLE_RANDOM;

    // touch: tapping a visible line selects and opens it
    if (action == ACT_NONE && _inputProvider.Triggered(KEY_TOUCH))
    {
        touchPosition t;
        touchRead(&t);
        int row = (int)(t.py / 8); // console rows are 8px tall
        if (row >= 1 && row <= BROWSER_VISIBLE_LINES)
        {
            u32 idx = _topLine + (u32)(row - 1);
            if (idx < _count)
            {
                _cursor = idx;
                action = _entries[idx].isDir ? ACT_OPEN_DIR : ACT_PLAY;
            }
        }
    }

    RenderIfNeeded();
    return action;
}

void BrowserController::GetSelectedPath(char* out, size_t outMax) const
{
    strncpy(out, _curDir, outMax - 1);
    out[outMax - 1] = 0;
    size_t len = strlen(out);
    if (len > 0 && out[len - 1] != '/' && len + 1 < outMax)
    {
        out[len] = '/';
        out[len + 1] = 0;
    }
    strncat(out, _entries[_cursor].name, outMax - strlen(out) - 1);
}
