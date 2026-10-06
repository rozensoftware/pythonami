# Testing

Host checks are driven by `make test`. Phase 0 covers type/runtime bootstrap state, tracked allocation, reallocation, injected failure, and cleanup. Phase 1 adds integer/string/name tokenization, comments, spans, indentation, delimiter nesting, unsupported-keyword classification, negative diagnostics, tokenizer allocation failure, AST arena cleanup, and expression precedence/postfix parsing. Phase 2 adds statement suites, assignments, conditionals, loops, functions, returns, and invalid loop/return context checks. Phase 3 adds deterministic parameter/local slots, duplicate-parameter rejection, nested-function rejection, global/local/builtin/undefined classification, and UNBOUND slot metadata. Phase 4 adds opcode metadata, bytecode growth and big-endian branch patching, constant/name tables, and module/function-body emission. Phase 5 adds linear instruction boundaries, operand/index checks, branch targets, CFG traversal, stack-depth consistency, maximum-stack calculation, and malformed-bytecode rejection. The VM slice adds verified scalar dispatch, explicit value/frame stacks, checked integer operations, and cleanup on runtime errors. Runtime-value tests cover tracked reference-counted strings and lists, aliases, indexed replacement, bounds errors, cycle rejection, release, and shutdown cleanup. Host sanitizer validation uses AddressSanitizer and UndefinedBehaviorSanitizer when supported; the full `make test` suite (all unit tests plus `pythonami -V`/`--help`) has been run under `-fsanitize=address,undefined` with zero leaks and zero UB findings.

The opt-in `make -f Makefile.host debug-stats-test` check validates `--debug`. It confirms that the report is written only to stderr, is absent for normal execution and `--debug -V`, is deterministic for repeated `-c` execution, and remains present on a runtime failure without replacing its exit status. The report labels all source-derived values as `top-level source`; imported-module aggregation is intentionally not part of this increment.

The opt-in `make -f Makefile.host stdio-redirection-test` check validates separate host stdout/stderr redirection for successful `-c` execution, a runtime failure with exit code 11, and `--debug`. It normalizes CRLF/LF before comparing output and confirms that debug statistics and diagnostics stay on stderr while script output stays on stdout.

The opt-in `make -f Makefile.host check-mode-test` check validates `--check`: valid source is compiled and verified without executing script output, while invalid source produces the normal syntax diagnostic and status 10.

`tests/unit/test_generators.c` covers the Level 0.7 generator runtime: `yield` tokenizing as a real keyword, `Py68Code.is_generator` set only for bodies that yield, `yield` outside a function rejected at parse time, the verifier rejecting `OP_YIELD_VALUE` in non-generator code, generator create/store-stack/finish/destroy lifecycle with retained argument slots and idempotent finish, allocation-failure rollback for every allocation in `py68_generator_new`, the emitted shape of a generator expression (hidden iterator parameter, captured free variables, no loop-variable leak), the D-0046 snapshot semantics, and the D-0047 collect path for `list`/`sum`/`sorted`/`any`/`all` including an exception raised mid-drain. Every case asserts `value_stack_count == 0`, `frame_count == 0`, and `allocator.stats.current_bytes == 0` after teardown.

Process coverage now includes repeated `os.system` launches with native exit-status checks, repeated `os.popen` captures, captured output content, invalid argument handling, embedded-NUL rejection, and allocator cleanup in `tests/unit/test_process.c`. The cross-target process fixtures also verify repeated command execution and pipe output capture under `tests/language/process/`.

`tests/unit/test_compiler.c` covers `break`/`continue` bytecode generation and jump patching: `break` inside a `while` body, `continue` inside a `for` body (verified via executed VM state), and `while`-`else` compilation for both the normal-completion path (else runs) and the `break`-exits-early path (else is skipped), each checked with `allocator.stats.current_bytes == 0` after teardown. It also covers source-level compilation and VM execution of `def` functions: parameter binding, recursion (`fibonacci`), local-variable shadowing of globals, `UNBOUND` local read errors, rejection of nested `def` statements, and rejection of duplicate parameter names.

## Language-level advanced fixtures

`make test` now also runs `make language-test`, which executes Python68K scripts under `tests/language/` and `examples/` through `build/host/pythonami` and diffs stdout against fixtures in `tests/fixtures/` via `tests/integration/host/run_language_check.sh`. Coverage added:

- `tests/language/operators/` — Python floor-div/mod for negatives, precedence, integer comparisons, short-circuit `and`/`or` with empty list/string falsiness, unary `not` (including empty containers), membership `in` / `not in` (str/list/tuple/dict/set) plus `for`/comprehension over strings, identity `is` / `is not` (None/bool/int immediates and distinct equal lists), and conditional expressions `then if cond else else` (true/false branches, right-associative nesting, `and`/`or`/comparisons, calls/lists/comprehension elements, one-branch evaluation).
- `tests/language/assignment/` — fixed-count unpacking from list/tuple/string, unparenthesized RHS tuples, `for a, b in …`, and length/`TypeError` unpack failures.
- `tests/language/control_flow/` — `elif` ladders, `while`-`else` (complete and break-skip), `for`-`else`, `continue` in `for`/`while`, nested `for` loops.
- `tests/language/functions/` — recursion (fibonacci/factorial/deep sum), local shadowing, reading globals from functions, `continue` inside functions, augassign on locals, multi-path `return`, implicit `None` return.
- `tests/language/lists/` — concat, `list_append`/`list_pop`, nested index, list-building helpers, empty/`step`/`reverse` `range`, list slices with explicit bounds.
- `tests/language/iterators/` — `iter`/`next`, `StopIteration`, default on exhaustion, `for` over `iter(...)`, double-`iter` identity.
- `tests/language/generators/` — `test_generators.py` covers `yield` in `def`, laziness, `next`/`next(…, default)`/`StopIteration`, bare `yield`, `iter(gen) is gen`, early `return`, `break` abandoning a partially consumed generator, nested generators, `for`-`else` inside a generator, `try`/`except`/`finally` across suspends, exceptions escaping a generator, interleaved independent generators, and `list`/`tuple`/`sorted`/`sum`/`any`/`all` over generators (D-0047); it is byte-identical to CPython output. `test_generator_exp.py` covers generator expressions: filters, nested `for` clauses, an inner iterable reading the outer name, laziness, eager evaluation of the outermost iterable, late-bound globals, genexps over generators and feeding generator functions, a nested list comprehension, and builtin consumption; it matches CPython except the one section marked as the D-0046 free-variable snapshot divergence.
- `tests/language/comprehensions/` — list/set/dict comprehensions, empty iterables, filters, nested `for`, name-binding leakage matching `for`, and unbound-local interaction. `tests/unit/test_parser.c` adds the generator-expression syntax rules: the parenthesized form, the unparenthesized form as a call's only argument, and rejection when another argument precedes or follows it.
- `tests/language/classes/test_classes.py` — host fixture coverage for Level 0.8 class creation, single inheritance, instance attributes, bound methods, `isinstance` / `issubclass`, builtin exception matching, and a user-defined exception subclass. This fixture passed in the `make language-test` host run on 2026-10-06; this run does not verify Amiga execution.
- `tests/language/strings/` — concat, index/slice, falsy empty string, `str`/`int` conversions, ASCII string methods, text builtins (`ord`/`chr`/`repr`/`format`/`maketrans`, `all`/`any`, `sum`, …), and restricted f-strings (`!s`/`!r`/`!a`, `:04d`, `{{`/`}}`).
- `tests/language/test_advanced_suite.py` — single cross-cutting differential fixture combining the above.
- `examples/test_features.py` sections 13–18 extend the incremental demo with the same advanced behaviors, and section 30 demonstrates generators: a `yield` countdown in `for`, `next`/`next(…, default)`, `iter(gen) is gen`, a generator expression drained by `list`, `sum` over a filtered genexp, and a genexp returned from a function.

A host bug in `OP_NOT` (truthiness evaluated after overwriting the value type, and missing release of object operands) was fixed so `not []` / `not ""` match Language Level 0.1 falsiness rules; language fixtures assert the corrected behavior.

Subsequent host fixes covered by `tests/language/test_bugfix_suite.py` and `examples/test_features.py` sections 19–21: for-`break` emits `OP_POP` to discard the range iterator before joining the exit path; subscript assignment parses `INDEX` targets and executes `OP_STORE_INDEX`; `None`/`bool` equality follows D-0011. Index/attr augmented assignment, lexicographic ordering for str/list/tuple, and `sorted(iterable)` are covered in the same bugfix suite (D-0039) and exercised by `examples/wordcount.py`. The suite also checks that a builtin called with the wrong argument count raises a catchable `TypeError` instead of the blank exception it used to report; `tests/unit/test_calls.c` pins the message text for both a wrong count and a callback that fails without recording a diagnostic. Decisions D-0009 through D-0011 and D-0039 record the designs. `tests/unit/test_compiler.c` also executes a for-`break` total accumulation case through verify+VM.

## 0.3–0.5 types, exceptions, and import coverage

Host unit tests `test_tuple`, `test_dict`, `test_set`, `test_attr`, `test_float`, `test_exceptions`, and `test_import` cover the new object types, limited attributes, binary32 float, try/except/finally/raise, and the import loader. Language fixtures live under `tests/language/types/`, `tests/language/exceptions/` (including `with fopen` on file handles), and `tests/language/import/`. Remaining unsupported keywords (`lambda`, `global`, `nonlocal`, `async`/`await`, `match`/`case`, relative import, `import *`) produce targeted diagnostics.

Import coverage includes sibling modules, `from` imports and aliases, cache-once
behavior, script-directory precedence, ordered `sys.path` lookup, `sys.argv`
preservation, missing-module status 11, imported syntax-error status 10,
imported runtime-error status 11, and cycle rejection. Run the focused slice
with `make -f Makefile.host import-test HOST_CC=<clang>`.

## 0.2.0 file and env/assign coverage

Host `tests/unit/test_file_io.c` and `tests/language/test_file_io_suite.py` exercise `fopen`/`fwrite`/`fread`/`freadline`/`fclose`/`exists`/`rename`/`remove` and host `getenv`/`setenv`/`unsetenv`. Amiga release/debug builds compile the same builtins as `assign_get`/`assign_add`/`assign_remove` (D-0012); assign execution on emulator/hardware is owner-verified, not claimed from host results.

## Amiga LoadSeg extensions

`make amiga-ext` builds `ext/demo_add/demo_add.py68k` and
`ext/gui_intuition/gui_intuition.py68k` (vbcc + vasm + vlink). The Amiga
interpreter (`src/ext_amiga.c`) installs `load_library`. Host unit coverage for
extension services (D-0048) is `tests/unit/test_ext_services.c`. Owner-run steps
are in `tests/integration/amiga/README.md`, `test_load_library.py`, and
`test_gui_intuition.py`. Host has no `load_library`. Intuition GUI execution is
owner-verified only (not Musashi).

## Cross-target fixture comparison (host vs. Amiga emulator/hardware)

The host build (Intel/Linux, this environment) can run and be verified directly. The Amiga Hunk build (`vbcc +aos68k`, produced by `Makefile.amiga`) cannot execute in this sandboxed environment — it requires an AmigaDOS runtime, a 680x0 emulator (e.g. WinUAE/FS-UAE), or real hardware, and must be tested by the project owner. Validation evidence is target-specific:

- Host: `make -f Makefile.host test` runs the full unit-test suite, smoke-checks `-V`/`--help`, and runs `language-test` stdout diffs on Linux. `build/host/pythonami <script.py>` executes example/feature scripts directly. `make language-test` runs only the language fixture diffs.
- Amiga: `export VBCC=/home/piotr/local/vbcc && make -f Makefile.amiga clean && make -f Makefile.amiga amiga-debug && make -f Makefile.amiga amiga-release` produces `pythonami-debug` and `pythonami` Hunk executables. These are compile/link-verified on the host but **not execution-verified** here.

`examples/print_values.py` is the platform-neutral fixture: its expected stdout is recorded in `tests/fixtures/print_values.expected.txt`. `examples/test_features.py` is the growing incremental feature-test script — it is extended with every new language feature and is intended to be run on the Amiga side (emulator or hardware) by the project owner and diffed against the host's output for the same script, which is captured here for reference:

```
=== 1. Scalar Arithmetic ===
13
7
30
3
1
=== 2. Comparisons & Logic ===
True
True
False
=== 3. Conditional Statements ===
a is greater than 5
=== 4. While Loops ===
3
2
1
=== 5. Builtin Functions & Lists ===
[10, 20, 30]
3
20
30
[10, 20]
2
=== 6. For Loops & Range ===
10
25
=== 7. Augmented Assignment ===
15
12
24
6
2
=== 8. Function Definitions ===
7
0
1
1
2
3
5
8
13
21
34
15
=== 9. Lists with list_append ===
[1, 2, 3, 4]
4
=== 10. Short-circuit and/or ===
2
5
0
4
=== 11. Strings ===
abcd
e
ell
5
=== 12. Remaining Builtins ===
42
7
False
True
3
2
9
=== 13. Floor/Mod Negatives ===
-3
2
-3
-2
=== 14. Truthiness & not ===
[]
2
x
5
True
False
True
=== 15. Elif & while-else ===
two
0
1
while-done
=== 16. Nested Loops & continue ===
66
=== 17. Recursion & list build ===
[0, 1, 4, 9]
120
=== 18. Range reverse ===
21
=== 19. for-break ===
10
=== 20. Index Assignment ===
9
2
7
5
6
=== 21. Bool/None Equality ===
True
False
True
True
True
=== 22. File Read/Write ===
True
Python68K
Python68K-IO
False
True
False
=== 23. Amiga Assigns ===
True
True
=== 24. Command Arguments ===
["examples/test_features.py", "feature-one", "feature-two"]
examples/test_features.py
=== 25. String Methods & Text Builtins ===
ABC
2
hi
a-b
["a", "b"]
65
A
0042
True
True
True
True
AB
=== 26. Comprehensions ===
[0, 1, 4, 9]
[1, 3]
True
4
[0, 1, 1, 2]
=== Feature Test Complete ===
```

Host exit code for `examples/test_features.py` is `0`. CLI `-c` is also supported (`pythonami -c 'print(1+2)'`). If the Amiga run produces different output or a non-zero exit code, that is a real target-specific bug to report (not a host/logic bug, since the host build already exercises the identical bytecode/VM path). Section 23 uses Amiga-favor `assign_*` names (on host these alias to env vars so the shared fixture still diffs); on Amiga they exercise DOS `AssignPath` / `AssignLock`.
