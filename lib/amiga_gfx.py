# Layer 2 helpers for ext/amiga_gfx/amiga_gfx.py68k (no classes).
# Contract: docs/amiga-os-gfx.md / HAS AMIGA_OS_GFX_API.md

MODE_LORES = 0
MODE_HIRES = 32768
MODE_LACE = 4
MODE_SPRITES = 16384

EVT_NONE = 0
EVT_CLOSE = 1

MB_LEFT = 1
MB_RIGHT = 2
MB_MIDDLE = 4

# bob_create(bitmap, save_bg)
BOB_PAINT = 0
BOB_SAVE_BG = 1


def open_lores_8(gfx, x, y, w, h):
    return gfx.open_screen(x, y, w, h, 3, MODE_SPRITES)


def open_hires_4(gfx, x, y, w, h):
    return gfx.open_screen(x, y, w, h, 2, 49152)


def open_wb_window(gfx, title, x, y, w, h):
    return gfx.open_window(title, x, y, w, h)


def set_grey_ramp(gfx, count):
    i = 0
    while i < count:
        if count > 1:
            v = (i * 15) // (count - 1)
        else:
            v = 0
        gfx.color(i, v, v, v)
        i = i + 1
