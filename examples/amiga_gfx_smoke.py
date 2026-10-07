# Host --check smoke for amiga_gfx Layer 2 helpers (no load_library on host).
import sys
sys.path.append("lib")
import amiga_gfx
print(amiga_gfx.MODE_LORES)
print(amiga_gfx.MODE_HIRES)
print(amiga_gfx.MODE_SPRITES)
print(amiga_gfx.EVT_CLOSE)
print(amiga_gfx.MB_LEFT)
print("amiga_gfx helpers ok")
