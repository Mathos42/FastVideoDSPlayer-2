#include <nds.h>
#include "PlayerController.h"

#define DIM_WAIT_SEC 5
#define DIM_FADE_SEC 3

// Ticks at BUS_CLOCK/1024 (~32728.5 Hz on NDS), ~30.5us/tick.
// ~3273 ticks = ~100ms, comfortably longer than any real switch bounce.
#define NAV_DEBOUNCE_TICKS 3273

extern u32 GetDebounceTicks();

bool PlayerController::sSubScreenOff = false;

// Variable globale pour mémoriser l'inversion des écrans
bool gScreenSwapped = false;

PlayerController::PlayerController(fv_player_t* player)
    : _inputRepeater(KEY_LEFT | KEY_RIGHT, 12, 3), _player(player), _subScreenState(SUB_SCREEN_STATE_ACTIVE),
      _subScreenStateCounter(0), _subBacklightOff(false), _playing(true), _lastTime(-1), _seekPenDown(false),
      _playPausePenDown(false), _seekLastFrame(-1), _seekKeyFrame(0), _pendingNavAction(NAV_ACTION_NONE),
      _videoEnded(false)
{
    for (int i = 0; i < 5; i++)
        _lastNavActionVBlank[i] = GetDebounceTicks() - NAV_DEBOUNCE_TICKS;

    // If the previous controller let the sub screen go dark (video chaining,
    // L/R/X/Y skips), stay dark instead of flashing back on for a few seconds.
    if (sSubScreenOff)
    {
        _subScreenState = SUB_SCREEN_STATE_OFF;
        REG_MASTER_BRIGHT_SUB = 16 | (2 << 14);
        if (isDSiMode())
        {
            // On éteint intelligemment l'écran inactif
            if (gScreenSwapped) {
                powerOff(PM_BACKLIGHT_TOP);
                powerOn(PM_BACKLIGHT_BOTTOM); // On sécurise la vidéo
            } else {
                powerOff(PM_BACKLIGHT_BOTTOM);
                powerOn(PM_BACKLIGHT_TOP);
            }
            _subBacklightOff = true;
        }
    }
}

void PlayerController::RestoreSubScreen()
{
    sSubScreenOff = false;
    REG_MASTER_BRIGHT_SUB = 0;
    if (isDSiMode()) {
        // On force le rallumage des deux écrans pour être sûr
        powerOn(PM_BACKLIGHT_BOTTOM);
        powerOn(PM_BACKLIGHT_TOP);
    }
}

void PlayerController::Initialize()
{
    _view.Initialize();

    // sync to whatever is physically held right now, so that a button the
    // user is still holding from just before this controller was created
    // (e.g. L/R/X/Y held a little past a video switch) doesn't get
    // misdetected as a brand new press on the very first Update() - see
    // InputProvider::PrimeCurrentState(). The repeater needs the same
    // treatment: PrimeCurrentState() only syncs _inputProvider, so without
    // this Reset() a LEFT/RIGHT held across the switch would still make
    // the freshly-constructed _inputRepeater fire an immediate spurious
    // repeat (see InputRepeater::Reset()).
    _inputProvider.PrimeCurrentState();
    _inputRepeater.Reset();

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

    // Si on a inversé les écrans, l'écran tactile (en bas physiquement)
    // affiche la vidéo, et non plus l'interface. On bloque donc les 
    // clics sur les boutons invisibles et on se contente de réveiller l'écran.
    if (gScreenSwapped) {
        if (_inputProvider.Triggered(KEY_TOUCH)) {
            _pendingNavAction = NAV_ACTION_SHOW_INFO;
        }
        return;
    }

    if (_inputProvider.Triggered(KEY_TOUCH))
    {
        if (touch.px >= 16 && touch.px < 240 && touch.py >= /*117*/ 113 && touch.py < /*121*/ 125)
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
            // tapped anywhere else on the touch screen: show the info toast
            // (filename + loop/random state) on demand
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
    // Debounce indépendant par touche : chaque bouton a son propre
    // timestamp, donc appuyer sur une touche ne bloque pas les autres.
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
        // --- LOGIQUE HAUT + START POUR INVERSER LES ECRANS ---
        if (_inputProvider.Current(KEY_UP)) 
        {
            gScreenSwapped = !gScreenSwapped;
            if (gScreenSwapped) {
                lcdMainOnBottom();
            } else {
                lcdMainOnTop();
            }
            
            // On réveille l'interface pour ne pas chercher les boutons à l'aveugle
            _subScreenState = SUB_SCREEN_STATE_ACTIVE;
            _subScreenStateCounter = 0;
            sSubScreenOff = false;
            
            if (isDSiMode()) {
                powerOn(PM_BACKLIGHT_BOTTOM);
                powerOn(PM_BACKLIGHT_TOP);
                _subBacklightOff = false;
            }
        } 
        else 
        {
            _pendingNavAction = NAV_ACTION_TOGGLE_LOOP;
        }
        
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
        // pause when lid is closed
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
    // Wake-up sources for the sub screen: pausing, a touch tap, or START/SELECT
    // (loop/random confirmation toast). Navigation keys (D-pad, L/R/X/Y) and
    // automatic video chaining must NOT wake it: the screen stays dark.
    if ((!_playing && !_videoEnded) || _inputProvider.Triggered(KEY_TOUCH) ||
        _inputProvider.Triggered(KEY_START) || _inputProvider.Triggered(KEY_SELECT))
    {
        _subScreenState = SUB_SCREEN_STATE_ACTIVE;
        _subScreenStateCounter = 0;
        sSubScreenOff = false;
    }

    // Sur DS Lite/Phat, il n'y a qu'un seul contrôle de rétroéclairage
    // hardware qui affecte les DEUX écrans. powerOff(PM_BACKLIGHT_BOTTOM)
    // éteindrait aussi l'écran du haut (celui de la vidéo).
    // Sur DSi/3DS, les deux écrans sont contrôlés indépendamment.
    if (isDSiMode())
    {
        if (_subBacklightOff && _subScreenState != SUB_SCREEN_STATE_OFF)
        {
            // Allumage de l'interface
            if (gScreenSwapped) {
                powerOn(PM_BACKLIGHT_TOP);
            } else {
                powerOn(PM_BACKLIGHT_BOTTOM);
            }
            // Sécurise la vidéo
            powerOn(gScreenSwapped ? PM_BACKLIGHT_BOTTOM : PM_BACKLIGHT_TOP);
            
            _subBacklightOff = false;
        }
        else if (!_subBacklightOff && _subScreenState == SUB_SCREEN_STATE_OFF)
        {
            // Extinction intelligente
            if (gScreenSwapped) {
                powerOff(PM_BACKLIGHT_TOP); // L'interface (en haut) s'éteint
                powerOn(PM_BACKLIGHT_BOTTOM);
            } else {
                powerOff(PM_BACKLIGHT_BOTTOM); // L'interface (en bas) s'éteint
                powerOn(PM_BACKLIGHT_TOP);
            }
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
            {
                _subScreenState = SUB_SCREEN_STATE_OFF;
                sSubScreenOff = true; // persist across controller recreations
            }
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
        // the video reached its end: stop audio/playback cleanly (instead of
        // leaving the last audio buffer looping forever) and let the caller
        // decide what happens next (repeat / next / random, depending on
        // the loop/random flags it owns)
        fv_pausePlayer(_player);
        _playing = false;
        _videoEnded = true;
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

    _inputProvider.Sample(); // todo: sample more frequently
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
    // while playing, PlayerView::Update()/VBlank() are normally only
    // called once a second (when the displayed second changes), to avoid
    // needless redraws; force an immediate refresh here so the toast (and
    // the time display drawn alongside it) doesn't wait for that next tick
    _view.Update();
    _view.VBlank();
}
