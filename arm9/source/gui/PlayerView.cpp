#include <nds.h>
#include <stdio.h>
#include "RobotoRegular10_ntft.h"
#include "playBg.h"
#include "iconPlay.h"
#include "iconPause.h"
#include "circle0.h"
#include "circle1.h"
#include "core/uiUtil.h"
#include "PlayerView.h"

PlayerView::PlayerView() : _robotoRegular10(RobotoRegular10_ntft)
{
}

// Decodes the next character from a UTF-8 string, returning it as a Latin-1
// codepoint (0-255) when possible so it can index NtftFont's 256-entry
// character table. Understands plain ASCII plus 2-byte UTF-8 sequences that
// map onto the Latin-1 Supplement block (accented French letters: é, è, à,
// ç, ù, â, ê, î, ô, û, and their uppercase forms all fall in this range).
// 3-byte and 4-byte sequences are consumed whole but rendered as '?'.
// Advances *text.
static unsigned char DecodeNextChar(const char** text)
{
    unsigned char c = (unsigned char)**text;
    if (c == 0)
        return 0;
    if ((c & 0xE0) == 0xC0 && ((*text)[1] & 0xC0) == 0x80)
    {
        // 2-byte UTF-8 sequence; only C2/C3 leads (Latin-1 Supplement,
        // U+0080-U+00FF) decode to something this font could plausibly have
        unsigned char c2 = (unsigned char)(*text)[1];
        *text += 2;
        return (unsigned char)(((c & 0x1F) << 6) | (c2 & 0x3F));
    }
    if ((c & 0xF0) == 0xE0 && ((*text)[1] & 0xC0) == 0x80 && ((*text)[2] & 0xC0) == 0x80)
    {
        *text += 3; // 3-byte sequence: no latin-1 equivalent, one single '?'
        return '?';
    }
    if ((c & 0xF8) == 0xF0 && ((*text)[1] & 0xC0) == 0x80 && ((*text)[2] & 0xC0) == 0x80 && ((*text)[3] & 0xC0) == 0x80)
    {
        *text += 4; // 4-byte sequence (emoji, non-latin): same
        return '?';
    }
    (*text)++;
    return c;
}

// VRAM_I, mapped to the sub-screen's sprite (OBJ) memory by main.cpp
// (vramSetBankI(VRAM_I_SUB_SPRITE)), is only 16KB in total. Writing sprite
// tile data past that boundary silently corrupts whatever comes after
// (which, on real hardware, showed up as garbled/missing digits) since
// VramManager itself has no notion of the underlying bank's real size.
static const int SUB_SPRITE_VRAM_SIZE = 16 * 1024;

int PlayerView::RenderTextLine(const char* text, u16* tileAddr, int maxChars)
{
    int n = 0;
    unsigned char c;
    while (n < maxChars && (c = DecodeNextChar(&text)) != 0)
    {
        // hard safety net: never allocate past the physical VRAM bank,
        // no matter what miscalculated the caller's char budget
        if ((int)_subObj.GetState() + CHAR_CELL_W * CHAR_CELL_H / 2 > SUB_SPRITE_VRAM_SIZE)
            break;

        char single[2] = { (char)c, 0 };
        memset(_textTmpBuf, 0, sizeof(_textTmpBuf));
        _robotoRegular10.CreateStringData(single, _textTmpBuf, CHAR_CELL_W);
        u16 addr = _subObj.Alloc(CHAR_CELL_W * CHAR_CELL_H / 2) >> 5;
        uiutil_convertToObj(_textTmpBuf, CHAR_CELL_W, CHAR_CELL_H, CHAR_CELL_W, &SPRITE_GFX_SUB[addr << 4]);
        tileAddr[n] = addr;
        n++;
    }
    return n;
}

int PlayerView::PlaceTextLine(SpriteEntry* oams, const u16* tileAddr, int len, int x, int y, int palette)
{
    for (int i = 0; i < len; i++)
    {
        oams[i].attribute[0] = ATTR0_NORMAL | ATTR0_TYPE_NORMAL | ATTR0_COLOR_16 | ATTR0_SQUARE | y;
        oams[i].attribute[1] = ATTR1_SIZE_16 | (x + i * CHAR_ADVANCE_W);
        oams[i].attribute[2] = ATTR2_PRIORITY(3) | ATTR2_PALETTE(palette) | tileAddr[i];
    }
    return len;
}

void PlayerView::Initialize()
{
    decompress(playBgTiles, BG_GFX_SUB, LZ77Vram);
    decompress(playBgMap, (u8*)BG_GFX_SUB + 0x800, LZ77Vram);
    dmaCopyWords(3, playBgPal, BG_PALETTE_SUB, playBgPalLen);

    _subOam.Apply(OAM_SUB);

    REG_DISPCNT_SUB = DISPLAY_BG0_ACTIVE | DISPLAY_BG1_ACTIVE | DISPLAY_BG2_ACTIVE | DISPLAY_SPR_ACTIVE |
                      DISPLAY_SPR_1D | DISPLAY_SPR_1D_SIZE_32 | MODE_0_2D | DISPLAY_WIN0_ON;

    REG_BG2CNT_SUB &= ~BG_PRIORITY_3;

    REG_BG0CNT_SUB = BG_32x32 | BG_PRIORITY_2 | BG_COLOR_16 | BG_MAP_BASE(1) | BG_TILE_BASE(0);
    REG_BG0HOFS_SUB = 0;
    REG_BG0VOFS_SUB = 0;

    REG_BG1CNT_SUB = BG_32x32 | BG_PRIORITY_1 | BG_COLOR_16 | BG_MAP_BASE(1) | BG_TILE_BASE(0);
    REG_BG1HOFS_SUB = 0;
    REG_BG1VOFS_SUB = -176;

    SUB_WIN0_X0 = 16;
    SUB_WIN0_X1 = 128;
    SUB_WIN0_Y0 = 117;
    SUB_WIN0_Y1 = 117 + 4;

    SUB_WIN_IN = 0x16;
    SUB_WIN_OUT = 0x15;

    for (int i = 0; i < 16; i++)
    {
        int rnew = 4 + ((31 - 4) * i) / 15;
        int gnew = 6 + ((31 - 6) * i) / 15;
        int bnew = 8 + ((31 - 8) * i) / 15;
        SPRITE_PALETTE_SUB[16 + i] = RGB5(rnew, gnew, bnew);
    }

    for (int i = 0; i < 16; i++)
    {
        int rnew = 6 + ((31 - 6) * i) / 15;
        int gnew = 8 + ((31 - 8) * i) / 15;
        int bnew = 11 + ((31 - 11) * i) / 15;
        SPRITE_PALETTE_SUB[32 + i] = RGB5(rnew, gnew, bnew);
    }

    for (
