# Amiga amiga_gfx blit + sprites fixture

Owner-run on AROS / WinUAE / hardware (not Musashi).

## Build

```text
export VBCC=/path/to/vbcc
make -f Makefile.amiga amiga-ext
```

## Run

```text
pythonami tests/integration/amiga/test_amiga_gfx_blit_sprites.py
```

Expect: LORES custom screen, bouncing blit tiles, up to 7–8 SimpleSprites
(`GetSprite(-1)`; Intuition pointer usually keeps HW sprite 0 → often 7).
Prints `sprites` / count. Joystick port 1 nudges the first sprite if
`lowlevel.library` is present. Quit: ESC / q / close.

Host syntax:

```text
build/host/pythonami --check tests/integration/amiga/test_amiga_gfx_blit_sprites.py
build/host/pythonami --check tests/integration/amiga/test_amiga_gfx_window.py
```
