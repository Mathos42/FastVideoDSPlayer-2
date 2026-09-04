#pragma once

#include "core/PadInputProvider.h"
#include "core/InputRepeater.h"
#include "../FastVideo/fvPlayer.h"
#include "PlayerView.h"

class PlayerController
{
public:
    enum NavAction
    {
        NAV_ACTION_NONE,
        NAV_ACTION_NEXT,          // user pressed R/X: skip forward (or a random video, if random mode is on)
        NAV_ACTION_PREV,          // user pressed L/Y: skip back (or a random video, if random mode is on)
        NAV_ACTION_VIDEO_ENDED,   // the video reached its end on its own
        NAV_ACTION_TOGGLE_LOOP,   // user pressed START
        NAV_ACTION_TOGGLE_RANDOM, // user pressed SELECT
        NAV_ACTION_SHOW_INFO,     // user tapped the bottom screen away from the other touch zones
        NAV_ACTION_EXIT           // user pressed B
    };

private:
    enum SubScreenState
    {
        SUB_SCREEN_STATE_ACTIVE,
        SUB_SCREEN_STATE_DIMMING,
        SUB_SCREEN_STATE_OFF
    };

    SubScreenState _subScreenState;
    int _subScreenStateCounter;
    bool _subBacklightOff;

    int _dimWaitFrames;
    int _dimFadeFrames;
    u32 _invDimFadeFrames;

    fv_player_t* _player;
    bool _playing;
    u32 _seekKeyFrame;
    u32 _lastTime;

    bool _seekPenDown;
    bool _playPausePenDown;
    int _seekLastFrame;

    PadInputProvider _inputProvider;
    InputRepeater _inputRepeater;

    PlayerView _view;

    NavAction _pendingNavAction;
    u32 _lastNavActionVBlank; // for debouncing L/R/X/Y/B/START/SELECT (see UpdateKeys)

    void TogglePlayPause();

    void UpdateTouch();
    void UpdateKeys();
    void UpdateDim();

public:
    PlayerController(fv_player_t* player);

    void Initialize();

    // Returns the action requested this frame (see NavAction), or
    // NAV_ACTION_NONE if nothing needs handling. The caller owns the
    // persistent state (current path, loop/random flags) and is expected to
    // stop calling Update() on this controller and act accordingly (load a
    // new video / toggle a flag / exit) whenever this returns non-none.
    NavAction Update();

    // Shows a brief on-screen message (e.g. filename + loop/random state,
    // or a toggle confirmation). line2 may be NULL for a single-line message.
    void ShowMessage(const char* line1, const char* line2);
};
