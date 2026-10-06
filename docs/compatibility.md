# Compatibility

Python68K is a restricted Python-compatible language, not CPython. Language Levels 0.1–0.8 implement the documented subset in `docs/language-reference.md`. Level 0.8 adds user classes with single inheritance and catchable user exception subclasses; multiple inheritance, metaclasses, and descriptors remain unsupported. No CPython `.pyc` compatibility is claimed. Differential behavior against desktop Python is intended only for the documented subset (floor division, bool/int equality, true divide to float, exception kind names, sibling-module imports, comprehension target leakage matching `for` rather than Python 3 nested scopes, and fixed-count unpacking of list/tuple/string). Identity `is` matches Python for None and bool singletons and for distinct heap objects; all ints and floats with the same tagged payload are identical (D-0037), which is broader than CPython interned small ints. Unpacking does not support stars, nested targets, or dict/set/range sources (D-0038). Conditional expressions match Python precedence and right-associative else-chains; statement `if`/`while` conditions also accept an unparenthesized ternary (CPython requires parentheses there) (D-0040).

Generators follow CPython for lazy production, `for`/`iter`/`next`, `StopIteration`, one-shot exhaustion, and `ValueError` when a running generator is resumed again, but they are deliberately narrower (D-0045): `yield` is a statement rather than an expression, a `return` value inside a generator is discarded instead of becoming `StopIteration.value`, there is no `send`/`throw`/`close()`/`yield from`, dropping the last reference to an unfinished generator frees it without running its `finally` blocks, and generators have no `str`/`print` form. Generator expressions snapshot the free variables of an enclosing function when the generator is created, so later rebinding is not observed, and their loop targets do not bind in the enclosing scope even though comprehension targets do (D-0046). Only `list`, `tuple`, `set`, `sorted`, `sum`, `all`, and `any` consume a generator argument; `min`, `max`, `len`, and `in` raise `TypeError` for one (D-0047).

Import compatibility is limited to single-level `.py` modules. The importing
script directory has precedence over `sys.path`, and `sys.path` is searched in
list order. Host paths use `/`; the platform path abstraction also accepts
Amiga-style `:` and `\\` separators when supplied by a script. Packages,
dotted names, relative imports, and native ABI **imports** are not supported.

On Amiga only, `load_library(path)` loads a relocatable `*.py68k` LoadSeg plugin
and returns a module of native exports (D-0027). This is not an `import` path
and is unavailable on the host build. See `docs/amiga-extensions.md`.

Command execution is currently limited to synchronous `os.system(command)` and
`os.popen(command)`. Both require a non-empty string without an embedded NUL.
`os.system` executes with inherited standard handles and returns the platform
command status directly. `os.popen` returns captured stdout as a `str`, not a
file object, and discards the return code. The host uses a temporary file and
combines stderr; Amiga uses an asynchronously produced `PIPE:` stream and
captures stdout only (D-0034). Amiga error output is shell/OS-version dependent
because the separate `SYS_Error` process tag is V50-only. `subprocess`,
argument-list commands, per-child directories/environments, timeouts, and
`Popen` are not implemented.
