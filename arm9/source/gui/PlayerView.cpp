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

static unsigned char DecodeNextChar(const char** text)
{
    unsigned char c = (unsigned char)**text;
    if (c == 0)
        return 0;
    if ((c & 0xE0) == 0xC0 && ((*text)[1] & 0xC0) == 0x80)
    {
        unsigned char c2 = (unsigned char)(*text)[1];
        *text += 2;
        return (unsigned char)(((c & 0x1F) << 6) | (c2 & 0x3F));
    }
    if ((c & 0xF0) == 0xE0 && ((*text)[1] & 0xC0) == 0x80 && ((*text)[2] & 0xC0) == 0x80)
    {
        *text += 3;
        return '?';
    }
    if ((c & 0xF8) == 0xF0 && ((*text)[1] & 0xC0) == 0x80 && ((*text)[2] & 0xC0) == 0x80 && ((*text)[3] & 0xC0) == 0x80)
    {
        *text += 4;
        return '?';
    }
    (*text)++;
    return c;
}

static const int SUB_SPRITE_VRAM_SIZE = 16 * 1024;

int PlayerView::RenderTextLine(const char* text, u16* tileAddr, int maxChars)
{
    int n = 0;
    unsigned char c;
    while (n < maxChars && (c = DecodeNextChar(&text)) != 0)
    {
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
