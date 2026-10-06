# Changelog

## [Unreleased]

### Added

- **Amiga GUI installer foundation:** `examples/installer/` plus `lib/install_*.py`
  (`install.ami` / `uninstall.ami`, LHA extract via `lha`, User-Startup upsert,
  safe uninstall that opens `ed S:User-Startup`). Plugins: extended
  `gui_intuition` (progress + label/enable/redraw) and new `ext/asl`
  (`ask_drawer`). Doc: [docs/amiga-gui-installer.md](docs/amiga-gui-installer.md).
  Host check: `examples/test_install_ami.py`.
- **AmigaDOS runtime install scripts:** `scripts/Install` now installs under
  `SYS:Python68k` and copies `scripts/Uninstall`. Uninstall removes the runtime,
  its `lib` drawer, and current-session Assign/Path entries; manually added
  `S:User-Startup` lines must still be removed by the user.

## 0.8.0

- Language Level 0.8: classes and instances with single inheritance.
  `class Name:` / `class Name(Base):` at module level; restricted class body
  (`pass`, simple assignment, `def`); per-instance `__dict__`; `__init__`;
  bound user methods; `isinstance` / `issubclass` / `object` / `type(obj)`.
- Builtin exceptions are real type objects under `BaseException` /
  `Exception`. `except Exception` matches by single-inheritance subtype.
  Internal `Py68ErrorKind` tags remain for C raise paths and logging; user
  exception subclasses use catchable `PY68_ERROR_EXCEPTION` plus a type
  pointer (D-0049, D-0050).
- `__build_class__` builtin builds types from a namespace dict.
- Multiple inheritance, metaclasses, descriptors, `super()`, operator
  dunders, nested classes, and closures remain out of scope.

## 0.7.2

- Extension ABI (D-0048): `Py68ExtServices` on `Py68Runtime` lets LoadSeg
  plugins build owned `str`/`list` results without linking the interpreter.
- New Amiga plugin `ext/gui_intuition/gui_intuition.py68k` — Layer 1 Intuition
  dialog API for guicreator-exported forms (`make amiga-ext`). Behavioral
  reference: HAS `lib/gui_intuition.s`; not linked from that object.
- Pure-Python `lib/gui_dialog.py` Layer 2 helper (`run_modal` / `run_modal_fields`).
- Owner Amiga fixture: `tests/integration/amiga/test_gui_intuition.*`.
- Host unit test: `tests/unit/test_ext_services.c`.
- gui_intuition close path matches HAS/RKM `CloseWindowSafely` (drain before
  `ModifyIDCMP(0)`); button/string borders match HAS bevels (`GFLG_GADGHNONE`,
  recessed edit frame with −2 inset) to fix close crashes and white/missing
  frames.

## 0.7.1

- Module identity: direct scripts and `-c` commands expose `__name__` as
  `"__main__"`; imported modules expose their canonical import name, including
  when accessed through an alias.
- Version string `Python68K 0.7.1`.

## 0.7.0

- Language Level 0.7: generators. `yield` in a module-level `def` makes the
  function a generator factory; calling it builds a `PY68_OBJECT_GENERATOR` and
  runs no body code. `for`, `iter`, and `next` resume generators, and an
  exhausted generator iterates as empty (D-0045). `send`, `throw`, `close()`,
  and `yield from` are not implemented.
- Generator state lives in the generator object (locals, private operand stack
  sized once to `maximum_stack`, try stack, resume IP), so suspending never
  allocates and resuming never uses C recursion. Dropping the last reference
  frees locals and saved operands without running user `finally` blocks
  (D-0045).
- New `OP_YIELD_VALUE` (0x3C) and `Py68Code.is_generator`; the verifier rejects
  `OP_YIELD_VALUE` outside a code object marked as a generator.
- Generator expressions `(elt for x in iterable if cond)`, including the
  unparenthesized form as a call's only argument (`sum(x for x in range(5))`);
  alongside another argument it is a syntax error, as in CPython.
  They compile to a synthetic nested generator code object; the outermost
  iterable is evaluated eagerly and free variables of an enclosing function are
  snapshotted as hidden arguments, so later rebinding is not observed
  (D-0046, a documented divergence from CPython closures).
- `list`, `tuple`, `set`, `sorted`, `sum`, `all`, and `any` drain a generator
  argument into a list inside the VM before the native callback runs, so no
  builtin re-enters the interpreter (D-0047). `min`, `max`, `len`, and `in` do
  not accept generators.
- A builtin called with the wrong number of arguments now raises a catchable
  `TypeError` naming the builtin and its accepted count; it previously failed
  with a blank `Exception:` line.

- I/O failures report as `OSError` (Python 3); `IOError` remains an accepted
  alias for the same catchable kind (D-0044).

- Restricted f-strings: `f"...{expr}...{expr!r}...{expr:04d}..."` with
  `!s`/`!r`/`!a`, literal format specs matching `format()`, and `{{`/`}}`
  escapes (D-0043). Nested f-strings, raw/bytes prefixes, and `str.format`
  methods remain deferred.

- Builtins `iter(x)` / `next(it[, default])` over the existing `Py68Range`
  cursor used by `for`; catchable `StopIteration` (D-0042). `enumerate` /
  `reversed` still deferred; `for` bytecode unchanged.

- Builtin `sum(iterable[, start])` for list/tuple of numbers; optional numeric
  `start` defaults to `0`. Checked int overflow; float promotion matches `+`
  (D-0041).

- Conditional expressions: `then if condition else else` (`PY68_AST_IF_EXP`).
  Evaluates the condition, then exactly one branch. Lower precedence than
  `or`/`and`/comparisons; else-chains are right-associative
  (`a if c1 else b if c2 else c`). Compiles to `JUMP_IF_FALSE` / `JUMP`
  (D-0040). Comprehension `for`/`if` clauses stay `or_test` filters.
  Statement `if`/`elif`/`else` is unchanged.

- Fixed-count unpacking / multiple assignment: `a, b = (1, 2)`, `a, b = [1, 2]`,
  `a, b = 1, 2`, `for a, b in pairs:`, and string unpack `a, b = "ab"`.
  `OP_UNPACK` (0x0F). Length mismatch is `ValueError`; non-sequence is
  `TypeError`. Assignment RHS is an expression list (D-0038). Starred and
  nested unpacking remain unsupported. D-0015 still requires parentheses for
  tuples outside assignment (`return a, b` is still a syntax error).

- Identity operators `is` / `is not` (`OP_IS` / `OP_IS_NOT`). Immediate
  None/bool/int/float values compare by type and payload; heap objects compare
  by pointer (D-0037). `is not` is one comparison operator, not `is` plus unary
  `not`.
- Membership operators `in` / `not in` (`OP_CONTAINS` / `OP_NOT_CONTAINS`) for
  str substring, list/tuple equality scan, dict keys, and set members.
- `for` / comprehensions iterate strings as successive one-character strings.
- Example `examples/wordcount.py` fixed for Level APIs (`OSError`, `fread`).
- `fread` allocates from remaining file size (Seek/ftell), not the full count,
  so large “read all” counts work on Amiga without multi-GiB pre-allocation.

- Amiga LoadSeg extensions: `load_library(path)` loads `*.py68k` plugins with a
  public export ABI (`include/py68k_ext.h`); sample `ext/demo_add` (vbcc C +
  vasm) via `make amiga-ext` (D-0027).
- Version string `Python68K 0.7.0`.

## 0.6.0

- Language Level 0.6: list, set, and dict comprehensions with nested `for`
  clauses and `if` filters. Targets bind in the enclosing function or module
  scope, matching `for` (D-0026). Generator expressions remain rejected.
- Comprehension bytecode uses `OP_LIST_APPEND` / `OP_SET_ADD` / `OP_MAP_ADD`
  with existing `RANGE_INIT`/`RANGE_NEXT` loops; no nested functions.
- Amiga: avoid calling `ErrorOutput()` on dos.library < V47 (fixes `--debug` /
  stderr Guru on Kickstart 2.x–3.1); use `pr_CES` or `Output()` instead.
- String methods (ASCII/8-bit): case, search, trim, split/join, replace, align,
  expandtabs, translate, classifiers; bound via `attr.c`.
- Text builtins: `ord`, `chr`, `repr`, `ascii`, `all`, `any`, `format` (minimal
  int specs), `maketrans` (returns dict). Literal escapes `\\ \' \" \n \r \t \xHH`
  decoded at load. See D-0024 / D-0025.
- Version string `Python68K 0.6.0`.

## 0.5.0

- Language Level 0.3: tuple, dict, set, limited attributes, bound methods, value equality/hash, IEEE-754 binary32 float, `/` true divide vs `//` floor divide.
- Language Level 0.4: catchable exception objects, `try`/`except`/`finally`/`raise`, `with fopen(...) as f`.
- Language Level 0.5: `import` / `from` / `as`, module objects, loader cache, builtin `sys` (`path`, `modules`, `argv`).
- Targeted diagnostics for remaining unsupported keywords (`class`, `lambda`, …).
- Version string `Python68K 0.5.0`.

## 0.2.0

- File builtins: `fopen`/`fclose`/`fread`/`freadline`/`fwrite`/`exists`/`remove`/`rename`
  with modes `r`/`w`/`a`/`rb`/`wb`/`ab` (AmigaDOS `Open` on target; stdio on host).
- Console `input([prompt])` via stdin / AmigaDOS `Input()`.
- Host environment: `getenv`/`setenv`/`unsetenv`.
- Amiga DOS assigns: `assign_get`/`assign_add`/`assign_remove` (`AssignPath` /
  `AssignLock` / `Lock`+`NameFromLock`).
- `PY68_OBJECT_FILE` with close-on-release; host unit + language fixtures.
- Version string `Python68K 0.2.0`.

## 0.1.0

- Language Level 0.1 runtime: tokenize → parse → symbols → compile → verify → stack VM.
- Scalars, arithmetic, comparisons, control flow, lists/strings, functions, builtins.
- Host GCC build (`make host`) and Amiga vbcc build (`make amiga`).
- Host unit tests and language fixture suite (`make test`).
- Copyright preamble on all C/H sources: 2026 Piotr Rozentreter (Rozsoft).
