#include <nds.h>
#include "PlayerController.h"

#define DIM_WAIT_SEC 5
#define DIM_FADE_SEC 3

// Ticks at BUS_CLOCK/1024 (~32728.5 Hz on NDS), ~30.5us/tick.
// ~3273 ticks = ~100ms, comfortably longer than any real switch bounce.
#define NAV_DEBOUNCE_TICKS 3273

extern u32 GetDebounceTicks();

PlayerController::PlayerController(fv_player_t* player)
    : _subScreenState(SUB_SCREEN_STATE_ACTIVE), _subScreenStateCounter(0), _subBacklightOff(false), _player(player),
      _playing(true), _lastTime(-1), _seekPenDown(false), _playPausePenDown(false), _seekLastFrame(-1),
      _inputRepeater(KEY_LEFT | KEY_RIGHT, 12, 3), _pendingNavAction(NAV_ACTION_NONE)
{
    for (int i = 0; i < 5; i++)
        _lastNavActionVBlank[i] = GetDebounceTicks() - NAV_DEBOUNCE_TICKS;
}

PlayerController::~PlayerController()
{
    if (_subBacklightOff)
    {
        powerOn(PM_BACKLIGHT_BOTTOM);
        _subBacklightOff = false;
    }
    REG_MASTER_BRIGHT_SUB = 0;
}

void PlayerController::Initialize()
{
    _view.Initialize();
    _inputProvider.PrimeCurrentState();

    _dimWaitFrames = DIM_WAIT_SEC * _player->fvHeader->fpsNum / _player->fvHeader->fpsDen;
    _dimFadeFrames = DIM_FADE_SEC * _player->fvHeader->fpsNum / _player->fvHeader->fpsDen;
    _invDimFadeFrames = 0x800000 / _dimFadeFrames;

    u32 totalSeconds = ((u64)_player->fvHeader->nrFrames * _player->fvHeader->fpsDen) / _player->fvHeader->fpsNum;
    _view.SetTotalTime(totalSeconds);
    _view.SetCurrentTime(0);
}

void PlayerController::TogglePlayPause()
{
    if (_playing)
    {
        fv_pausePlayer(_player);
        _playing = false;
    }
    else
    {
        fv_resumePlayer(_player);
        _playing = true;
    }
}

void PlayerController::UpdateTouch()
{
    if (_subScreenState != SUB_SCREEN_STATE_ACTIVE)
        return;

    touchPosition touch;
    touchRead(&touch);

    if (_inputProvider.Triggered(KEY_TOUCH))
    {
        if (touch.px >= 16 && touch.px < 240 && touch.py >= 113 && touch.py < 125)
        {
            _seekPenDown = true;
            _seekLastFrame = -1;
        }
        else if (touch.px >= 110 && touch.px < 110 + 36 && touch.py >= 140 && touch.py < 140 + 36)
        {
            _playPausePenDown = true;
        }
        else
        {
            _pendingNavAction = NAV_ACTION_SHOW_INFO;
        }
    }

    if (_inputProvider.Released(KEY_TOUCH))
    {
        _seekPenDown = false;

        if (_playPausePenDown)
        {
            TogglePlayPause();
            _playPausePenDown = false;
        }
    }

    if (_inputProvider.Current(KEY_TOUCH) && _playPausePenDown)
    {
        if (!(touch.px >= 110 && touch.px < 110 + 36 && touch.py >= 140 && touch.py < 140 + 36))
        {
            _playPausePenDown = false;
        }
    }

    if (_inputProvider.Current(KEY_TOUCH) && _seekPenDown)
    {
        int frame = ((int)touch.px - 16) * (int)_player->fvHeader->nrFrames / 224;
        if (frame < 0)
            frame = 0;
        else if ((u32)frame >= _player->fvHeader->nrFrames)
            frame = _player->fvHeader->nrFrames - 1;
        if (frame != _seekLastFrame)
        {
            _seekLastFrame = frame;
            fv_gotoNearestKeyFrame(_player, frame);
            fv_startPlayer(_player);
            _playing = true;
        }
    }
}

void PlayerController::UpdateKeys()
{
    u32 now = GetDebounceTicks();

    bool bDebounced      = (now - _lastNavActionVBlank[0]) < NAV_DEBOUNCE_TICKS;
    bool startDebounced  = (now - _lastNavActionVBlank[1]) < NAV_DEBOUNCE_TICKS;
    bool selectDebounced = (now - _lastNavActionVBlank[2]) < NAV_DEBOUNCE_TICKS;
    bool nextDebounced   = (now - _lastNavActionVBlank[3]) < NAV_DEBOUNCE_TICKS;
    bool prevDebounced   = (now - _lastNavActionVBlank[4]) < NAV_DEBOUNCE_TICKS;

    if (!bDebounced && _inputProvider.Triggered(KEY_B))
    {
        _pendingNavAction = NAV_ACTION_EXIT;
        _lastNavActionVBlank[0] = now;
        return;
    }
    if (!startDebounced && _inputProvider.Triggered(KEY_START))
    {
        _pendingNavAction = NAV_ACTION_TOGGLE_LOOP;
        _lastNavActionVBlank[1] = now;
        return;
    }
    if (!selectDebounced && _inputProvider.Triggered(KEY_SELECT))
    {
        _pendingNavAction = NAV_ACTION_TOGGLE_RANDOM;
        _lastNavActionVBlank[2] = now;
        return;
    }
    if (!nextDebounced && (_inputProvider.Triggered(KEY_R) || _inputProvider.Triggered(KEY_X)))
    {
        _pendingNavAction = NAV_ACTION_NEXT;
        _lastNavActionVBlank[3] = now;
        return;
    }
    if (!prevDebounced && (_inputProvider.Triggered(KEY_L) || _inputProvider.Triggered(KEY_Y)))
    {
        _pendingNavAction = NAV_ACTION_PREV;
        _lastNavActionVBlank[4] = now;
        return;
    }

    if (_inputProvider.Current(KEY_LID))
    {
        if (_playing)
        {
            fv_pausePlayer(_player);
            _playing = false;
        }
    }
    else if (_inputProvider.Triggered(KEY_A))
    {
        TogglePlayPause();
    }
    else if (_inputProvider.Triggered(KEY_LEFT))
    {
        _seekKeyFrame = fv_gotoPreviousKeyFrame(_player);
        fv_startPlayer(_player);
        _playing = true;
    }
    else if (_inputRepeater.Triggered(KEY_LEFT))
    {
        if (_seekKeyFrame > 0)
            _seekKeyFrame--;
        fv_gotoKeyFrame(_player, _seekKeyFrame);
        fv_startPlayer(_player);
        _playing = true;
    }
    else if (_inputProvider.Triggered(KEY_RIGHT))
    {
        _seekKeyFrame = fv_gotoNextKeyFrame(_player);
        fv_startPlayer(_player);
        _playing = true;
    }
    else if (_inputRepeater.Triggered(KEY_RIGHT))
    {
        fv_gotoKeyFrame(_player, ++_seekKeyFrame);
        fv_startPlayer(_player);
        _playing = true;
    }
}

void PlayerController::UpdateDim()
{
    if (!_playing || (_inputProvider.GetCurrentKeys() & ~KEY_LID) || (_inputProvider.GetReleasedKeys() & KEY_LID))
    {
        _subScreenState = SUB_SCREEN_STATE_ACTIVE;
        _subScreenStateCounter = 0;
    }

    if (isDSiMode())
    {
        if (_subBacklightOff && _subScreenState != SUB_SCREEN_STATE_OFF)
        {
            powerOn(PM_BACKLIGHT_BOTTOM);
            _subBacklightOff = false;
        }
        else if (!_subBacklightOff && _subScreenState == SUB_SCREEN_STATE_OFF)
        {
            powerOff(PM_BACKLIGHT_BOTTOM);
            _subBacklightOff = true;
        }
    }

    switch (_subScreenState)
    {
        case SUB_SCREEN_STATE_ACTIVE:
            REG_MASTER_BRIGHT_SUB = 0;
            if (++_subScreenStateCounter >= _dimWaitFrames)
            {
                _subScreenStateCounter = 0;
                _subScreenState = SUB_SCREEN_STATE_DIMMING;
            }
            break;

        case SUB_SCREEN_STATE_DIMMING:
        {
            int dimFrame = (_subScreenStateCounter * 16 * _invDimFadeFrames + 0x400000) >> 23;
            if (dimFrame < 0)
                dimFrame = 0;
            else if (dimFrame > 16)
                dimFrame = 16;
            REG_MASTER_BRIGHT_SUB = dimFrame | (2 << 14);
            if (dimFrame < 16)
                _subScreenStateCounter++;
            else
                _subScreenState = SUB_SCREEN_STATE_OFF;
            break;
        }

        case SUB_SCREEN_STATE_OFF:
            break;
    }
}

PlayerController::NavAction PlayerController::Update()
{
    if (_player->videoEnded && _playing)
    {
        fv_pausePlayer(_player);
        _playing = false;
        _pendingNavAction = NAV_ACTION_VIDEO_ENDED;
    }

    _view.SetPlaying(_playing);
    if (_playing)
    {
        REG_DIVCNT = DIV_64_32;
        REG_DIV_NUMER = (u64)_player->curFrame * (u64)_player->fvHeader->fpsDen;
        REG_DIV_DENOM_L = _player->fvHeader->fpsNum;

        fv_updatePlayer(_player);

        while (REG_DIVCNT & DIV_BUSY)
            ;
        u32 time = REG_DIV_RESULT_L;
        if (time != _lastTime)
        {
            _lastTime = time;
            _view.SetCurrentTime(time);
            _view.Update();
            _view.VBlank();
        }
    }
    else
    {
        _view.Update();
        swiWaitForVBlank();
        _view.VBlank();
        _lastTime = -1;
    }

    _inputProvider.Sample();
    _inputProvider.Update();
    _inputRepeater.Update(&_inputProvider);
    UpdateTouch();
    UpdateKeys();
    UpdateDim();

    NavAction action = _pendingNavAction;
    _pendingNavAction = NAV_ACTION_NONE;
    return action;
}

void PlayerController::ShowMessage(const char* line1, const char* line2)
{
    _view.SetMessage(line1, line2);
    _view.Update();
    _view.VBlank();
}
