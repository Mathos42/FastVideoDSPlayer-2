#include <nds.h>
#include <stdio.h>
#include <string.h>
#include "BrowserView.h"

// latin-1 codepoint -> closest ASCII, for display only
static char Transliterate(unsigned char c)
{
    if (c < 0x80)
        return (char)c;
    switch (c)
    {
        case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: case 0xC5:
        case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5:
            return 'a';
        case 0xC7: case 0xE7:
            return 'c';
        case 0xC8: case 0xC9: case 0xCA: case 0xCB:
        case 0xE8: case 0xE9: case 0xEA: case 0xEB:
            return 'e';
        case 0xCC: case 0xCD: case 0xCE: case 0xCF:
        case 0xEC: case 0xED: case 0xEE: case 0xEF:
            return 'i';
        case 0xD1: case 0xF1:
            return 'n';
        case 0xD2: case 0xD3: case 0xD4: case 0xD5: case 0xD6:
        case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6:
            return 'o';
        case 0xD9: case 0xDA: case 0xDB: case 0xDC:
        case 0xF9: case 0xFA: case 0xFB: case 0xFC:
            return 'u';
        case 0xDD: case 0xFD: case 0xFF:
            return 'y';
        default:
            return '?';
    }
}

// decodes UTF-8 (ASCII + 2/3/4-byte sequences) and transliterates
static void MakeDisplayName(const char* name, char* out, size_t outMax)
{
    size_t o = 0;
    while (*name && o < outMax - 1)
    {
        unsigned char c = (unsigned char)*name;
        if ((c & 0xE0) == 0xC0 && (name[1] & 0xC0) == 0x80)
        {
            out[o++] = Transliterate((unsigned char)(((c & 0x1F) << 6) | (name[1] & 0x3F)));
            name += 2;
        }
        else if ((c & 0xF0) == 0xE0 && (name[1] & 0xC0) == 0x80 && (name[2] & 0xC0) == 0x80)
        {
            out[o++] = '?'; // 3-byte sequence: no latin-1 equivalent
            name += 3;
        }
        else if ((c & 0xF8) == 0xF0 && (name[1] & 0xC0) == 0x80 && (name[2] & 0xC0) == 0x80 && (name[3] & 0xC0) == 0x80)
        {
            out[o++] = '?'; // 4-byte sequence (emoji, non-latin): same
            name += 4;
        }
        else
        {
            out[o++] = Transliterate(c);
            name++;
        }
    }
    out[o] = 0;
}

void BrowserView::Initialize()
{
    // The player view leaves BG0/BG1 and sprites enabled on the sub screen
    // (background art, progress bar, icons, help text). Hide everything
    // before showing the console listing, otherwise those layers stay
    // visible on top of it.
    oamClear(&oamSub, 0, 128); // disables all 128 sub sprite entries
    REG_DISPCNT_SUB = MODE_0_2D;

    consoleInit(NULL, 2, BgType_Text4bpp, BgSize_T_256x256, 2, 1, false, true);
    consoleClear();

    // only the console text BG remains visible
    REG_DISPCNT_SUB = MODE_0_2D | DISPLAY_BG2_ACTIVE;
}

void BrowserView::Render(const char* dirPath, const fv_dir_entry_t* entries, u32 count, u32 total, u32 cursor,
                         u32 topLine, bool loopEnabled, bool randomEnabled)
{
    char disp[FV_MAX_PATH_LEN];

    consoleClear();

    MakeDisplayName(dirPath, disp, sizeof(disp));
    iprintf("\x1b[0;0H\x1b[33m%.32s\x1b[0m", disp);

    for (int i = 0; i < BROWSER_VISIBLE_LINES; i++)
    {
        u32 idx = topLine + i;
        iprintf("\x1b[%d;0H", 1 + i);
        if (idx < count)
        {
            char line[31];
            MakeDisplayName(entries[idx].name, line, sizeof(line));
            char suffix = entries[idx].isDir ? '/' : ' ';
            if (idx == cursor)
                iprintf("\x1b[47;30m>%-30s%c\x1b[0m", line, suffix);
            else
                iprintf(" %-30s%c", line, suffix);
        }
    }

    iprintf("\x1b[12;0H");
    if (count == 0)
        iprintf("(dossier vide)");
    else if (total > count)
        iprintf("(%lu/%lu entrees, liste tronquee)", (unsigned long)count, (unsigned long)total);
    else
        iprintf("%lu entree(s)", (unsigned long)total);

    iprintf("\x1b[23;0H\x1b[36mA:OUVRIR B:RETOUR ST/SE:MODES\x1b[0m");
}
