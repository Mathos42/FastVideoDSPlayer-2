#pragma once

#include "core/VramManager.h"
#include "core/OamManager.h"
#include "core/NtftFont.h"

class PlayerView
{
    VramManager _subObj;
    OamManager _subOam;
    NtftFont _robotoRegular10;

    u8 _textTmpBuf[16 * 16];

    u16 _oneDigitObjAddr;
    s8 _oneDigitOffset[10];
    u8 _oneDigitWidth[10];
    s8 _oneDigitEndOffset[10];

    u16 _twoDigitObjAddr;
    s8 _twoDigitOffset[/*100*/60];
    u8 _twoDigitWidth[/*100*/60];
    s8 _twoDigitEndOffset[/*100*/60];

    u16 _colonObjAddr;
    s8 _colonOffset;
    u8 _colonWidth;

    u32 _curTime;

    SpriteEntry _curTimeOams[5];

    u32 _totalTime;
    u32 _invTotalTime;

    SpriteEntry _totalTimeOams[5];

    u16 _circleObjAddr;
    u16 _playIconObjAddr;
    u16 _pauseIconObjAddr;

    bool _playing;

    // --- generic small on-screen text (help legend + toast messages) ---
    // each character is rendered into a 16x16 sprite (wide enough that no
    // glyph gets clipped/skipped, even wide ones like 'm' or 'M' - an 8px
    // buffer turned out to be too narrow and silently dropped those), but
    // consecutive characters are placed only CHAR_ADVANCE_W apart (tighter
    // than the sprite itself) to keep the text reasonably dense; this
    // relies on glyphs being left-aligned with blank space on their right
    // within the 16px cell, which holds for all but the widest characters.
    //
    // The sub-screen's sprite VRAM bank (VRAM_I) is only 16KB total, and at
    // 128 bytes per 16x16 4bpp character cell that budget disappears fast
    // once the fixed content (digits/circle/icons, ~5KB) is accounted for -
    // MAX_LEGEND_CHARS/MAX_MSG_CHARS are sized to leave a comfortable
    // safety margin under that limit (see RenderTextLine's hard cutoff for
    // the case where they're still miscalculated).
    static const int CHAR_CELL_W = 16;
    static const int CHAR_CELL_H = 16;
    static const int CHAR_ADVANCE_W = 8;
    static const int MAX_LEGEND_CHARS = 24; // per legend line
    static const int MAX_MSG_CHARS = 20;    // per toast line

    u16 _legendVramAfterFixed;              // VramManager checkpoint right after digits/circle/icons
    u16 _legendLine1TileAddr[MAX_LEGEND_CHARS];
    int _legendLine1Len;
    u16 _legendLine2TileAddr[MAX_LEGEND_CHARS];
    int _legendLine2Len;

    u16 _msgVramCheckpoint; // VramManager checkpoint right after the (permanent) legend
    u16 _msgLine1TileAddr[MAX_MSG_CHARS];
    int _msgLine1Len;
    u16 _msgLine2TileAddr[MAX_MSG_CHARS];
    int _msgLine2Len;
    bool _msgVisible;
    u32 _msgHideAtTime; // video timeline (_curTime) at which the toast should hide

    // renders `text` as a row of individual 8x16 character sprites (tile
    // data only; OAM placement happens every frame in Update()). Returns
    // the number of characters actually rendered (<= maxChars). Handles a
    // small subset of UTF-8 (Latin-1 Supplement, e.g. accented French
    // letters) so filenames with accents display reasonably.
    int RenderTextLine(const char* text, u16* tileAddr, int maxChars);

    // draws a previously-rendered line (see RenderTextLine) as OAM sprites
    // starting at (x,y). Returns the number of OAM entries used.
    int PlaceTextLine(SpriteEntry* oams, const u16* tileAddr, int len, int x, int y, int palette);

    int RenderColon(SpriteEntry* oam, int x, int y);
    int RenderSingleDigit(SpriteEntry* oam, int digit, int x, int y);
    int RenderDoubleDigit(SpriteEntry* oam, int digits, int x, int y);

public:

    PlayerView();

    void Initialize();
    void Update();
    void VBlank();

    void SetTotalTime(u32 totalTime);
    void SetCurrentTime(u32 currentTime);

    void SetPlaying(bool playing)
    {
        _playing = playing;
    }

    // shows a temporary 1- or 2-line message (e.g. filename + loop/random
    // state) for a few seconds. line2 may be NULL for a single-line message.
    void SetMessage(const char* line1, const char* line2);
};
