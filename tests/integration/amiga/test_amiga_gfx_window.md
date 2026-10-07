# Amiga amiga_gfx Workbench window fixture

Owner-run. Opens a normal WB window; draw + blit on the window RastPort.
Quit: close gadget / ESC / q.

Requires a rebuilt `ext/amiga_gfx/amiga_gfx.py68k` that exports `open_window`
(35 exports). Copy that plugin next to `PROGDIR:`; `lib/amiga_gfx.py` is
optional for this script (it calls `gfx.open_window` directly).

```text
pythonami tests/integration/amiga/test_amiga_gfx_window.py
```

Host: `build/host/pythonami --check tests/integration/amiga/test_amiga_gfx_window.py`
