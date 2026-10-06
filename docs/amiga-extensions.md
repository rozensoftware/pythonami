# Amiga LoadSeg extensions

Python68K on Amiga can load relocatable Hunk plugins (convention: `*.py68k`) with
`LoadSeg` and expose their exports as callables on a module-like object.

Host builds do **not** provide `load_library`. This is Amiga-only (D-0027).

## Python usage

```python
lib = load_library("PROGDIR:ext/demo_add.py68k")
print(lib.add(2, 3))
print(lib.mul(2, 3))
```

`load_library(path)` returns a module object. Exported names are ordinary module
attributes and are invoked with the normal call syntax.

**Keep-alive rule:** keep the library module reachable while calling its
exports. When the module is released, pythonami clears its globals and calls
`UnLoadSeg`. Holding a function reference after the library module is gone is
undefined.

Failures raise catchable `ImportError` / `TypeError` / `ValueError` as
appropriate (`load_library` could not open the file, bad header, bad arguments).

## Plugin binary layout

After `LoadSeg(path)`, AmigaDOS prepends a next-segment longword to each hunk.
pythonami reads the extension header immediately after that longword:

```text
BADDR(seg) + 0  -> ULONG next_seg   (dos.library; do not modify)
BADDR(seg) + 4  -> Py68ExtHeader    (must be first payload of the first hunk)
```

Header (see [`include/py68k_ext.h`](../include/py68k_ext.h)):

| Field | Type | Value |
|-------|------|-------|
| `magic` | `ULONG` | `0x50593638` (`'PY68'`) |
| `abi_version` | `UWORD` | `1` |
| `export_count` | `UWORD` | `1..256` |
| `exports` | pointer | relocated pointer to `Py68ExtExport[]` |

Each export:

| Field | Meaning |
|-------|---------|
| `name` | C string in the hunk (relocated) |
| `min_args` / `max_args` | inclusive arity |
| `function` | `Py68ExtCallback` (same shape as internal natives) |

Callback contract (same as builtins):

- Arguments are **borrowed**; do not release them.
- On success return `PY68_STATUS_OK` and store **one owned** `Py68Value` in `*result`.
- Scalar helpers: `py68_ext_value_none`, `py68_ext_value_bool`, `py68_ext_value_int`.

Compile against the public headers only. Do **not** link `pythonami`,
`startup.o`, `vc.lib`, or NDK `amiga.lib` into the plugin (D-0004 / D-0027).

## Building the sample (`demo_add`)

```text
make amiga-ext
# or: make -f Makefile.amiga amiga-ext VBCC=... NDK=...
```

Produces `ext/demo_add/demo_add.py68k` from:

- `demo_add_header.s` — `Py68ExtHeader` at the start of the first CODE hunk
- `demo_add.c` — `add` in C + export table
- `demo_add_asm.s` — `mul` in Motorola 68000 assembler

Toolchain: `vc` (compile C), `vasmm68k_mot -Fhunk -m68000`, `vlink -bamigahunk -s`.

Also build the interpreter: `make amiga` → `./pythonami`.

## Writing your own function in vbcc C

1. Include `py68k_ext.h`.
2. Implement a callback:

```c
#include "py68k_ext.h"

Py68Status my_square(struct Py68Runtime *runtime, Py68U16 argument_count,
                     Py68Value *arguments, Py68Value *result)
{
    Py68I32 n;
    (void)runtime;
    (void)argument_count;
    if (arguments[0].type != PY68_VALUE_INT &&
        arguments[0].type != PY68_VALUE_BOOL)
        return PY68_STATUS_RUNTIME_ERROR;
    n = arguments[0].as.integer;
    *result = py68_ext_value_int(n * n);
    return PY68_STATUS_OK;
}
```

3. Publish it in a `Py68ExtExport` table and point a `Py68ExtHeader` at that table.
4. Ensure the header is the first payload of the first hunk (link a small header
   object first, as in `demo_add_header.s`).
5. Link without startup:

```text
vc +aos68k -cpu=68000 -fpu=0 -Iinclude -c -o myfn.o myfn.c
vasmm68k_mot -Fhunk -m68000 -o myhdr.o myhdr.s
vlink -bamigahunk -s -o myfn.py68k myhdr.o myfn.o
```

## Writing a function in vasm

Match the vbcc `+aos68k` C calling convention for:

```c
Py68Status fn(Py68Runtime *runtime, Py68U16 argument_count,
              Py68Value *arguments, Py68Value *result);
```

Typical stack on entry (68000):

| Offset | Content |
|--------|---------|
| `4(sp)` | `runtime` |
| `8(sp)` | `argument_count` (32-bit stack slot) |
| `12(sp)` | `arguments` |
| `16(sp)` | `result` |

`Py68Value` is 8 bytes: `type.u16`, `reserved.u16`, `payload.i32` / pointer.
Return status in `d0`. See `ext/demo_add/demo_add_asm.s` for a full `mul`.

Export the symbol with a leading underscore (`xdef _my_fn`) so vbcc/vlink
see `_my_fn` as `my_fn` from C tables.

## Checklist

- Target Motorola 68000, no FPU
- Relocatable string and function pointers (normal hunk relocs)
- Header magic/ABI correct; first hunk payload is the header
- No `startup.o` / `vc.lib` / `amiga.lib` in the plugin link
- Keep the `load_library` module alive while calling exports
- For owned `str`/`list` results, use `py68_ext_services(runtime)` (D-0048)
- Test on emulator or hardware (`tests/integration/amiga/`)

## gui_intuition dialog plugin

`make amiga-ext` also builds `ext/gui_intuition/gui_intuition.py68k` — Layer 1
Intuition dialogs for guicreator-exported pythonami forms. Contract:
highamigaassembler `docs/GUI_PYTHONAMI_API.md`. Layer 2 helper:
`lib/gui_dialog.py`. Owner fixture: `tests/integration/amiga/test_gui_intuition.md`.

Progress / live status exports (installer): `add_progress`, `set_progress`,
`set_label_text`, `enable_widget`, `redraw`.

## ASL drawer plugin

`make amiga-ext` builds `ext/asl/asl.py68k` with `ask_drawer(title, initial)`.
Used by `examples/installer/installer.py`. See `docs/amiga-gui-installer.md`.
