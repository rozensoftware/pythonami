# Amiga amiga_gfx BOB save-background fixture (owner-run)
#
# Left bob: save_bg=1 — restores background when moving (no trail).
# Right bob: save_bg=0 — paint-only (leaves a trail like plain blit).
# Quit: ESC / q / close.
#
# Requires amiga_gfx.py68k with bob_* exports (39 total).

import sys
sys.path.append("lib")
import amiga_gfx

GFX_PLUGIN = "PROGDIR:ext/amiga_gfx/amiga_gfx.py68k"

gfx = load_library(GFX_PLUGIN)
if gfx.init() != 0:
    print("init fail")
    exit(1)

if amiga_gfx.open_lores_8(gfx, 0, 0, 320, 200) == 0:
    print("open_screen fail")
    gfx.shutdown()
    exit(1)

gfx.color(0, 0, 0, 2)
gfx.color(1, 15, 15, 15)
gfx.color(2, 15, 8, 0)
gfx.color(3, 0, 12, 4)

# Checker background so restore is obvious
gfx.ink(3)
y = 0
while y < 200:
    x = 0
    while x < 320:
        if ((x // 16) + (y // 16)) % 2 == 0:
            gfx.rect(x, y, 16, 16, 1)
        x = x + 16
    y = y + 16

gfx.ink(1)
gfx.text(8, 12, "bob save_bg vs trail  ESC/q")

img = gfx.alloc_bitmap(24, 24, 3)
if img == 0:
    print("alloc fail")
    gfx.close_screen()
    gfx.shutdown()
    exit(1)
gfx.bitmap_clear(img)
yy = 2
while yy < 22:
    xx = 2
    while xx < 22:
        gfx.bitmap_plot(img, xx, yy, 2)
        xx = xx + 1
    yy = yy + 1

# save_bg=1 remembers / pastes background on move
bob_clean = gfx.bob_create(img, 1)
# save_bg=0 paints only (clears nothing behind)
bob_trail = gfx.bob_create(img, 0)
if bob_clean < 0 or bob_trail < 0:
    print("bob_create fail")
    gfx.close_screen()
    gfx.shutdown()
    exit(1)

x0 = 40
y0 = 80
x1 = 200
y1 = 80
dx0 = 2
dy0 = 1
dx1 = -2
dy1 = 1

print("bobs ok")
running = 1
while running:
    if gfx.poll() == amiga_gfx.EVT_CLOSE:
        running = 0
    else:
        k = gfx.key()
        if k == 27:
            running = 0
        if k == 113:
            running = 0
        if k == 81:
            running = 0

        x0 = x0 + dx0
        y0 = y0 + dy0
        if x0 < 8:
            x0 = 8
            dx0 = 2
        if x0 > 140:
            x0 = 140
            dx0 = -2
        if y0 < 40:
            y0 = 40
            dy0 = 1
        if y0 > 160:
            y0 = 160
            dy0 = -1

        x1 = x1 + dx1
        y1 = y1 + dy1
        if x1 < 160:
            x1 = 160
            dx1 = 2
        if x1 > 280:
            x1 = 280
            dx1 = -2
        if y1 < 40:
            y1 = 40
            dy1 = 1
        if y1 > 160:
            y1 = 160
            dy1 = -1

        gfx.bob_draw(bob_clean, x0, y0)
        gfx.bob_draw(bob_trail, x1, y1)
        gfx.wait_tof()

gfx.bob_undraw(bob_clean)
gfx.bob_free(bob_clean)
gfx.bob_free(bob_trail)
gfx.free_bitmap(img)
gfx.close_screen()
gfx.shutdown()
print("done")
