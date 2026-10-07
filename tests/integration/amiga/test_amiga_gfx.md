# Amiga amiga_gfx LoadSeg fixture (owner-run)

Smoke for `ext/amiga_gfx/amiga_gfx.py68k` (OS custom screen). Musashi cannot
validate Intuition `OpenScreen`; run on AROS / WinUAE / hardware.

## Build

```text
make -f Makefile.amiga amiga-ext
```

Place `pythonami` and `ext/amiga_gfx/amiga_gfx.py68k` so the path in the script
resolves (typically under `PROGDIR:`).

## Run

```text
pythonami tests/integration/amiga/test_amiga_gfx.py >T:py68k-gfx-out
```

Expect: opens a 320×200×3 LORES screen, draws plot/line/rect/circle/text, waits
for ESC/q/close, then shuts down cleanly.

Host syntax only:

```text
./python --check tests/integration/amiga/test_amiga_gfx.py
./python --check examples/amiga_gfx_smoke.py
```
