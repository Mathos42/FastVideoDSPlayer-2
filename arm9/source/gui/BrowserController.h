#pragma once
#include <nds.h>
#include "../../../common/ipc.h"
#include "core/PadInputProvider.h"
#include "core/InputRepeater.h"
#include "BrowserView.h"

class BrowserController
{
public:
    enum Action
    {
        ACT_NONE = 0,
        ACT_OPEN_DIR,
        ACT_PLAY,
        ACT_PARENT,
        ACT_TOGGLE_LOOP,
        ACT_TOGGLE_RANDOM,
        ACT_EXIT
    };

    BrowserController();

    bool OpenDir(const char* path);
    void SelectEntryByName(const char* name);
    void SetModes(bool loopEnabled, bool randomEnabled);
    Action Update();
    void GetSelectedPath(char* out, size_t outMax) const;
    const char* GetCurDir() const
    {
        return _curDir;
    }

private:
    void MoveCursor(int delta);
    void RenderIfNeeded();

    BrowserView _view;
    PadInputProvider _inputProvider;
    InputRepeater _inputRepeater;

    char _curDir[FV_MAX_PATH_LEN];
    const fv_dir_entry_t* _entries;
    u32 _count;
    u32 _total;
    u32 _cursor;
    u32 _topLine;
    bool _dirty;
    bool _loopEnabled;
    bool _randomEnabled;
};
