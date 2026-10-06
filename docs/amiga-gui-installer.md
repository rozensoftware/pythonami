# Amiga GUI installer (pythonami)

HD-oriented installer that extracts an LHA, writes `uninstall.ami`, and can
append Assign/Path markers to `S:User-Startup`.

## Package format

Inside the LHA root: `install.ami` (`KEY=VALUE`). Required: `NAME`, `START`,
`DRAWER_NAME`. Optional: `ASSIGN`, `PATH_ADD`, `USER_STARTUP`, `VERSION`,
`README`.

After install, the target drawer contains generated `uninstall.ami` with
absolute `INSTALL_PATH` and marker lines for User-Startup.

## Uninstall

Deletes the installed drawer (all files). Does **not** rewrite User-Startup;
if `USER_STARTUP=1`, opens `ed S:User-Startup` so the user removes the marked
block. Global install registry is deferred.

## Plugins

| Plugin | Path |
|--------|------|
| GUI | `PROGDIR:ext/gui_intuition/gui_intuition.py68k` |
| ASL drawer | `PROGDIR:ext/asl/asl.py68k` |

Build: `make amiga-ext` (requires vbcc/vasm/vlink).

## Run (Amiga)

```text
lha a -r demo_package.lha demo_package
pythonami examples/installer/installer.py
```

Requires `lha` on PATH and write access to the chosen HD drawer.

## Runtime package shell scripts

The separate AmigaDOS scripts in `scripts/Install` and `scripts/Uninstall`
install the `pythonami` executable and `lib/` under `SYS:Python68k`, then
configure the current-session Assign and command Path. `Uninstall` removes the
runtime and `lib/` and clears those current-session entries. It does not edit
`S:User-Startup`; remove any persistent Assign/Path lines there manually.

## Related HAS docs

highamigaassembler: `docs/GUI_PYTHONAMI_API.md`, `docs/GUI_GADGETS_GUIDE.md`
(Progress + `set_label_text` / `set_progress` / `enable_widget` / `redraw`).
