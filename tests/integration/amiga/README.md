# Amiga stdout/stderr redirection fixture

This is a manual integration specification for an AmigaDOS emulator or real Amiga hardware. It is not automatically run by `Makefile.host` or `Makefile.amiga`.

Run from the directory containing the release Hunk executable `pythonami` (use `pythonami-debug` for a debug build). AmigaDOS `$RC` is the process return code.

On Kickstart / dos.library before V47, Amiga Shell `2>` may be unavailable and
stderr often shares the console with stdout; the binary still must not crash
when writing diagnostics or `--debug` statistics.

```text
pythonami -c "print(42)" >T:py68k-stdout 2>T:py68k-stderr
echo $RC
```

Expected: `$RC` is `0`; `T:py68k-stdout` contains exactly `42` followed by a newline; `T:py68k-stderr` is empty.

```text
pythonami -c "print(1 // 0)" >T:py68k-stdout 2>T:py68k-stderr
echo $RC
```

Expected: `$RC` is `11`; `T:py68k-stdout` is empty; `T:py68k-stderr` contains a line beginning with `ZeroDivisionError:`.

```text
pythonami --debug -c "print(42)" >T:py68k-stdout 2>T:py68k-stderr
echo $RC
```

Expected: `$RC` is `0`; `T:py68k-stdout` contains exactly `42` followed by a newline; `T:py68k-stderr` contains both `--- Python68K debug statistics (top-level source) ---` and `--- end Python68K debug statistics ---`, and does not contain a line consisting of `42`.

Inspect the three files after each command and remove them when finished:

```text
delete T:py68k-stdout
delete T:py68k-stderr
```

The host counterpart is `make -f Makefile.host stdio-redirection-test`.

## DOS command execution fixture

The shared host/Amiga language fixture is
`tests/language/process/test_system.py`. After building `pythonami`, run it
from the directory containing the executable and the repository tree:

```text
pythonami tests/language/process/test_system.py >T:py68k-system-out
echo $RC
type T:py68k-system-out
```

Expected `$RC` is `0`, and `T:py68k-system-out` contains:

```text
PY68K_SYSTEM_OK
0
PY68K_SYSTEM_AGAIN
0
type-error
```

The two `echo` lines are emitted by AmigaDOS itself. The following `0` values
are the direct return codes from `os.system()`. The second import verifies
that the built-in `os` module is returned from the import cache. Remove the
temporary output after the check:

```text
delete T:py68k-system-out
```

## DOS command output-capture fixture

This test requires a mounted `PIPE:` handler. Run:

```text
pythonami tests/language/process/test_popen.py >T:py68k-popen-out
echo $RC
type T:py68k-popen-out
```

Expected `$RC` is `0`, and `T:py68k-popen-out` contains:

```text
PY68K_POPEN_OK
type-error
```

The command's stdout travels through a unique `PIPE:` object. Error output is
shell/OS-version dependent because `SYS_Error` is not available before V50.
Remove the fixture output:

```text
delete T:py68k-popen-out
```

## LoadSeg `load_library` fixture

Build the interpreter and sample plugin on a machine with vbcc/vasm/vlink:

```text
make amiga
make amiga-ext
```

Place `pythonami` and `ext/demo_add/demo_add.py68k` so the script’s path resolves
(default: `demo_add.py68k` in the current directory). Then:

```text
pythonami tests/integration/amiga/test_load_library.py >T:py68k-ext-out
echo $RC
type T:py68k-ext-out
```

Expected: `$RC` is `0`; `T:py68k-ext-out` contains:

```text
5
6
True
```

Details: `tests/integration/amiga/test_load_library.md` and `docs/amiga-extensions.md`.

## gui_intuition dialog smoke fixture

After `make amiga-ext`, place `ext/gui_intuition/gui_intuition.py68k` so
`PROGDIR:ext/gui_intuition/gui_intuition.py68k` resolves (or edit the script).

```text
pythonami tests/integration/amiga/test_gui_intuition.py >T:py68k-gui-out
echo $RC
type T:py68k-gui-out
```

Expected: `$RC` is `0`; stdout:

```text
init_ok
shown
closed
```

Details: `tests/integration/amiga/test_gui_intuition.md`. Owner-verified on
emulator/hardware only.

## amiga_gfx custom-screen smoke fixture

After `make amiga-ext`, place `ext/amiga_gfx/amiga_gfx.py68k` so
`PROGDIR:ext/amiga_gfx/amiga_gfx.py68k` resolves.

```text
pythonami tests/integration/amiga/test_amiga_gfx.py >T:py68k-gfx-out
```

Opens a LORES custom screen, draws, waits for ESC/q/close. Musashi cannot
validate Intuition. Details: `tests/integration/amiga/test_amiga_gfx.md`.

Blit + 8 sprites + input: `test_amiga_gfx_blit_sprites.py` (see `.md`).
Workbench window draw/blit: `test_amiga_gfx_window.py`.
BOB save-background vs trail: `test_amiga_gfx_bob.py`.
