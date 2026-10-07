# Amiga OS Graphics (pythonami)

OS-cooperative Intuition graphics for Python68K. No TakeSystem / `$DFF0xx`.
Contract: sibling HAS `docs/AMIGA_OS_GFX_API.md`.

## Display modes

| API | What you get |
|-----|----------------|
| `open_screen(x,y,w,h,depth,mode)` | Custom screen (own palette). Draw on screen RastPort. SimpleSprites OK with `MODE_SPRITES`. |
| `open_window(title,x,y,w,h)` | **Normal Workbench window** with close/depth/drag. Draw + blit on `Window->RPort`. Sprites use the WB screen ViewPort (pointer shares slots). |

Yes: a normal window supports plot/line/rect/circle/text/blit. Palette `color()` on WB changes shared pens—prefer custom screens for full palette ownership.

## Input (AmigaOS only)

| Call | Source |
|------|--------|
| `poll()` | Non-blocking IDCMP drain; returns `EVT_CLOSE` or `0` |
| `wait_event()` | Blocking Wait + drain |
| `key()` / `rawkey()` | Last VANILLAKEY / RAWKEY (cleared on read) |
| `mouse_x()` / `mouse_y()` / `mouse_buttons()` | Window-relative; buttons 1=LMB 2=RMB 4=MMB |
| `joy(port)` | Raw `lowlevel.library` ReadJoyPort (0 if library missing) |
| `joy_x` / `joy_y` / `joy_fire` | Decoded −1/0/1 and fire (no bitwise ops needed in Python) |

## Blit vs BOB

| Call | Behaviour |
|------|-----------|
| `blit(bm, sx,sy,w,h, dx,dy)` | One-shot copy; leaves whatever was underneath covered. |
| `bob_create(bm, save_bg)` | Soft BOB from a bitmap. `save_bg=0`: paint-only (trail when moved). `save_bg=1`: stash screen pixels under the BOB and paste them back before each move / on `bob_undraw`. |
| `bob_draw(bob, x, y)` | Draw (and restore old bg if `save_bg`). |
| `bob_undraw(bob)` / `bob_free(bob)` | Restore bg (if saved) then free. |

Keep image bitmaps alive until after `bob_free`. Overlapping save-bg BOBs: undraw in reverse draw order.

## Sprites

`sprite_get(prefer)` → `sprite_box(slot, height, pattern)` → `sprite_move`.
Use `prefer=-1` for the next free hardware sprite. Prefer `0..7` requests that
slot; **sprite 0 is usually held by the Intuition pointer**, so `sprite_get(0)`
fails unless you fall back (the plugin retries with `-1`). Under OS you typically
get **at most 7** SimpleSprites while the mouse pointer is active.

## Usage

```python
import sys
sys.path.append("lib")
import amiga_gfx

gfx = load_library("PROGDIR:ext/amiga_gfx/amiga_gfx.py68k")
gfx.init()
# Custom screen + sprites:
amiga_gfx.open_lores_8(gfx, 0, 0, 320, 200)
# Or Workbench window:
# amiga_gfx.open_wb_window(gfx, "Demo", 40, 30, 280, 160)
gfx.ink(1)
gfx.text(8, 12, "hello")
while gfx.poll() != amiga_gfx.EVT_CLOSE:
    gfx.wait_tof()
gfx.close_screen()
gfx.shutdown()
```

Build: `make amiga-ext`. Fixtures: `tests/integration/amiga/test_amiga_gfx*.py`.
