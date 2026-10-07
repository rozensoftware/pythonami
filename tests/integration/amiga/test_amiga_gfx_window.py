# Host --check / Amiga smoke: Workbench window draw+blit (no sprites required).
# On Amiga: opens a normal WB window, draws, blits a tile, polls mouse/key.
# Quit: close gadget / ESC / q.

import sys
sys.path.append("lib")
import amiga_gfx

GFX_PLUGIN = "PROGDIR:ext/amiga_gfx/amiga_gfx.py68k"

gfx = load_library(GFX_PLUGIN)
if gfx.init() != 0:
    print("init fail")
    exit(1)

# Call Layer 1 export directly (do not require lib/amiga_gfx.open_wb_window).
win = gfx.open_window("Gfx Window", 40, 30, 280, 160)
if win == 0:
    print("open_window fail")
    gfx.shutdown()
    exit(1)

gfx.ink(1)
gfx.text(8, 12, "WB window draw/blit")
gfx.line(8, 20, 200, 20)
gfx.rect(20, 40, 60, 40, 1)
gfx.circle(160, 70, 24, 0)

tile = gfx.alloc_bitmap(16, 16, 2)
if tile != 0:
    gfx.bitmap_clear(tile)
    y = 0
    while y < 16:
        x = 0
        while x < 16:
            gfx.bitmap_plot(tile, x, y, 1)
            x = x + 3
        y = y + 3
    gfx.blit(tile, 0, 0, 16, 16, 40, 100)
    gfx.free_bitmap(tile)

print("window ok - ESC/q/close")
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
        mx = gfx.mouse_x()
        my = gfx.mouse_y()
        if gfx.mouse_buttons() == amiga_gfx.MB_LEFT:
            gfx.ink(2)
            gfx.plot(mx, my)
        gfx.wait_tof()

gfx.close_screen()
gfx.shutdown()
print("done")
