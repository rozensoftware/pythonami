# Python68K

**Version 0.8.0** — Copyright © 2026 Piotr Rozentreter (Rozsoft)

Python68K is a deliberately restricted, Python-compatible language and runtime for classic **Motorola 68000** Amiga systems (AmigaOS 2.x+), with a modern **Linux/host** build for development and testing.

Source is compiled to a custom bytecode format and executed on an explicit stack VM. The implementation is portable **ANSI C89**, cross-compiled with **vbcc** (`+aos68k`) for Amiga Hunk executables and with **GCC** on the host.

Executable name: `pythonami`

---

## Features (Language Level 0.1–0.8)

### Values and operators
- Scalars: integers (signed 32-bit, checked overflow), IEEE-754 binary32 `float` (soft-float, no NaN/Inf), `True` / `False`, `None`
- Arithmetic: `+ - * / // %` (`/` is true divide → float; `//` is floor-int)
- Comparisons: `== != < <= > >=` (bool as 0/1; `None == None`; str/list/tuple equality)
- Membership: `x in y` / `x not in y` (str substring; list/tuple equality scan; dict keys; set members)
- Identity: `x is y` / `x is not y` (None/bool/int/float by type+payload; heap objects by pointer)
- Unary: `+ - not`
- Short-circuit `and` / `or` (value-preserving; empty containers are falsy)
- Conditional expressions: `then if condition else else` (right-associative else-chains)

### Control flow
- `if` / `elif` / `else` (statements). Value-level `then if cond else else` is an expression (D-0040).
- `while` … `else`, `for … in` range/list/tuple/dict/set/string … `else`
- `for a, b in pairs:` unpacks each item (fixed name targets; D-0038)
- Multiple assignment: `a, b = (1, 2)`, `a, b = [1, 2]`, `a, b = 1, 2`
- `break` / `continue` (including `break` inside `for`)
- `try` / `except` / `except Type as e` / `finally` / `raise`
- `with fopen(...) as f`

### Data
- Lists: literals, index get/set, concat, slice, `list_append` / `list_pop` and `.append` / `.pop`
- Tuples: parenthesized `(a, b)`, `(a,)`, `()`; assignment also allows
  unparenthesized `a, b = 1, 2` and `x = 1, 2` (D-0038)
- Dicts: `{k: v}`, subscript get/set, `.get` / `.keys` / `.values` / `.items` / `.pop`
- Sets: `{a, b}`, `set()`, `.add` / `.remove` / `.discard`
- Comprehensions (0.6): `[expr for x in iterable if cond]`, nested `for`,
  `{expr for ...}`, `{k: v for ...}`. Targets bind like `for` (no nested scope).
- Strings: literals, concat, index, slice, `len`
- Limited attributes (`obj.name` → bound method or module export)
- `range(stop)`, `range(start, stop)`, `range(start, stop, step)`

### Functions
- `def` with parameters and locals, recursion, explicit/`None` return
- Local shadowing of globals; unbound local reads raise errors
- Augmented assignment: `+= -= *= //= %=`

### Generators (0.7)

- `yield` in a `def` makes it a generator factory; calling it builds a generator
  and runs no body code until it is resumed
- `for x in gen`, `iter(gen)`, `next(gen[, default])`; a finished generator
  iterates as empty and `next` raises catchable `StopIteration`
- Generator expressions `(expr for x in iterable if cond)`, unparenthesized when
  they are a call's only argument (`sum(x for x in range(5))`)
- `list` / `tuple` / `set` / `sorted` / `sum` / `all` / `any` consume generators
- Free variables of an enclosing function are snapshotted when the generator is
  created (D-0046); no `send` / `throw` / `close()` / `yield from`

### Classes (0.8)

Module-level `class` with single inheritance, instance `__dict__`, `__init__`,
bound methods, `isinstance`/`issubclass`, and exception type objects.

### Imports (0.5)
- `import name`, `import name as alias`, `from name import a, b`
- Search: importer directory, then `sys.path`
- Builtin module `sys` (`path`, `modules`, `argv`)

### Builtins (core)
`print`, `input`, `len`, `range`, `list`, `tuple`, `dict`, `set`, `list_append`, `list_pop`, `int`, `float`, `str`, `bool`, `abs`, `min`, `max`, `exit`, `sum`

`input([prompt])` writes an optional prompt (no newline), flushes stdout, reads one line from the console, and returns it without the trailing newline.

### File I/O (0.2.0)
Function builtins plus `with fopen(...) as f` (`__enter__`/`__exit__` close the handle):

| Builtin | Notes |
|---------|--------|
| `fopen(path, mode)` | modes: `r` `w` `a` `rb` `wb` `ab` |
| `fclose(handle)` | |
| `fread(handle, count)` | returns string (raw bytes in binary mode) |
| `freadline(handle)` | one line as string |
| `fwrite(handle, string)` | returns byte count |
| `exists(path)` / `remove(path)` / `rename(old, new)` | |

Amiga uses DOS `Open`/`Read`/`Write`/`Seek`/`Close`/`Lock`/`DeleteFile`/`Rename`. Host uses stdio.

### Environment / assigns (0.2.0)

| Host | Amiga (target) |
|------|----------------|
| `getenv(name)` | `assign_get(name)` |
| `setenv(name, value)` | `assign_add(name, path)` via `AssignPath` |
| `unsetenv(name)` | `assign_remove(name)` via `AssignLock(name, 0)` |

Amiga also provides `load_library(path)` for LoadSeg `*.py68k` plugins (vbcc/vasm);
see `docs/amiga-extensions.md`. Host has no `load_library`.

### Tooling
- CLI: `pythonami script.py`, `pythonami --check script.py`, `pythonami -c "..."`, `-V` / `--help`
- Pipeline: tokenize → parse (AST) → symbol analysis → compile → verify → VM execute
- Host unit tests and language fixture diffs (`make test`)
- Error reporting with frame traceback

### Not in 0.8.0
Multiple inheritance, metaclasses, `super()`, descriptors, operator dunders, Unicode, bytes, generator `send` / `throw` / `close()` / `yield from`, closures, nested `def` outside methods, method-style `open()` / `file.read()` / keyword `encoding=` / `FileNotFoundError` (planned beyond 0.8; `fopen` remains), seek, relative imports, `from x import *`, Amiga `ENV:` GetVar/SetVar, frozen emulator/hardware differential sign-off.

---

## Quick start

```bash
make help                 # list targets
make host                 # → build/host/pythonami (debug)
make host MODE=release
make amiga                # → ./pythonami (Amiga release Hunk)
make amiga MODE=debug     # → ./pythonami-debug
make amiga-ext            # → ext/demo_add/demo_add.py68k (vasm/vlink)
make test                 # host unit + language tests
make clean
```

```bash
./build/host/pythonami -V
./build/host/pythonami examples/hello.py
./build/host/pythonami examples/test_features.py
./build/host/pythonami -c 'print(1 + 2 * 3)'
```

For the complete command list, small examples, and common workflows, see
[`docs/user-guide.md`](docs/user-guide.md). Contributors should start with
[`docs/developer-guide.md`](docs/developer-guide.md).

Amiga toolchain defaults (override if needed):

```text
VBCC=/home/piotr/local/vbcc
NDK=/run/media/piotr/BACKUP/Rozen/Programy/Amiga/NDK3.2
```

```bash
make amiga VBCC=/path/to/vbcc NDK=/path/to/NDK3.2
```

---

## Architecture

```text
source → tokenizer → parser (AST) → symbols → compiler → verifier → stack VM
```

| Area | Role |
|------|------|
| Host | GCC, sanitizers, fast unit/language tests |
| Amiga | vbcc `+aos68k`, `-cpu=68000 -fpu=0`, AmigaDOS I/O via platform layer |
| Memory | Tracked allocator, refcounted objects, explicit ownership |
| Bytecode | Stable opcodes, big-endian operands, verified before execution |

Details: `docs/architecture.md`, `docs/bytecode.md`, `docs/language-reference.md`, `docs/memory-model.md`.

---

## Repository layout

```text
src/           Implementation (.c)
include/       Public headers (py68k_*.h)
platform/      Amiga compatibility shims
config/        Host / Amiga config headers
examples/      Sample scripts (hello, fibonacci, test_features, …)
tests/unit/    Host C unit tests
tests/language/  Python68K scripts + expected stdout fixtures
docs/          Architecture, builds, testing, decisions
Makefile       Easy entry: host / amiga / test
Makefile.host  GCC host build and tests
Makefile.amiga vbcc Amiga build
```

---

## Documentation

| Document | Contents |
|----------|----------|
| `docs/language-reference.md` | What is executable |
| `docs/host-build.md` | Host GCC build |
| `docs/amiga-build.md` | vbcc / AmigaOS build and Workbench notes |
| `docs/amiga-extensions.md` | LoadSeg `*.py68k` plugins (`load_library`) |
| `docs/testing.md` | Test coverage narrative |
| `docs/debug-statistics.md` | `--debug` report fields and interpretation |
| `docs/decisions.md` | Design decisions (D-0001…) |
| `docs/architecture.md` | Pipeline and module overview |
| `docs/user-guide.md` | CLI, build, test, and script-running commands |
| `docs/developer-guide.md` | Source layout, implementation pipeline, and contributor workflow |
| `CHANGELOG.md` | Release history |

Normative design briefs (for implementers): `Python68K_Full_Agent_Implementation_Brief.md`, `Python68K_Final_Implementation_Checklist.md`.

## Current verification status

The host build and test suite are the primary executable checks. They cover the
documented Language Levels 0.1-0.8, CLI smoke checks, imports, process APIs,
debug statistics, standard-stream separation, and `--check` mode. Amiga
debug/release builds are compile/link-verified when vbcc and the NDK are
available; execution on an emulator or real 68000 hardware remains a separate
owner verification step. See [`docs/testing.md`](docs/testing.md) for the
evidence and boundaries of each check.

---

## License

MIT License.

Copyright © 2026 Piotr Rozentreter (Rozsoft).

See LICENSE for the full license text.
