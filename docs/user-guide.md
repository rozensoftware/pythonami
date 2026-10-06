# Python68K User Guide

Python68K runs a deliberately restricted Python-compatible language on a
68000 Amiga or on a modern development host. The executable is named
`pythonami`. This guide lists the commands normally needed to build, test, and
run it. Language syntax and builtin semantics are specified in
[`language-reference.md`](language-reference.md).

## Command-line interface

The command form is:

```text
pythonami [--debug] [--check] [-V|--version|--help] [-c command | script.py [args...]]
```

Options are processed before the input. `--debug` may precede `-V`, `--help`,
`-c`, or a script path. `--check` compiles and verifies the selected input but
does not execute it; use it before `-c` or the script path.

| Command | Example | Description |
| --- | --- | --- |
| Show version | `pythonami -V` | Prints the current version, `Python68K 0.8.0`. `--version` is the long form. |
| Show help | `pythonami --help` | Prints the usage line. Running `pythonami` with no input does the same. |
| Run a script | `pythonami examples/hello.py` | Loads, compiles, verifies, and executes one `.py` source file. |
| Pass script arguments | `pythonami examples/test_features.py one two` | Makes the script path and following arguments available as `sys.argv`. |
| Execute source text | `pythonami -c "print(1 + 2)"` | Compiles and executes the command string; its diagnostic path is `<string>`. |
| Debug a script | `pythonami --debug examples/hello.py` | Runs the script and writes top-level memory, source, token, bytecode, and stack statistics to stderr. |
| Check a script | `pythonami --check examples/hello.py` | Compiles and verifies without producing script output or performing top-level execution. |
| Check source text | `pythonami --check -c "print(1 + 2)"` | Validates command text without running it. |
| Combine debug and check | `pythonami --debug --check examples/hello.py` | Reports compilation statistics while still skipping execution. |

`--debug -V` and `--debug --help` print only the requested version/help text;
they do not emit a statistics report. Script output goes to stdout. Errors and
debug statistics go to stderr where the platform supports separate streams.

The process exit code is `0` for success. `exit(value)` returns a value
clamped to the range `0..255`; runtime, source, and I/O failures return the
runtime status. A user break maps to `10` on AmigaDOS. A script can inspect
its arguments with:

```python
import sys
print(sys.argv)
```

## Build commands

Run these from the repository root. The top-level `Makefile` forwards host
work to `Makefile.host` and Amiga work to `Makefile.amiga`.

| Command | Example | Description |
| --- | --- | --- |
| Default make target | `make all` | Shows the top-level help because the default `all` target is intentionally informational. |
| List top-level targets | `make help` | Shows the supported top-level targets and configured Amiga paths. |
| Host debug build | `make host` | Builds `build/host/pythonami` with GCC and debug-friendly flags. |
| Host release build | `make host MODE=release` | Builds the optimized host executable. |
| Host debug alias | `make debug` | Forwards to the host debug build. |
| Host release alias | `make release` | Forwards to the host release build. |
| Amiga release build | `make amiga` | Builds the `pythonami` Amiga Hunk executable with vbcc. |
| Amiga debug build | `make amiga MODE=debug` | Builds `pythonami-debug` with debug flags. |
| Explicit Amiga release | `make amiga-release` | Directly invokes the Amiga release target. |
| Explicit Amiga debug | `make amiga-debug` | Directly invokes the Amiga debug target. |
| Build sample extension | `make amiga-ext` | Builds `ext/demo_add/demo_add.py68k` with vbcc, vasm, and vlink. |
| Override Amiga tools | `make amiga VBCC=/path/to/vbcc NDK=/path/to/NDK3.2` | Supplies the vbcc installation and NDK include tree. |
| Remove build products | `make clean` | Removes host, Amiga, and sample-extension products. |

The host compiler can be selected with `HOST_CC`, for example:

```text
make -f Makefile.host test HOST_CC=clang
```

## Test commands

| Command | Example | Description |
| --- | --- | --- |
| Full host suite | `make test` | Builds/runs C unit tests, language fixtures, CLI smoke checks, and focused integration checks. |
| Language fixtures only | `make language-test` | Runs scripts under `examples/` and `tests/language/` and diffs stdout fixtures. |
| Import slice | `make -f Makefile.host import-test` | Runs import unit tests, language fixtures, and import integration checks. |
| Import failures | `make -f Makefile.host import-failure-test` | Checks missing-module, syntax-error, runtime-error, and cycle failures. |
| Debug statistics | `make -f Makefile.host debug-stats-test` | Checks `--debug` output, determinism, stderr routing, and failure status preservation. |
| Stream redirection | `make -f Makefile.host stdio-redirection-test` | Checks stdout/stderr separation for success, failure, and debug runs. |
| Check-only mode | `make -f Makefile.host check-mode-test` | Checks that `--check` validates source without executing it. |

On Windows, use a POSIX-compatible `make` environment or WSL. A host compiler
such as LLVM-MinGW Clang may be passed through `HOST_CC`. Amiga builds that
complete on the host prove compilation and linking only; run the resulting
Hunk executable under an Amiga emulator or on hardware for execution evidence.

## Common script examples

```text
build/host/pythonami examples/hello.py
build/host/pythonami examples/fibonacci.py
build/host/pythonami --check examples/test_features.py
build/host/pythonami --debug -c 'print("hello from Python68K")'
```

Use [`examples/`](../examples/) for runnable samples. The language intentionally
does not implement full CPython; consult the unsupported-feature list in
[`language-reference.md`](language-reference.md) before porting a script.

The language-level command and builtin inventory is maintained in
[`language-reference.md`](language-reference.md), including `print`, `input`,
collection constructors, conversions, text and file operations, time
functions, imports, process calls, and the Amiga-only extension loader. The
user-facing command list in this guide is limited to the `pythonami` CLI and
repository build/test commands.

## Language syntax implemented

This is a user-facing summary; the authoritative grammar and semantics are in
[`language-reference.md`](language-reference.md) and `docs/grammar.ebnf`.

- Values: `int`, `bool`, `None`, binary32 `float`, 8-bit `str`, `list`,
  `tuple`, `dict`, `set`.
- Operators: arithmetic `+ - * / // %`, comparisons `== != < <= > >=`,
  identity `is` / `is not`, membership `in` / `not in`, boolean `and` / `or` /
  `not`, conditional expressions `a if cond else b`.
- Statements: `if` / `elif` / `else`, `while … else`, `for … in iterable …
  else`, assignment and augmented assignment, fixed-count unpacking
  (`a, b = seq`), `break` / `continue` / `return` / `pass`, `try` / `except`
  / `finally`, `raise`, `with EXPR as NAME` (file handles only), `def`
  (no nested `def`, no closures), list/set/dict comprehensions, `yield`
  generators and generator expressions `(expr for x in iterable)`.
- Imports: `import name[ as alias]`, `from name import a[, b]`.
- Restricted f-strings: `f"...{expr!s|r|a:spec}..."`.

## Builtin commands

Builtins are called directly, without `import`, except for the `os` and `sys`
modules noted below. Full argument semantics are in
[`language-reference.md`](language-reference.md#builtins).

| Group | Builtins | Notes |
| --- | --- | --- |
| Output / input | `print`, `input` | `input([prompt])` returns one line without the trailing newline. |
| Collections | `len`, `range`, `list`, `tuple`, `dict`, `set`, `list_append`, `list_pop`, `sorted`, `iter`, `next` | `iter`/`next` share the `for`-loop cursor, also accept generators, and raise `StopIteration`. `list`/`tuple`/`set`/`sorted` consume a generator argument. |
| Conversion / inspection | `int`, `float`, `str`, `bool`, `abs`, `min`, `max`, `sum`, `ord`, `chr`, `repr`, `ascii`, `format`, `maketrans`, `all`, `any` | `ord`/`chr` operate on one byte (`0..255`); `format` supports a minimal int subset. `sum`/`all`/`any` consume a generator argument; `min`/`max` take two values and do not. |
| File I/O | `fopen`, `fclose`, `fread`, `freadline`, `fwrite`, `exists`, `remove`, `rename` | `fopen(path, mode)` accepts `r`/`w`/`a`/`rb`/`wb`/`ab`; file handles support `with fopen(...) as f:`. |
| Environment (host) | `getenv`, `setenv`, `unsetenv` | Host process environment only. |
| Amiga assigns | `assign_get`, `assign_add`, `assign_remove` | AmigaDOS logical assigns; Amiga build only. |
| Amiga extensions | `load_library(path)` | Loads a `*.py68k` LoadSeg plugin; Amiga build only. See [`amiga-extensions.md`](amiga-extensions.md). |
| Time | `time`, `sleep`, `ctime`, `localtime`, `strftime`, `perf_counter`, `time_tick` | `localtime` returns a `struct_time`-like object with `tm_*` attributes. |
| Break / scheduling | `check_break`, `yield_cpu`, `set_poll_interval`, `get_poll_interval` | Control the VM's backward-branch break check. |
| Process (via `import os`) | `os.system(command)`, `os.popen(command)` | `os.system` runs synchronously and returns the platform status; `os.popen` returns captured combined stdout/stderr text (not a file object). Both reject empty or NUL-containing commands. |
| Runtime | `exit([code])` | Exit code is clamped to `0..255`. |

`sys` is a builtin module (no separate import list needed beyond `import sys`)
exposing `sys.path`, `sys.modules`, and `sys.argv`.

## Example: file I/O and an external command

The following script was verified with `--check` (compiles, no output) and
then executed on the host build before being added to this guide:

```python
import os

f = fopen("RAM_TEST.txt", "w")
fwrite(f, "hello from user guide\n")
fclose(f)

f = fopen("RAM_TEST.txt", "r")
line = freadline(f)
fclose(f)
print(line)

status = os.system("echo user-guide-check")
print(status)

remove("RAM_TEST.txt")
```

Expected output on the host build:

```text
hello from user guide

user-guide-check
0
```

The blank line after the file contents is the newline written by `fwrite` and
echoed by `print`; `freadline` does not strip it. On Amiga, replace
`"RAM_TEST.txt"` with an AmigaDOS path such as `"RAM:test.txt"`.

## Platform notes

Host file APIs use stdio and host environment variables. Amiga builds use
AmigaDOS file handles and assigns, and only Amiga provides `load_library()` for
`*.py68k` extensions. `os.system(command)` runs synchronously and
`os.popen(command)` returns captured combined output; subprocess handles,
timeouts, and asynchronous process APIs are not supported.

## Comparison with standard Python semantics

Python68K intentionally implements a restricted subset of CPython. This
summarizes the differences a Python programmer should expect; see
[`compatibility.md`](compatibility.md) for the full rationale and design
decisions.

| Area | Standard Python | Python68K |
| --- | --- | --- |
| Numeric types | Arbitrary-precision `int`, `float` (double), `complex` | Checked 32-bit `int` (raises `OverflowError`), binary32 `float` (no NaN/Inf), no `complex` |
| Strings | Unicode `str`, separate `bytes`/`bytearray` | 8-bit `str` only; no `bytes`, `bytearray`, or `encode`/`decode` |
| Classes | `class`, instances, single inheritance, `__init__`, bound methods | Language Level 0.8; no MI/`super()`/descriptors |
| Functions | Closures, nested `def`, `*args`/`**kwargs`, default/keyword args | `def` without nesting or closures; methods are positional-only |
| Iteration | Generators with `send`/`throw`/`close`/`yield from`, `enumerate`, `reversed` | `yield` generators and generator expressions (free variables snapshotted at creation), comprehensions, `iter`/`next`; no `send`/`throw`/`close`/`yield from` |
| Unpacking | Starred (`a, *rest`) and nested (`(a, b), c = …`) targets | Fixed-count unpacking of list/tuple/string only |
| Imports | Packages, relative imports, `from x import *` | Single-level `.py` modules, explicit names only |
| File I/O | `open()`, file objects with `.read()`/`.write()`/`seek()` | `fopen`/`fread`/`freadline`/`fwrite`/`fclose` procedural API; no `seek` |
| Process control | `subprocess.run`/`Popen`, async process APIs | Synchronous `os.system` and `os.popen` (captured text only) |
| Error handling | `except (A, B)`, full MI MRO | Single-inheritance exception types (0.8); kind tags kept internally |
| Dynamic execution | `eval`, `exec`, `compile` | Not implemented |
| Pattern matching | `match` / `case` | Not implemented |

## Missing areas versus CPython

The following are not implemented in any Language Level through 0.8 and have
no scheduled release; treat scripts depending on them as unsupported until
`language-reference.md` records otherwise:

- Multiple inheritance, metaclasses, `super()`, descriptors/`property`, operator dunders.
- Unicode text and the `bytes`/`bytearray` types.
- Generator `send`/`throw`/`close()`, `yield from`, and `yield` as an expression.
- Closures and nested `def` outside class methods (generator expressions snapshot enclosing locals
  instead of capturing them).
- `async`/`await` and the `match` statement.
- Starred and nested unpacking (`a, *rest`; `(a, b), c = …`; `*args`/`**kwargs`).
- Relative imports, `from x import *`, and multi-level packages.
- File `seek`, encodings, and method-style `open()`/`file.read()`.
- Full `str.format`/`format_map`, and raw/bytes/nested f-string prefixes.
- `eval`, `exec`, `compile`.
- `enumerate`, `reversed`, and `iter(callable, sentinel)`.
- `subprocess`-style process control (argument lists, timeouts, async, per-child environment).

This list is sourced from the "Still not implemented" section of
[`language-reference.md`](language-reference.md); update both places together
when an item is implemented and evidenced by tests.