# amiga_gfx Amiga smoke fixture (owner-run)
# Opens a custom screen, draws, waits for ESC/q/close.

import sys
sys.path.append("lib")
import amiga_gfx

GFX_PLUGIN = "PROGDIR:ext/amiga_gfx/amiga_gfx.py68k"

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
gfx.color(3, 0, 12, 0)
gfx.ink(1)
gfx.text(16, 16, "pythonami gfx")
gfx.ink(2)
gfx.plot(30, 40)
gfx.line(40, 50, 200, 90)
gfx.ink(3)
gfx.rect(50, 100, 90, 40, 1)
gfx.circle(220, 120, 28, 0)

bm = gfx.alloc_bitmap(32, 32, 3)
if bm != 0:
    gfx.bitmap_clear(bm)
    gfx.bitmap_plot(bm, 4, 4, 1)
    gfx.blit(bm, 0, 0, 32, 32, 10, 150)
    gfx.free_bitmap(bm)

print("gfx ok - press ESC/q or close")
while gfx.wait_event() != amiga_gfx.EVT_CLOSE:
    gfx.wait_tof()

gfx.close_screen()
gfx.shutdown()
print("done")
