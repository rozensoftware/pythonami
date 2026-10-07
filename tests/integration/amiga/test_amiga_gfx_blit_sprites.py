# Amiga amiga_gfx blit + sprites + OS input fixture (owner-run)
#
# Custom LORES screen: blit offscreen tiles and animate SimpleSprites.
# Intuition's mouse pointer keeps hardware sprite 0, so OS apps typically
# get at most 7 SimpleSprites via GetSprite(-1). We request any free slot.
#
# Quit: ESC / q / close gadget.
# Joystick port 1 nudges the first sprite when lowlevel.library is present.
#
# Build: make -f Makefile.amiga amiga-ext
# Host:  build/host/pythonami --check tests/integration/amiga/test_amiga_gfx_blit_sprites.py

import sys
sys.path.append("lib")
import amiga_gfx

GFX_PLUGIN = "PROGDIR:ext/amiga_gfx/amiga_gfx.py68k"
WANT = 8

gfx = load_library(GFX_PLUGIN)
if gfx.init() != 0:
    print("init fail")
    exit(1)

scr = amiga_gfx.open_lores_8(gfx, 0, 0, 320, 200)
if scr == 0:
    print("open_screen fail")
    gfx.shutdown()
    exit(1)

gfx.color(0, 0, 0, 2)
gfx.color(1, 15, 15, 15)
gfx.color(2, 15, 4, 0)
gfx.color(3, 0, 12, 4)
gfx.color(4, 4, 4, 15)
gfx.color(17, 15, 15, 0)
gfx.color(18, 0, 15, 15)
gfx.color(19, 15, 0, 15)

gfx.ink(1)
gfx.text(8, 12, "blit+sprites  ESC/q quit")

tile_a = gfx.alloc_bitmap(24, 24, 3)
tile_b = gfx.alloc_bitmap(24, 24, 3)
if tile_a == 0 or tile_b == 0:
    print("alloc_bitmap fail")
    gfx.close_screen()
    gfx.shutdown()
    exit(1)

gfx.bitmap_clear(tile_a)
gfx.bitmap_clear(tile_b)
y = 2
while y < 22:
    x = 2
    while x < 22:
        gfx.bitmap_plot(tile_a, x, y, 2)
        gfx.bitmap_plot(tile_b, x, y, 3)
        x = x + 2
    y = y + 2

# prefer -1: next free HW sprite (skip Intuition pointer on slot 0)
slots = []
i = 0
while i < WANT:
    s = gfx.sprite_get(-1)
    if s < 0:
        i = WANT
    else:
        pat = (len(slots) % 3) + 1
        if gfx.sprite_box(s, 16, pat) != 0:
            print("sprite_box fail")
            gfx.close_screen()
            gfx.shutdown()
            exit(1)
        slots.append(s)
        i = i + 1

n = len(slots)
if n < 1:
    print("sprite_get fail")
    gfx.close_screen()
    gfx.shutdown()
    exit(1)

print("sprites")
print(n)

sx = [40, 70, 100, 130, 160, 190, 220, 250]
sy = [40, 50, 60, 70, 80, 90, 100, 110]
sdx = [1, -1, 1, -1, 1, -1, 1, -1]
sdy = [1, 1, -1, -1, 1, 1, -1, -1]

frame = 0
running = 1
while running:
    ev = gfx.poll()
    if ev == amiga_gfx.EVT_CLOSE:
        running = 0
    else:
        k = gfx.key()
        if k == 27:
            running = 0
        if k == 113:
            running = 0
        if k == 81:
            running = 0

        bx = 20 + ((frame * 2) % 200)
        by = 140
        gfx.blit(tile_a, 0, 0, 24, 24, bx, by)
        gfx.blit(tile_b, 0, 0, 24, 24, 280 - bx, by)

        mx = gfx.mouse_x()
        my = gfx.mouse_y()
        mb = gfx.mouse_buttons()

        sx[0] = sx[0] + gfx.joy_x(1) * 2
        sy[0] = sy[0] + gfx.joy_y(1) * 2

        i = 0
        while i < n:
            sx[i] = sx[i] + sdx[i]
            sy[i] = sy[i] + sdy[i]
            if sx[i] < 16:
                sx[i] = 16
                sdx[i] = 1
            if sx[i] > 300:
                sx[i] = 300
                sdx[i] = -1
            if sy[i] < 30:
                sy[i] = 30
                sdy[i] = 1
            if sy[i] > 180:
                sy[i] = 180
                sdy[i] = -1
            gfx.sprite_move(slots[i], sx[i], sy[i])
            i = i + 1

        if mb == amiga_gfx.MB_LEFT:
            gfx.ink(4)
            gfx.plot(mx, my)

        frame = frame + 1
        gfx.wait_tof()

gfx.free_bitmap(tile_a)
gfx.free_bitmap(tile_b)
gfx.close_screen()
gfx.shutdown()
print("done")
