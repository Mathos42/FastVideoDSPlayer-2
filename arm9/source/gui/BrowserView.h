#pragma once
#include <nds.h>
#include "../../../common/ipc.h"

#define BROWSER_VISIBLE_LINES 10

// Console-based (iprintf + ANSI) directory listing on the sub screen.
// Sprite-per-char rendering like PlayerView is not usable here: OAM is
// limited to 128 sprites and VRAM_I to 16KB, a 10-line list would need
// ~250 char sprites. The libnds console costs nothing and is already
// wired to BG2 sub. Display-only transliteration keeps accented names
// readable (the console font has no accents); the real UTF-8 names are
// untouched and used as-is for playback.
class BrowserView
{
public:
    void Initialize();
    void Render(const char* dirPath, const fv_dir_entry_t* entries, u32 count, u32 total, u32 cursor, u32 topLine);
};
