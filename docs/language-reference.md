# Language Reference

Python68K is a restricted Python-compatible language. Language Levels **0.1** (core), **0.2.0** (file/env I/O), **0.3** (types and limited attributes), **0.4** (exceptions and `with`), **0.5** (import/modules), **0.6** (comprehensions), **0.7** (generators), and **0.8** (classes/instances) are executable on the host and Amiga builds.

Symbol analysis classifies names using local, module-global, builtin, and undefined lookup order; parameters occupy the first local slots and later assignment targets use deterministic source order. Referencing a local before assignment is a runtime `NameError`. Empty strings, lists, tuples, dicts, and sets are falsy. `input([prompt])` writes an optional prompt, reads one line, and returns it without the trailing newline; EOF raises an I/O error.

## Values

- Scalars: signed 32-bit `int` (checked overflow), `bool`, `None`, IEEE-754 binary32 `float` (no NaN/Inf; no 68881; integer-only software in `src/float.c`).
- `str`: 8-bit strings (not Unicode).
- `list`, `tuple` (parenthesized displays `()`, `(a,)`, `(a, b)`; assignment
  also accepts unparenthesized expression lists such as `x = 1, 2`), `dict`, `set`.
- Hashable keys: `None`, `bool`, `int`, `str`, and tuples of hashable items. Lists, dicts, sets, files, functions, and modules are unhashable.
- Cyclic list/dict insertion is rejected (`ValueError`).
- `/` is true division and yields `float`. `//` is integer floor division. Mixed int/float arithmetic promotes to float.

## Operators and comparisons

Arithmetic `+ - * / // %`, unary `+ - not`, comparisons `== != < <= > >=` (bool compares as 0/1; `None == None`; strings/lists/tuples compare lexicographically when types match; unsupported orderings raise `TypeError`). Identity `is` / `is not` tests sameness, not equality: tagged immediates (None, bool, int, float) match by type and payload, so `x is None` follows Python and equal ints with the same bits are identical; heap objects match only when they are the same object (`[1] is [1]` is false). `is not` is a single operator (`x is not y` is not `x is (not y)`). Comparisons do not chain: `a is b is c` is `(a is b) is c`, matching `==`. Short-circuit `and` / `or` are value-preserving. Conditional expressions `then if condition else else` evaluate the condition, then exactly one branch. They bind less tightly than `or`; `a if c1 else b if c2 else c` is `a if c1 else (b if c2 else c)`. Comprehension `if` filters are unrelated (`or_test`, not a ternary). Statement `if`/`elif`/`else` is unchanged.

## Control flow

- `if` / `elif` / `else` (statements). Value-level `then if cond else else` is an expression (D-0040).
- `while` … `else`, `for … in iterable` … `else` (`range`, list, tuple, dict keys, set, string → one-char strings). `for a, b in pairs:` unpacks each item (D-0038).
- Assignment: `NAME = expr`, index/attribute stores, augmented assignment on names/index/attributes (`name += expr`, `L[i] += expr`), and fixed-count unpacking `a, b = seq` (list, tuple, or string). Assignment RHS may be an unparenthesized expression list (`a, b = 1, 2`). Starred and nested unpacking are not supported.
- List, set, and dict comprehensions: `[elt for x in iterable if cond]`, nested `for`, `{elt for ...}`, `{k: v for ...}`. Loop targets bind in the enclosing function or module, matching `for` (D-0026).
- Generator expressions: `(elt for x in iterable if cond)` (0.7). See "Generators".
- Comparisons: `== != < <= > >=`, membership `in` / `not in` (str substring; item in list/tuple; key in dict; member in set), identity `is` / `is not`
- `break` / `continue` / `return` / `pass`
- `try` / `except` / `except TypeError` / `except TypeError as e` / `finally`
- `raise` and `raise TypeError("msg")`
- `with EXPR as NAME` (file handles from `fopen` implement `__enter__` / `__exit__`)

Catchable runtime kinds: `TypeError`, `ValueError`, `IndexError`, `KeyError`, `ZeroDivisionError`, `OverflowError`, `NameError`, `OSError` (alias `IOError`), `RecursionError`, `ImportError`, `StopIteration`, and user exception instances. Builtin exceptions are real type objects under `BaseException` → `Exception`. Matching uses single-inheritance subtype checks (`except Exception` catches `ValueError`). Internal kind tags remain for C raise paths (D-0049). `OSError` and `IOError` share one type object (D-0044).

## Classes (0.8)

Module-level `class Name:` or `class Name(Base):` with a single base (default `object`). Class body may contain `pass`, simple name assignments, and `def` methods only. Instantiation calls the type; `__init__(self, ...)` runs if defined and must return `None`. Instance attributes live in a per-instance `__dict__`. Attribute load checks the instance dict, then the class dict, then the single base chain; functions found on the class become bound methods. `isinstance(obj, T)` and `issubclass(A, B)` walk that chain. `type(obj)` returns the type of user instances, exception objects, and type objects. Multiple inheritance, metaclasses, `super()`, descriptors, operator dunders, nested classes, and classes inside functions are not supported.

## Attributes

Limited attribute access on builtin containers remains a static per-type method table (Level 0.3). User instances and types use dict/class lookup as above. Modules still resolve exports in their global table. `list.append` / `list.pop` exist alongside `list_append` / `list_pop`. Dict: `get`, `keys`, `values`, `items`, `pop`. Set: `add`, `remove`, `discard`. Strings: ASCII/8-bit methods including case (`upper`/`lower`/`capitalize`/`swapcase`/`title`/`casefold`), search (`find`/`rfind`/`index`/`rindex`/`count`/`startswith`/`endswith`), trim (`strip`/`lstrip`/`rstrip`/`removeprefix`/`removesuffix`), split/join (`split`/`rsplit`/`splitlines`/`partition`/`rpartition`/`join`), `replace`, align (`center`/`ljust`/`rjust`/`zfill`), `expandtabs`, `translate`, and classifiers (`isalnum`…`isupper`). Methods are positional-only (no kwargs).

## Imports (0.5)

- `import name`, `import name as alias`
- `from name import a, b`, `from name import a as b`
- Search: directory of the importing source, then entries in `sys.path` (starts with `.`)
- A successfully loaded module is cached and its top-level code runs once per runtime.
- Module bytecode (including function bodies) is retained so imported defs stay callable.
- Functions defined in a module resolve globals against that module (including after nested imports).
- The main script and `-c` command use `__name__ == "__main__"`; an imported module uses its canonical import name, even when accessed through an alias.
- A module that is currently loading is rejected with `ImportError: import cycle detected`.
- Failed imports are removed from the cache; their partial globals are not published.
- No relative imports, no `from x import *`, no multi-level packages

`sys` is a builtin module: `sys.path` (list), `sys.modules`, `sys.argv`.

## Generators (0.7)

A `def` whose body contains `yield` is a generator factory. Calling it builds a
generator and runs no body code; the body advances only while the generator is
resumed, and each `yield expr` suspends it, saving the locals, operand stack,
try blocks, and resume point in the generator object (D-0045).

```python
def counter(limit):
    index = 0
    while index < limit:
        yield index
        index = index + 1

for value in counter(3):
    print(value)
```

- `for x in gen`, `iter(gen)` (which returns the same generator), and
  `next(gen[, default])` resume the generator. Exhaustion raises catchable
  `StopIteration` from `next(gen)`, returns the default from
  `next(gen, default)`, and ends a `for` loop.
- Reaching the end of the body or executing `return` finishes the generator. A
  returned value is discarded; `StopIteration.value` does not exist.
- A finished generator iterates as empty and is never restarted. Resuming one
  that is already running raises `ValueError: generator is already executing`.
- `yield` is a statement, so `x = yield v` is a syntax error, as is `yield`
  outside a `def`. `send`, `throw`, `close()`, and `yield from` do not exist.
- When an unfinished generator loses its last reference, its locals and saved
  operands are freed immediately, but its `finally` blocks do not run (D-0045).
- `list`, `tuple`, `set`, `sorted`, `sum`, `all`, and `any` consume a generator
  argument (D-0047). `min`, `max`, `len`, and `in` / `not in` do not and raise
  `TypeError`; generators also have no `str`/`print` form.

Generator expressions `(elt for x in iterable if cond)` accept the same `for` /
`if` clause chain as comprehensions and evaluate to a generator. The
parentheses are required unless the expression is a call's only argument, so
`sum(x for x in range(5))` is accepted while `f(x for x in it, 1)` is not.
Unlike comprehension targets, generator-expression targets do not bind in the
enclosing scope. The outermost iterable is evaluated when the generator is
created, and free variables of an enclosing function are snapshotted by value at
that moment, so rebinding such a local afterwards is not observed; module
globals stay late-bound (D-0046). CPython re-reads the enclosing variable when
each element is produced, so that case differs deliberately.

## Builtins

`print`, `input`, `len`, `range`, `list`, `tuple`, `dict`, `set`, `list_pop`, `list_append`, `int`, `float`, `str`, `bool`, `abs`, `min`, `max`, `sum`, `iter`, `next`, `sorted`, `ord`, `chr`, `repr`, `ascii`, `all`, `any`, `format`, `maketrans`, `exit`, plus 0.2.0 file builtins `fopen`/`fclose`/`fread`/`freadline`/`fwrite`/`exists`/`remove`/`rename` (modes `r`/`w`/`a`/`rb`/`wb`/`ab`). Host: `getenv`/`setenv`/`unsetenv`. Amiga: `assign_get`/`assign_add`/`assign_remove`, and `load_library(path)` for LoadSeg `*.py68k` plugins (see `docs/amiga-extensions.md`).

`ord`/`chr` operate on one byte (`0..255`). `format` supports a minimal int subset (`''`, `d`, width, zero-pad such as `04d`). `maketrans` builds a translation `dict` for `str.translate`. `ascii` escapes bytes `>= 128` as `\xHH`. `sum(iterable[, start])` adds list/tuple numbers with optional numeric `start` (default `0`); float promotes like `+` (D-0041). `iter(x)` / `next(it[, default])` use the same `Py68Range` cursor as `for`, and accept generators (D-0042, D-0045); exhaustion raises catchable `StopIteration`. Restricted f-strings `f"...{expr}..."` support `!s`/`!r`/`!a` and literal `:spec` matching `format()` (D-0043).

Time builtins are `time()` (epoch seconds as binary32 `float`), `sleep(seconds)`,
`ctime([seconds])`, `localtime([seconds])`, `strftime(format, [localtime])`,
and `perf_counter()` (a monotonic elapsed-time `float`). `localtime` returns a
`struct_time` with `tm_year`, `tm_mon`, `tm_mday`, `tm_hour`, `tm_min`,
`tm_sec`, `tm_wday`, `tm_yday`, and `tm_isdst` attributes. Calendar fields
follow the host or Amiga platform timezone rules; clock precision follows the
available platform clock.
`time_tick()` returns a signed 32-bit millisecond tick suitable for seeding
short-lived pseudo-random examples without converting a large epoch float.

Break and scheduling builtins are `check_break()`, `yield_cpu()`,
`set_poll_interval(count)`, and `get_poll_interval()`. The VM already tests for
a user break (Ctrl-C on Amiga, `SIGINT` on the host) on taken backward branches
and raises an uncatchable `KeyboardInterrupt`; `set_poll_interval(0)` disables
that test and hands full responsibility to the script. `check_break()` consumes
a pending break and returns `True` once, so a script that calls it must stop by
itself. `yield_cpu()` is only a politeness hint: AmigaOS is preemptively
multitasking, so no program has to yield for other programs to run.

## Still not implemented

Unicode, bytes/bytearray/`encode`, generator `send`/`throw`/`close()`, `yield from`, `yield` as an expression, async generators, closures, nested `def` (outside class methods), multiple inheritance, metaclasses, `super()`, descriptors/`property`, operator overloading, nested classes, async, `match`, starred unpacking (`a, *rest`, `*args`/`**kwargs`), nested unpacking (`(a, b), c = …`), relative imports, `from x import *`, AmigaDOS `ENV:` GetVar/SetVar, file seek, encodings, method-style `open()` / `file.read()` with keyword `encoding=` and `FileNotFoundError` (post-0.6 option 1; today use `fopen`/`fread`), full `str.format`/`format_map`, nested/raw/bytes f-string prefixes, `eval`/`exec`/`compile`, `enumerate`/`reversed` and `iter(callable, sentinel)`, and Language Level freeze after owner emulator/hardware verification.
