#pragma once
#include <nds.h>
#include "../FastVideo/fvPlayer.h"
#include "PlayerView.h"
#include "core/PadInputProvider.h"
#include "core/InputRepeater.h"

class PlayerController
{
public:
    enum NavAction
    {
        NAV_ACTION_NONE = 0,
        NAV_ACTION_EXIT,
        NAV_ACTION_TOGGLE_LOOP,
        NAV_ACTION_TOGGLE_RANDOM,
        NAV_ACTION_NEXT,
        NAV_ACTION_PREV,
        NAV_ACTION_VIDEO_ENDED,
        NAV_ACTION_SHOW_INFO
    };

    PlayerController(fv_player_t* player);

    void Initialize();
    NavAction Update();
    void ShowMessage(const char* line1, const char* line2);

    // Re-lights the sub screen (backlight + master brightness) and clears the
    // "stay dark" state. Call when leaving video playback for good (back to
    // the browser, or before exiting the app), NOT between two videos.
    static void RestoreSubScreen();

private:
    enum SubScreenState
    {
        SUB_SCREEN_STATE_ACTIVE = 0,
        SUB_SCREEN_STATE_DIMMING,
        SUB_SCREEN_STATE_OFF
    };

    void TogglePlayPause();
    void UpdateTouch();
    void UpdateKeys();
    void UpdateDim();

    PlayerView _view;
    PadInputProvider _inputProvider;
    InputRepeater _inputRepeater;

    fv_player_t* _player;

    SubScreenState _subScreenState;
    u32 _subScreenStateCounter;
    bool _subBacklightOff;

    bool _playing;
    int _lastTime;

    bool _seekPenDown;
    bool _playPausePenDown;
    int _seekLastFrame;
    int _seekKeyFrame;

    u32 _dimWaitFrames;
    u32 _dimFadeFrames;
    u32 _invDimFadeFrames;

    NavAction _pendingNavAction;

    // Un timestamp de debounce séparé par touche pour éviter que l'appui
    // sur une touche ne bloque les autres pendant la fenêtre d'anti-rebond
    // index: 0=B, 1=START, 2=SELECT, 3=R/X, 4=L/Y
    u32 _lastNavActionVBlank[5];

    // True once the sub screen has gone dark. Static so that a controller
    // recreated for the next video (chaining, L/R/X/Y skips) inherits the
    // dark state instead of flashing the screen back on.
    static bool sSubScreenOff;
};
