# Decisions

## D-0050: Exception types with kind-tag bridge

- Context: Level 0.4 matched `except` by comparing `Py68ErrorKind` to native
	stub flags (D-0017). Level 0.8 needs `except Exception` and user
	`class E(Exception)` to follow single-inheritance subtype rules without
	rewriting every `py68_vm_error` call site.
- Decision: Builtin exceptions are `Py68Type` objects under `BaseException` /
	`Exception`, each carrying `exception_kind`. Exception instances keep a
	kind tag and a retained `type` pointer. Matching uses
	`py68_type_is_subtype`. C raise paths still set a kind; `exception_from_error`
	attaches the mapped builtin type. User exception subclasses use catchable
	`PY68_ERROR_EXCEPTION` plus their type pointer. `OP_RAISE` reuses
	`current_exception` in `py68_vm_catch` so the type pointer is not rebuilt
	away. `IOError` remains an alias of the `OSError` type object (D-0044).
- Alternatives considered: Drop kind tags entirely; keep kind-only matching.
- Consequences: `except Exception` catches `ValueError` and user subclasses.
	Uncatchable errors (syntax/bytecode/memory/internal/interrupt) stay outside
	the user hierarchy.

## D-0049: Single-inheritance classes with instance `__dict__`

- Context: Level 0.7 had no user classes; attributes were a static per-type
	native method table (D-0014). Amiga RAM and M68000 complexity rule out
	multiple inheritance and full descriptors for the first OOP milestone.
- Decision: Language Level 0.8 adds `Py68Type` / `Py68Instance`, module-level
	`class Name[(Base)]:`, restricted bodies (`pass` / simple assign / `def`),
	`__build_class__(ns, name[, base])`, per-instance `__dict__`, `__init__`,
	bound user methods, and `isinstance`/`issubclass` via a single `base_type`
	chain. Builtin container method tables remain for list/str/….
- Alternatives considered: Slots-only instances; multiple inheritance/C3;
	compile-time-only classes without runtime type objects.
- Consequences: Higher per-instance RAM from `__dict__`. No `super()`,
	metaclasses, MI, or operator dunders in 0.8.

## D-0047: Builtins consume a generator through a VM-driven collect frame

- Context: `sum(x * x for x in range(10))` and `list(gen())` are the ordinary
	way to use a generator, but `py68_builtin_sum` and friends walk a
	`Py68List`/`Py68Tuple` directly. A native cannot step a generator, because
	resuming one means running bytecode, and the explicit VM value and frame
	stacks exist precisely so that no script call recurses in C (D-0045).
- Decision: `Py68NativeFunction` gains `consumes_iterable`. It is set for
	`list`, `tuple`, `set`, `sorted`, `sum`, `all`, and `any` — the builtins that
	read their first argument as an iterable. When `OP_CALL` sees such a native
	with a generator in that position it pushes an empty list, then resumes the
	generator with `PY68_RESUME_COLLECT` and `return_ip` pointing back at the
	`OP_CALL` itself. Each `OP_YIELD_VALUE` in that mode appends to the list at
	`stack_base - 1` and keeps the activation running, so no generator state is
	saved and no frame is popped. When the generator finishes, the list replaces
	the generator argument in place and the same `OP_CALL` re-executes, this time
	reaching the callback with a list.
- Alternatives considered: A `next()`-style inline special case per builtin
	(duplicates the drain loop several times); letting natives call back into
	`py68_vm_run` (C recursion per nesting level); converting every generator
	argument to a list regardless of the callee (`print(gen)` and `type(gen)`
	would stop reporting the generator); requiring scripts to write an explicit
	`for` loop.
- Consequences: The drain is eager, so `sum(infinite_generator())` never
	returns, exactly as in CPython. Memory use is that of the materialized list,
	which CPython avoids for `sum`/`any`/`all`; on a 68000 a long generator can
	therefore fail with `MemoryError` where CPython would stream. `any`/`all` do
	not short-circuit over a generator. Builtins outside the list still see the
	generator object itself, so `print(gen)` and `len(gen)` stay `TypeError`.

## D-0046: Generator expressions snapshot free names as synthetic arguments

- Context: `(elt for x in it)` needs its own suspended frame, but Language
	Level 0.7 has no cells, no closures, and no user nested `def`. A genexp
	written inside a `def` can still read that function's locals.
- Decision: The compiler synthesizes a nested `Py68Code` with
	`is_generator = 1` whose body is the comprehension loop ending in
	`OP_YIELD_VALUE`. Its **first parameter is a hidden iterator**: the creation
	site evaluates the outermost iterable eagerly, as CPython does, and the body
	starts with `OP_LOAD_LOCAL 0` + `OP_RANGE_INIT 1`. The parameter is recorded
	with a zero-length name, which can never match a source identifier, so script
	code cannot reach it. Names used inside the genexp that resolve to a local or
	parameter of the enclosing function become **additional leading parameters**.
	At the creation site the compiler emits `OP_MAKE_FUNCTION`, the outer
	iterable, then loads those names in the enclosing scope and emits `OP_CALL`,
	so the values are **snapshotted at construction time**. Names that are not
	enclosing locals stay `OP_LOAD_GLOBAL` (module global, then builtin) and are
	therefore resolved lazily on each iteration step, and inner `for` clauses
	evaluate their iterables lazily as in CPython. Genexp loop targets bind only
	inside the generator and never in the enclosing scope (unlike list/set/dict
	comprehensions, D-0026); loop targets of a comprehension nested inside the
	genexp become locals of the synthetic scope for the same reason.
	`f(x for x in xs)` is accepted when the genexp is the only argument, reusing
	the call parentheses as CPython does; alongside other arguments it must be
	parenthesized.
- Alternatives considered: Real cell objects with late binding (needs a closure
	model and cycle handling); inline the genexp into a list (loses laziness);
	reject genexps inside functions; snapshot every enclosing local
	unconditionally (an unbound local could not be pushed without a new opcode).
- Consequences: Rebinding an enclosing local after the genexp is created does
	not change what the generator sees; CPython's late-binding cells would. This
	is the one documented behavioral divergence, covered by
	`tests/language/generators/test_generator_exp.py`. Rebinding a *global* does
	change it, which matches CPython. A genexp captures at most
	`PY68_GENEXP_MAX_FREE` free names and binds at most `PY68_GENEXP_MAX_BOUND`,
	both reported as targeted syntax errors, because the sets live on the C stack
	during compilation and `OP_CALL` encodes its argument count in one byte. The
	divergence is documented in `docs/language-reference.md`.

## D-0045: Generators (`yield`) with private frame state; no `send`/`throw`

- Context: Level 0.7 adds suspended iteration. The VM has a single shared
	`value_stack`, keeps the live instruction pointer only inside
	`py68_vm_run`, and iterates exclusively through `OP_RANGE_INIT` /
	`OP_RANGE_NEXT` over `Py68Range` (D-0042). Script calls must never use C
	recursion.
- Decision:
	- A module-level (or imported-module) `def` whose body contains `yield` is a
		**generator factory**: `OP_CALL` allocates a `PY68_OBJECT_GENERATOR`,
		copies the arguments into its locals, and returns it without running the
		body.
	- A generator owns its locals, its saved operand stack, and its saved try
		stack. While it runs, an **activation frame** on `runtime->frames`
		points at the generator (`frame->generator`), borrows `generator->locals`
		as the frame locals, and records `stack_base`, the caller resume point,
		and how to finish (`for` step or `next()` call). Operands live on the
		shared `runtime->value_stack` above `stack_base` while running and are
		copied into the generator on `OP_YIELD_VALUE` (try-block depths are
		rebased relative to `stack_base`). Suspended generators therefore hold no
		frame slot and consume no recursion budget, and no script call uses C
		recursion. This replaces the plan's separate `runtime->active_generator`
		field: the activation frame already identifies the running generator.
	- `OP_YIELD_VALUE` (0x3C, width 1, stack effect −1) pops the yielded value,
		saves generator state, pops the activation without releasing locals, and
		pushes the value on the caller's stack. `yield` always resumes with
		`None` internally; there is no `send`, `throw`, or `close()` method and
		no `yield from`. `yield` is a statement only.
	- `return` inside a generator (with or without a value) finishes it; the
		value is discarded because `StopIteration.value` does not exist here.
	- Consumption reuses the iteration opcodes (D-0042): `OP_RANGE_INIT` with
		one argument passes a generator through unchanged, `OP_RANGE_NEXT`
		resumes a generator TOS, `iter(gen)` returns the same generator, and
		`next(gen[, default])` resumes it (the VM handles this `next` inline so a
		native callback never re-enters the interpreter; builtins that consume a
		whole iterable are handled by D-0047). An exhausted generator
		iterates as empty, matching CPython, rather than raising; the plan's
		"reject DONE" reading would have diverged from CPython for
		`for x in exhausted_gen`.
	- An exception that escapes a generator marks it `DONE` and releases its
		locals while it propagates to the caller.
	- **Close on release**: dropping the last reference to an unfinished
		generator releases its locals, saved operands, and retained code and
		module references deterministically, but does **not** execute the
		script's `finally` blocks. Executing bytecode from inside
		`py68_object_release` would re-enter the interpreter from arbitrary
		release points and recurse in C, which the 68000 stack budget forbids.
- Alternatives considered: Routing every stack helper through
	`runtime->active_generator` (touches every opcode handler); keeping
	suspended generators on `runtime->frames` (burns recursion slots and
	reorders unwinding); a nested `py68_vm_run` per resume (C recursion per
	generator); a deferred close queue drained at instruction boundaries that
	force-jumps through `finally` handlers (needs a `GeneratorExit`-like
	uncatchable unwind and duplicates exception machinery).
- Consequences: Resources held by a suspended generator are freed by reference
	counting, so files close, but user-visible `finally` cleanup only runs when
	the generator is driven to completion or raises. `print(gen)` is a
	`TypeError`, as it already is for `Py68Range` iterators. Generators are
	unhashable and are not cycle-checked; storing a generator inside a container
	that the generator itself can reach is undefined and unsupported.

## D-0044: `OSError` primary name; `IOError` alias

- Context: Python 3 renamed the catchable I/O failure type to `OSError`;
	`IOError` remains only as an alias. Python68K still reported and
	registered `IOError` alone, which was a Python 2-era surface.
- Decision: `py68_error_kind_name(PY68_ERROR_IO)` returns `"OSError"`.
	Both `OSError` and `IOError` builtins map to `PY68_ERROR_IO`, so
	`except OSError` and `except IOError` both match. Name lookup accepts
	either spelling. Kind numbers are unchanged.
- Alternatives considered: Drop `IOError` entirely; keep `IOError` as the
	printed name with an `OSError` alias only.
- Consequences: Printed/raised type strings say `OSError`. Existing scripts
	that catch `IOError` keep working. Aligns process/file docs that already
	described `OSError`.

## D-0043: Restricted f-strings via JOINED_STR / FORMATTED_VALUE

- Context: Scripts want interpolated strings. Full `str.format` / format-map /
	nested f-strings / debug `=` conflict with Level constraints (no kwargs,
	limited `format()`).
- Decision: Accept `f`/`F`-prefixed one-line quotes as `PY68_TOKEN_FSTRING`.
	Parse to `JOINED_STR` of literal `STRING` parts and `FORMATTED_VALUE`
	`{expr[!s|!r|!a][:spec]}` with literal format specs matching builtin
	`format()`. Compile without new opcodes: `str`/`repr`/`ascii`/`format`
	calls plus `OP_ADD`. `{{`/`}}` are literal braces. Reject `fr`/`rf`/`b`/`u`
	prefixes, nested f-strings, empty `{}`, and expressions in format specs.
- Alternatives considered: `BUILD_STRING` opcode; full CPython f-string
	tokenizer; defer until `str.format` methods exist.
- Consequences: Updates D-0025 / D-0039 to allow this subset. Bare `f` remains
	a normal name.

## D-0042: `iter` / `next` over `Py68Range`; catchable `StopIteration`

- Context: Scripts need CPython-style `iter`/`next` without classes or user
	`__iter__`. `for` already converts containers to `PY68_OBJECT_RANGE` via
	`RANGE_INIT`/`RANGE_NEXT`.
- Decision: Expose that cursor as the iterator. `iter(x)` (arity 1) converts
	list/tuple/str/dict/set (and retains an existing RANGE) through shared
	`py68_iterable_get_iter`. `next(it[, default])` advances a RANGE; exhaustion
	raises catchable `StopIteration` (`PY68_ERROR_STOP_ITERATION`, appended after
	`INTERRUPT`) or returns `default`. `for` / comprehensions stay on
	`RANGE_*` (no `GET_ITER`/`FOR_ITER`). Reject `iter(callable, sentinel)`.
- Alternatives considered: New iterator heap type; rewire `for` to new opcodes;
	exhaustion as `ValueError` only.
- Consequences: `for x in iter(xs):` works because `RANGE_INIT` accepts RANGE.
	`enumerate` / `reversed` remain deferred.

## D-0041: `sum(iterable[, start])` numeric builtin

- Context: Scripts and examples use CPython's `sum` over lists/tuples. Python68K
	already exposed related aggregators (`min`/`max`/`all`/`any`/`sorted`) but
	omitted `sum`, which produced `NameError` at call sites.
- Decision: Register import-free `sum(iterable[, start])` with arity 1–2.
	`iterable` must be a list or tuple of numbers (`int`/`bool`/`float`);
	`start` defaults to `0` and must be numeric. Empty input returns `start`.
	Integer accumulation uses checked 32-bit add (OverflowError); any float
	promotes the running total to binary32 float with non-finite rejection,
	matching `OP_ADD`. Strings and other containers are `TypeError`.
- Alternatives considered: Defer until a general iterator protocol; accept
	only ints (too narrow vs existing float arithmetic); support string
	`start` for concatenation (CPython forbids this for `sum`).
- Consequences: `sum(range(n))` works because `range` materializes a list.
	No `key=` or keyword arguments.

## D-0040: Conditional expressions via JUMP_IF_FALSE / JUMP

- Context: Everyday Python `then if condition else else` was missing. `if` /
	`else` existed only as statements; comprehension `if` filters are a
	separate `or_test` production. Lambda is unsupported, so the Pratt `or`
	ladder is the top of the expression grammar.
- Decision: Parse `or_test ["if" or_test "else" expression]` as
	`PY68_AST_IF_EXP`. The else-clause is a full expression, so nested
	ternaries associate to the right. Compile by evaluating the condition,
	`OP_JUMP_IF_FALSE` to the else branch (pops the condition), emitting the
	then-expr, `OP_JUMP` past else, then the else-expr. No new opcode.
	Comprehension iterables and `if` filters keep parsing `or_test` so
	`[x for x in items if x]` is not consumed as a ternary. Statement
	`if`/`elif`/`while` conditions still use the full expression parser, so
	an unparenthesized ternary is accepted there (slightly more permissive
	than CPython, which uses `namedexpr_test`).
- Alternatives considered: A dedicated `OP_IF_EXP`; left-associative else
	chains; allowing ternary `if` to steal comprehension filters (would break
	Level 0.6 comprehensions).
- Consequences: `a or b if c else d` is `(a or b) if c else d`. Missing `else`
	is a syntax error. Both branches are compiled; only one runs.

## D-0039: Index/attr augmented assignment, ordering, and sorted

- Context: `examples/wordcount.py` needs `d[k] += 1`, lexicographic ordering of
	strings/tuples for `sorted(dict.items())`, and a `sorted` builtin. Plain
	name `+=` and `OP_STORE_INDEX` already existed (D-0010); ordering
	comparisons for non-numerics raised `TypeError`; D-0025 deferred `sorted`.
- Decision: After a leading expression, accept augmented-assignment operators
	when the target is `NAME`, `INDEX`, or `ATTRIBUTE`. Compile index targets by
	re-evaluating container and index around `OP_LOAD_INDEX` / binary op /
	`OP_STORE_INDEX` (side effects run twice). Attribute targets use `OP_DUP` +
	`LOAD_ATTR` / `STORE_ATTR`. Add `py68_value_compare` for int/bool/float and
	same-type str/list/tuple lexicographic order; wire `< <= > >=` through it.
	Expose `sorted(iterable)` returning a new list (stable insertion sort; no
	`key`/`reverse`). Accept list, tuple, dict keys, set values, and string
	characters. Restricted f-strings are provided separately (D-0043); wordcount
	still uses concatenation where convenient.
- Alternatives considered: Rewrite wordcount to avoid `+=`/`sorted`; implement
	full f-strings; add `DUP_TWO` instead of re-evaluating index targets.
- Consequences: Updates D-0025 to allow `sorted` without general iterator
	protocol builtins. Nested-container `print` remains limited to scalar list
	items.

## D-0037: Identity comparison for tagged immediates vs heap objects

- Context: Python `is` tests object identity. Python68K stores None, bool, int,
	and float as tagged immediate `Py68Value`s with no heap object, while
	str/list/tuple/dict/set/function/module/file are reference-counted heap
	objects. Membership recently reserved opcodes 0x1E/0x1F.
- Decision: Parse `is` and compound `is`+`not` as comparison-precedence binary
	operators (`PY68_AST_BINARY`, with parser-only `PY68_TOKEN_IS_NOT`). Emit
	`OP_IS` (0x16) and `OP_IS_NOT` (0x17). Identity is the same `Py68ValueType`
	plus: None values are identical to each other; bool/int/float match when
	the 32-bit payload matches; heap objects match when the pointers are equal.
	`is not` is one operator, so `x is not y` is not `x is (not y)`. Prefix
	`not` still binds less tightly than comparisons (`not x is None` ≡
	`not (x is None)`). Chaining is left-associative like `==` (`a is b is c`
	≡ `(a is b) is c`).
- Alternatives considered: Heap-box all scalars so only pointer identity exists
	(expensive on 68000); intern only None (would make `1 is 1` false unlike
	the tagged model); reuse `OP_EQUAL` with a flag.
- Consequences: `x is None` / `x is not None` match Python. `True is 1` is false
	while `True == 1` remains true (D-0011). Equal ints/bools/floats with the
	same payload are identical even when CPython would not intern large ints
	or floats. Two equal lists, tuples, or strings are not identical unless
	they are the same object; each `LOAD_CONST` string allocates a new heap
	string.

## D-0038: Fixed-count unpacking and assignment expression lists

- Context: Language Level 0.1 assignment targets were a single name or index
	store; `for` targets were a single `NAME`. Everyday Python `a, b = (1, 2)`
	was listed as unsupported. D-0015 kept tuples parenthesized because a bare
	`a, b` collides with call and assignment parsing in expression position.
- Decision: Add `OP_UNPACK` (0x0F, `u16` count). The instruction pops one
	list, tuple, or string and pushes `count` items right-to-left so the first
	item is TOS; stores then run left-to-right. Wrong length is `ValueError`
	(`not enough values to unpack` / `too many values to unpack`); a
	non-sequence is `TypeError`. Assignment RHS is an expression list: a comma
	builds a tuple, so `a, b = 1, 2` and `x = 1, 2` work. Unparenthesized
	name lists are assignment and `for` targets only (`a, b = …`,
	`for a, b in …`, and parenthesized `(a, b)` as those targets).
	Comprehension `for` targets remain a single `NAME`. Nested unpack
	(`(a, b), c`), starred unpack, and unpack into subscript/attribute
	targets stay unsupported.
- Alternatives considered: Desugar to indexed loads without a new opcode
	(worse errors, extra bounds checks); extend D-0015 to unparenthesized
	tuples in every expression position (`return a, b`), which still collides
	with argument lists.
- Consequences: D-0015 still applies outside assignment: `return a, b` remains
	a syntax error. Opcode 0x0F is assigned; changing it requires a
	bytecode-format version bump.

## D-0036: Membership operators and string iteration

- Context: `PY68_TOKEN_IN` existed for `for`/`comprehension` only. Scripts such as
	`examples/wordcount.py` need comparison `in` / `not in` and `for char in text`.
- Decision: Parse `in` and compound `not`+`in` as comparison-precedence binary
	operators (`PY68_AST_BINARY`, with parser-only `PY68_TOKEN_NOT_IN`). Emit
	`OP_CONTAINS` (0x1E) and `OP_NOT_CONTAINS` (0x1F). Semantics: str/str
	substring (empty needle is true); list/tuple equality scan; dict key
	presence; set membership; other containers raise `TypeError`. Extend
	`OP_RANGE_INIT` iterable conversion so strings become a range over
	one-character strings. Adjust prefix `not` so it binds less tightly than
	comparisons (`not a in b` ≡ `not (a in b)`).
- Alternatives considered: Desugar `not in` to `CONTAINS`+`NOT`; reject string
	iteration until a dedicated iterator type exists.
- Consequences: Membership and string `for`/comprehensions match the documented
	Python 3 subset. Opcode numbers 0x1E/0x1F are now assigned; changing them
	requires a bytecode-format version bump.

## D-0034: Platform-specific output capture for `os.popen`

- Context: D-0032 deferred output capture pending a native request/result
	contract. Users need to read a command's output into a variable (e.g. `dir`
	or `list`) instead of only seeing it on the console.
- Decision: Add `py68_platform_system_capture` and expose it as
	`os.popen(command)`, returning captured stdout directly as a `str` rather
	than a file object. Validation matches `os.system`: reject non-string,
	empty, or NUL-containing commands with `TypeError`/`ValueError`. The host
	backend redirects combined stdout/stderr to a per-process temporary file.
	The Amiga backend creates a unique `PIPE:` name from the current task and a
	sequence number, launches `command >PIPE:name` asynchronously through the
	user shell, then opens the reader endpoint and buffers until EOF. Because
	async `SystemTagList` closes its streams, the child receives a disposable
	`NIL:` base output instead of PythonAmi's `Output()` handle; shell
	redirection replaces it for command stdout. `SYS_Error` is not used because
	it is V50-only and PythonAmi supports V36+. Error output behavior therefore
	depends on the active shell and DOS version. The Amiga async launch cannot
	provide a portable child exit status, and `os.popen` discards the platform
	return code.
- Alternatives considered: A CPython-compatible `os.popen` returning a
	file-like object; synchronous `SystemTagList` redirection to `T:`. The
	temporary-file implementation failed on the target when reopening/reading
	the redirected process output. Returning a live file object would require
	process lifetime and close semantics that the platform API does not expose.
- Consequences: Scripts can capture command output into a variable on both
	host and Amiga when a `PIPE:` handler is mounted. Amiga capture is bounded
	to 1 MiB including the terminator and waits for producer EOF; a child that
	never closes stdout can block the caller. `subprocess`, argument-list
	commands, per-child `cwd`/`env`, timeouts, and `Popen` remain unimplemented.
## D-0033: Check-only CLI mode

- Context: Amiga deployment needs a way to validate a script without running
	its side effects, while preserving the same compiler and verifier path used
	for normal execution.
- Decision: `--check` accepts a command or script input, compiles and verifies
	it, skips builtin installation and VM execution, and returns the normal
	source or memory status. Valid input produces no script output; diagnostics
	continue to use stderr.
- Alternatives considered: Parse-only validation, a separate checker, or
	executing in a sandbox. These alternatives would either omit bytecode
	verification or duplicate the production pipeline.
- Consequences: Deployment scripts can be preflighted on Amiga without
	executing them. Imported modules are not loaded during this top-level check;
	import validation remains part of normal execution.

## D-0032: Synchronous os.system as the first DOS process API

- Context: The DOS command execution proposal requires a process backend, but
	output capture and asynchronous lifetime management need handles, cleanup,
	and child-I/O semantics that do not yet exist in the platform interface.
- Decision: Implement only `os.system(command)` initially. Validate a single,
	non-empty string and reject embedded NUL bytes, execute synchronously with
	inherited standard handles, and return the native command status directly.
	Launch failure is mapped to an I/O runtime error. The Amiga implementation
	uses `SystemTagList` (not `Execute`, which only returns DOSTRUE/DOSFALSE);
	the host implementation uses its synchronous command primitive.
	`subprocess` and `Popen` remain explicitly unsupported.
- Alternatives considered: Emulate `subprocess` synchronously, silently
	ignore capture/timeout parameters, or expose shell execution as a direct
	process API before the backend can enforce that distinction.
- Consequences: The smallest useful API is available without claiming
	`shell=False`, capture, timeout, environment, or process-handle semantics.
	A later increment must add a native request/result contract and tests for
	temporary-file capture before expanding the public API.

## D-0030: Time API and struct_time representation

- Context: The requested time subset needs calendar fields while the language
	has no general user-defined object type.
- Decision: Install `time`, `sleep`, `ctime`, `localtime`, `strftime`, and
	`perf_counter` as native functions. `localtime` returns a dedicated
	reference-counted `struct_time` object exposing the nine documented `tm_*`
	attributes. Epoch and performance-clock values use the existing software
	binary32 value representation; calendar conversion and formatting use the
	platform C time services.
- Alternatives considered: Return an unnamed tuple, add a general attribute
	dictionary, or expose only formatted strings.
- Consequences: `localtime().tm_year` and `strftime(format, localtime())` are
	supported without expanding the user object model. Host monotonic timing is
	backed by the host clock service and Amiga timing by `DateStamp`; sub-second
	precision follows each platform's available clock resolution.

## D-0031: Bounded millisecond seed tick

- Context: Seeding the random example with `int(time())` repeats whenever two
	processes start in the same epoch second. Scaling the epoch float by 1000
	would exceed the signed 32-bit language integer range.
- Decision: Expose `time_tick()` as a signed 31-bit millisecond value. Host
	implementations use the host elapsed/system clock and Amiga uses the
	millisecond value derived from DOS `DateStamp()`, masked to `0x7fffffff`.
- Alternatives considered: Keep second-resolution seeds, use a large integer
	timestamp, or add a platform-specific random source.
- Consequences: Short-lived examples receive varying seeds without requiring
	64-bit integers or floating-point conversion. The tick wraps periodically,
	so it is suitable for seeding and not a persistent timestamp.

## D-0048: Extension runtime services for owned str/list results

- Context: LoadSeg plugins (D-0027) cannot link pythonami, so they could only
	return none/bool/int via inline helpers in `py68k_ext.h`. Dialog APIs need
	owned `str` (and list helpers) without embedding the allocator.
- Decision: `Py68Runtime` begins with a `const Py68ExtServices *ext_services`
	pointer (plugins read it via `Py68ExtRuntimeHead` / `py68_ext_services()`).
	The interpreter installs a table in `py68_runtime_initialize` that wraps
	`py68_string_new_copy`, string borrow, list new/append/count/get, and
	`py68_value_release`. Header magic and `PY68_EXT_ABI_VERSION` stay at 1;
	services are runtime-side, not part of the hunk header.
- Alternatives considered: Buffer-out APIs only; in-tree Intuition builtins;
	bumping the hunk ABI version with an embedded services pointer in the header.
- Consequences: `ext/gui_intuition/` can return real Python strings. Plugins must
	release owned values obtained via `list_get_copy` using `value_release`.

## D-0027: Amiga LoadSeg extension plugins (not OpenLibrary)

- Context: Authors want vbcc/vasm performance helpers callable from pythonami without rebuilding the interpreter. Classic AmigaOS `.library` (Resident/LibInit/LVOs) is heavy for this use case; host `dlopen` is out of scope.
- Decision: Amiga-only builtin `load_library(path)` uses `LoadSeg` on a relocatable Hunk file (`*.py68k`). First hunk payload after the seglist next-pointer is a `Py68ExtHeader` (`'PY68'`, ABI 1, export table). Each export becomes a `Py68NativeFunction` on a returned module. `UnLoadSeg` runs when the module is destroyed (after clearing globals). Public ABI is `include/py68k_ext.h`. Plugins must not link `startup.o` / `vc.lib` / NDK `amiga.lib`.
- Alternatives considered: Real AmigaOS `.library` via `OpenLibrary`; extending `import` to auto-load natives; host ELF `dlopen`.
- Consequences: Scripts keep the library module alive while calling exports; escaped native refs after unload are undefined. `import` remains `.py`-only. Sample + vasm workflow live under `ext/demo_add/` and `make amiga-ext`.

## D-0029: Amiga `__stack` process stack size

- Context: `examples/test_random.py` (`import random`, which itself does `import
	randgen`) ran to completion with correct printed output on Amiga, then the
	process crashed with a Guru Meditation after the script finished. Nothing in
	`Makefile.amiga` or `src/main.c` ever requested a process stack size, so the
	binary inherited whatever stack the launching Shell/Workbench icon provided
	(often as little as 4 KiB). Recursive-descent tokenizing, parsing, and
	compiling, plus a nested `import` re-entering `py68_vm_execute_module` on
	the C call stack (once per imported module, stacked on top of the outer
	module's own still-active frame), can exceed a small default stack; the
	resulting corruption of adjacent memory only faults later, when the
	corrupted C stack frames unwind during cleanup — after the script's own
	output has already been written.
- Decision: Define `long __stack = 65536L;` in `src/main.c` under
	`#ifdef PY68K_AMIGA`. vbcc's `+aos68k` `lib/startup.o` recognizes this
	SAS/C-style global and allocates a process stack of that size instead of
	inheriting the caller's, independent of any CLI `Stack` override behavior.
	Host builds are unaffected (`PY68K_AMIGA` is only defined for the Amiga
	build).
- Alternatives considered: Convert nested `import` execution to an explicit
	worklist instead of C recursion (larger refactor, deferred); require callers
	to raise the CLI `Stack` before running `pythonami` (undiscoverable,
	not enforceable from Workbench).
- Consequences: `examples/test_random.py` and other multi-level `import`
	chains need real Amiga/emulator re-verification with a freshly rebuilt
	binary. `__stack` size may need future tuning
	if deeper import chains or recursion are added.

## D-0022: Opt-in top-level debug statistics

- Context: The CLI needs deterministic execution statistics without changing normal script output or exit status.
- Decision: `--debug` is accepted before `-c` or a script path and emits a delimited report through the platform stderr abstraction after VM execution and before source/code cleanup. Source file count, source bytes/lines, tokens, and bytecode metrics describe only the top-level source unit in this increment.
- Alternatives considered: Always-on diagnostics, reporting after cleanup, or aggregating imported modules before the import metrics contract is defined.
- Consequences: Report writes are best-effort and cannot replace the original status. `-V` and `--help` remain report-free, including when preceded by `--debug`; imported-module aggregation remains future work.

## D-0023: Amiga stderr without requiring dos.library V47

- Context: NDK 3.2 documents `ErrorOutput()` as V47-only. Calling that LVO on Kickstart 2.x–3.1 crashes after successful script output when `--debug` or diagnostics first touch stderr.
- Decision: `py68_platform_write_stderr` on Amiga uses `ErrorOutput()` only when `DOSBase->dl_lib.lib_Version >= 47`; otherwise it writes to `pr_CES` when non-zero and falls back to `Output()`. Reject a null file handle before `Write()`.
- Alternatives considered: Require AmigaOS 3.2, always write diagnostics to `Output()`, or open a fixed console.
- Consequences: Separated stdout/stderr redirection works on OS 3.2 shells; on older systems stderr merges with stdout unless the process already has `pr_CES` set.

## D-0021: Linux-first host compiler with local Windows fallback

- Context: Linux is the primary development environment, while this workspace
	also needs a local compiler for host tests on Windows.
- Decision: Keep `gcc` as the default host compiler and support Clang through
	the `HOST_CC` make variable. The Windows workspace uses LLVM-MinGW Clang
	22.1.8 installed locally through WinGet. Visual Studio 2026 is installed,
	but `cl.exe` is not the configured compiler and must be evaluated from a
	Developer PowerShell if support is added later.
- Alternatives considered: Make the repository depend on Visual Studio, add
	a committed compiler binary, or change the Linux default to Clang.
- Consequences: Linux builds remain unchanged with `make test`; Windows host
	tests can use `HOST_CC=<path-to-clang.exe>`. The compiler installation is a
	machine prerequisite, not a repository dependency, and Amiga builds remain
	controlled by `Makefile.amiga`.

## D-0001: Host 32-bit language integer typedef

- Context: The brief requires `signed long` for `Py68I32`, while modern 64-bit hosts commonly define `long` as 64 bits.
- Decision: Use `signed int` and `unsigned int` for host builds and `signed long` and `unsigned long` for Amiga builds, with compile-time four-byte assertions in both configurations.
- Alternatives considered: Force the host compiler into an LLP32 data model, or reject common 64-bit host compilers.
- Consequences: The language width is explicit and portable on the supported host and Amiga targets; serialized formats must continue to use `Py68U8` byte encoding rather than C type layout.

## D-0002: Cross-target validation strategy

- Context: Python68K must be checked both on the Motorola 68000 target and on a current Linux Intel host without confusing compilation evidence with execution evidence.
- Decision: Use `vbccm68k` for 68000 compiler/object checks and `vbcci386` or GCC for Intel-host compatibility checks. Report emulator and hardware execution separately from compiler and host results.
- Alternatives considered: Treat the Amiga Hunk build as sufficient, or rely only on GCC and desktop tests.
- Consequences: Host sanitizers remain fast and authoritative for portable-core defects; target, emulator, and hardware results are reported separately, and no compatibility claim is made without actual execution evidence.

## D-0003: Workbench startup ownership

- Context: The sibling assembler project contains `wbstartup.s`, while the Python68K Amiga build uses vbcc's `+aos68k` C runtime.
- Decision: Keep vbcc `startup.o` as the sole Workbench startup/exit owner. Do not link or call the sibling `wbstartup.s` from C code.
- Alternatives considered: Add `WBStartup`/`WBExit` calls around `main`, or replace vbcc startup with a custom assembly entry.
- Consequences: The current build follows the vbcc/NDK Workbench handshake contract; a second message receive/reply must not be introduced. Workbench retesting must use a freshly rebuilt NDK-linked binary.

## D-0004: vbcc-native Amiga runtime linkage

- Context: The NDK `amiga.lib` and vbcc target libraries are separate ABI/runtime families.
- Decision: Link the Python68K Amiga C program through the vbcc `+aos68k` configuration and its target `startup.o`/`vc.lib`; use the NDK only as an optional reference/source of headers, not as a library mixed into this link.
- Alternatives considered: Append NDK `lib/amiga.lib` to the vbcc link command.
- Consequences: The C runtime, Workbench startup handshake, DOS inline calls, and vbcc ABI remain coherent. Amiga runtime execution must be retested with freshly rebuilt artifacts.

## D-0005: Release-build stack safety flags

- Context: The debug build succeeds while the release binary crashes in Amiga startup or early execution, a classic sign of a 68000 optimization or delayed-pop issue rather than a source-logic error.
- Decision: Keep `-use-framepointer` and `-no-delayed-popping` in both debug and release builds to preserve a stable stack frame and avoid release-only Guru Meditation behavior.
- Alternatives considered: Keep the release build at `-O=2` alone, or add a custom assembly startup wrapper.
- Consequences: The release artifact follows the same stable ABI assumptions as the debug build; any remaining emulator or hardware crash will be treated as a true runtime issue, not a compiler flag mismatch.

## D-0006: Content-based global/builtin name lookup

- Context: `Py68GlobalEntry` and the builtin table originally stored a `name_index` into a code object's constant/name table. Each `Py68Code` (module or function body) owns its own independently-numbered name table, so the same numeric index can denote different identifiers in different code objects. Once function bodies could read/write module globals and call builtins, this caused genuine cross-code-object name collisions (a function's local index 2 could collide with the module's index 2 for an unrelated name).
- Decision: Store `(const Py68U8 *name, Py68U16 name_length)` in both `Py68GlobalEntry` and the builtin registry, and resolve by byte-content comparison instead of index equality. `py68_global_set_copy`/`get_copy` and `py68_builtin_set_copy`/`get_copy` take name bytes and length directly.
- Alternatives considered: Give every code object a shared/global name table (larger refactor, touches the compiler's per-function name emission); intern all names into one process-wide table with stable indices.
- Consequences: Global/builtin lookup is a linear byte comparison rather than an index compare, which is acceptable at Language Level 0.1's expected program sizes on a 68000. A future increment may revisit interning with FNV-1a hashing (already implemented for `Py68String`) if lookup cost becomes a concern for larger programs.

## D-0007: UNBOUND sentinel for uninitialized locals

- Context: Python raises `UnboundLocalError` when a function reads a local variable before any assignment reaches it on the executed path (e.g. `if False: x = 1` then `return x`). The frame previously initialized every local slot to `None`, silently masking this class of bug and diverging from Python's documented semantics.
- Decision: Add `PY68_VALUE_UNBOUND` as a distinct `Py68ValueType` and initialize every non-parameter local slot to it when a frame is set up. `OP_LOAD_LOCAL` checks for this sentinel and raises a runtime error instead of returning it as a usable value; `OP_STORE_LOCAL` overwrites it normally.
- Alternatives considered: Track "assigned" state via a separate bitmask per frame; perform a static "definitely assigned" data-flow analysis at compile time.
## D-0008: Short-circuit and/or via JUMP_*_OR_POP

- Context: Python `and`/`or` must not evaluate the right-hand operand when the left-hand value already decides the result, and must return the deciding operand rather than a coerced boolean.
- Decision: Compile `and`/`or` with `OP_JUMP_IF_FALSE_OR_POP` / `OP_JUMP_IF_TRUE_OR_POP`. Verifier treats the jump path as keeping TOS and the fall-through path as popping TOS (stack effect -1).
- Alternatives considered: Always evaluate both sides into booleans with `OP_AND`/`OP_OR` opcodes.
- Consequences: Matches Python value-preserving short-circuit semantics; empty strings/lists are falsy via updated truthiness rules.

## D-0009: for-break pops the range iterator

- Context: `for` compilation leaves a range object on the value stack for `OP_RANGE_NEXT`. Exhausted iteration pops it before joining the exit/else path, but `break` previously jumped to the same join with the iterator still on the stack, so the verifier reported inconsistent stack depth.
- Decision: Mark for-loop contexts with `pop_on_break` and emit `OP_POP` immediately before each for-`break` jump. `OP_POP` releases the popped value. While-loops leave `pop_on_break` clear.
- Alternatives considered: Dedicated break-cleanup label after the loop, or changing `OP_RANGE_NEXT` metadata so break could jump through a shared pop block only.
- Consequences: for-`break` verifies and runs; else clauses remain skipped on break; continue is unchanged (jumps back to `RANGE_NEXT` with the iterator still under the body).

## D-0010: Subscript assignment via OP_STORE_INDEX

- Context: Language Level 0.1 documents list item assignment and `OP_STORE_INDEX` existed, but the statement parser only accepted bare-name targets, so `L[1] = 99` failed before codegen.
- Decision: After parsing a leading expression, if `=` follows and the expression is an `INDEX` node, emit an assignment whose `target` is that index. Compile as container, index, value, then `OP_STORE_INDEX`. Symbol analysis treats the target as a use (not a new binding). String item assignment remains a TypeError.
- Alternatives considered: Restrict targets to `NAME[index]` only, or invent a separate AST kind.
- Consequences: Nested stores such as `G[1][1] = 40` work because the outer index container may itself be an index expression.

## D-0011: Equality for None and bool/int

- Context: Comparisons required both operands to be `INT`, so `None == None` and `True == 1` raised TypeError despite Language Level 0.1 scalar equality rules.
- Decision: Handle `OP_EQUAL`/`OP_NOT_EQUAL` for `None` first (`None` equals only `None`). Accept `BOOL` alongside `INT` for equality and ordering by using the stored 0/1 integer payload (Python numeric policy for booleans).
- Alternatives considered: Coerce bool to int at load time only, or reject bool/int mixed comparisons.
- Consequences: `True == 1`, `False == 0`, and `None == None` match Python; unrelated types still TypeError on arithmetic/order paths that do not special-case them.

## D-0012: Language Level 0.2 file and env/assign builtins

- Context: Phase 6 requires AmigaDOS file and environment access. Language Level 0.1 has no attribute access or `with`, so Python-style file objects with methods are unavailable. Amiga “environment” for this project means DOS assigns, not `ENV:` GetVar.
- Decision: Expose function builtins `fopen`/`fclose`/`fread`/`freadline`/`fwrite`/`exists`/`remove`/`rename` with modes `r`/`w`/`a`/`rb`/`wb`/`ab`. Binary and text both use string payloads (no `bytes` type). Host installs `getenv`/`setenv`/`unsetenv`; Amiga installs `assign_get`/`assign_add`/`assign_remove` on the same platform_var_* layer (`AssignPath`, `AssignLock(name,0)`, `Lock("name:")`+`NameFromLock`). Platform I/O stays in `file_host.c` / `file_amiga.c`; `PY68_OBJECT_FILE` closes on final release. `fread(handle, count)` allocates `min(count, remaining)` via Seek/ftell, not `count` bytes upfront, so large “read all” counts are Amiga-safe. Relative `fopen` paths use the process current directory.
- Alternatives considered: method-style `open()`, Amiga `GetVar`/`SetVar`, full CPython mode matrix (`+`, `x`); pre-allocate `count+1` for every `fread`.
- Consequences: Scripts targeting Amiga should call `assign_*`. Host tests exercise file APIs and POSIX env. Emulator/hardware assign behavior remains owner-verified. Non-seekable handles are unsupported for sized `fread`.

## D-0013: True divide `/` vs floor divide `//`

- Context: Language Level 0.1 tokenized both `/` and `//` as floor-divide. Level 0.3 adds binary32 float and Python-3 true division.
- Decision: `/` emits `OP_TRUE_DIVIDE` and always yields a finite `float`. `//` remains integer floor division. NaN and Inf results are rejected as `ValueError`. No 68881 and no Amiga IEEE library: binary32 add/mul/div/parse/print are integer-only software in `src/float.c` (`-cpu=68000 -fpu=0`).
- Alternatives considered: Keep `/` as floor until a later level; use binary64.
- Consequences: Existing scripts that used `/` for floor-int must switch to `//`. Decision is a documented language-level break from 0.2.0.

## D-0014: Limited attributes and bound methods

- Context: Level 0.1 had no attribute access; file I/O used function builtins. Dict methods, `with`, and imports need `obj.name`.
- Decision: `obj.name` compiles to `OP_LOAD_ATTR`. Each heap type has a static method table. Lookup builds a `PY68_OBJECT_BOUND_METHOD` `{self, native}` consumed by `OP_CALL`. Modules resolve attributes in their global table. `STORE_ATTR` is allowed only on module objects. No user-defined attributes or classes.
- Alternatives considered: Function-style `dict_get` only; full instance dictionaries.
- Consequences: `list.append` exists alongside `list_append`. `sys.path.append` works because `sys.path` is a list.

## D-0015: Parenthesized tuples only

- Context: `(` `)` already grouped expressions. Bare `a, b` would collide with call and assignment parsing.
- Decision: Accept only parenthesized tuples: `()`, `(a,)`, `(a, b)`. A single `(expr)` remains grouping.
- Alternatives considered: Full Python tuple display including unparenthesized targets.
- Consequences: `return a, b` is a syntax error; write `return (a, b)`.
  Assignment and `for` targets later gained unparenthesized name lists
  without changing this expression-display rule (D-0038).

## D-0016: Hashable keys and cyclic containers

- Context: Dict and set need a value hash/equality protocol. Level 0.1 already rejects cyclic lists.
- Decision: Hashable: `None`, `bool`, `int`, `str`, and tuples of hashable items. Unhashable keys raise `TypeError`. Inserting a value that would make a dict reachable from itself is `ValueError: cyclic containers are not supported`. `OP_EQUAL` uses the same equality helper (so strings and lists compare).
- Alternatives considered: Allow all objects as keys via identity; add a tracing GC instead of cycle rejection.
- Consequences: Deterministic FNV-1a (strings) plus identity-free scalar hashes; no randomized hashing.

## D-0017: Catchable exceptions without classes

- Context: Runtime errors were a single aborting `Py68Error`. Level 0.4 needs `try`/`except`/`finally`/`raise`.
- Decision: Token/syntax/bytecode/memory/internal errors stay uncatchable. Other `Py68ErrorKind` values become `PY68_OBJECT_EXCEPTION` objects. `OP_SETUP_TRY` / `OP_POP_TRY` record handler IP and stack depth per frame. Matching compares exception kind to a builtin exception-type native (`TypeError`, …), not a class MRO. `finally` bodies are compiled inline before `return`/`break`/`continue`.
- Alternatives considered: Full exception class hierarchy; CPython block stack with Why flags.
- Consequences: `except TypeError as e` works; user-defined exception types do not.

## D-0018: `with` as enter / try / finally / exit

- Context: File I/O had no context managers. Level 0.4 adds `with`.
- Decision: Compile `with EXPR as NAME` to keep the manager on the stack, call `__enter__`, bind the result, wrap the body in `SETUP_TRY`, and call `__exit__(None, None, None)` on both success and handler paths. First context manager: `PY68_OBJECT_FILE` (`__enter__` returns self, `__exit__` closes).
- Alternatives considered: Method-style `open()`; no `as` target only.
- Consequences: `with fopen(path, mode) as f:` is the supported form.

## D-0019: Single-directory import loader

- Context: Level 0.1 had no import system. “Module” meant the top-level script code object.
- Decision: `import` / `from` / `as` compile to `OP_IMPORT_NAME` / `OP_IMPORT_FROM`. The loader reads `name.py` from the importing source directory, then `sys.path` entries. Compiled modules are `PY68_OBJECT_MODULE` objects cached by resolved path. Relative imports and `import *` stay unsupported. `sys` is a builtin module (`path`, `modules`, `argv`).
- Alternatives considered: Full package/`__init__.py` trees; CPython `.pyc`.
- Consequences: Multi-file programs work for sibling `.py` files; no CPython bytecode compatibility.

## D-0023: Transactional single-level import cache

- Context: Imported modules must execute once, failed imports must not leave
	stale globals, and recursive imports must not recurse indefinitely.
- Decision: Insert a module in the cache with a private loading flag before
	executing it. A lookup of a loading module reports `ImportError: import cycle
	detected`; successful execution clears the flag, while any failure removes
	the cache entry and releases the partial module.
- Alternatives considered: Execute imports without caching, expose partially
	initialized modules to cycles, or add package-style import state.
- Consequences: Cache identity and one-time execution are deterministic. Cycles
	are rejected intentionally; packages, dotted names, and relative imports
	remain outside this increment.

## D-0028: Imported function code and module globals

- Context: A Python `random` library needs `import random` and callable defs that
	read module-level PRNG state. After import, destroying the module code object
	left function objects with dangling bytecode; nested `import` cleared
	`executing_module`, so defs after an import bound the wrong globals.
- Decision: Retain each module's `Py68Code` on the module (`owned_code`) for the
	module lifetime. `MAKE_FUNCTION` records the defining module; `LOAD_GLOBAL`
	in that function uses the module's globals. Nested imports save/restore
	`executing_module`.
- Alternatives considered: C-only PRNG builtins; require `from module import *`
	style flattening; retain code in each function object.
- Consequences: Imported user functions remain callable and see their defining
	module globals. Module teardown releases globals before destroying `owned_code`.

## D-0020: `set` and `dict` are names, not keywords

- Context: The 0.1 tokenizer classified `set` and `dict` as unsupported keywords, unlike Python where they are builtins.
- Decision: Remove them from the keyword table so they tokenize as `PY68_TOKEN_NAME` and resolve to constructor builtins.
- Alternatives considered: Keep them as keywords that introduce literal syntax only.
- Consequences: `set = 1` is a legal (if unwise) assignment that shadows the builtin.

## D-0024: 8-bit ASCII string methods vs CPython Unicode

- Context: The string builtins reference mirrors CPython Unicode `str` APIs. Python68K strings are 8-bit byte strings (Language Level contract), not Unicode.
- Decision: Implement Phase-2/Phase-3 `str` methods and related text builtins with ASCII / 8-bit semantics only. Case mapping and classifiers operate on `A-Z`/`a-z` and ASCII digit/whitespace/printable ranges; other bytes are left unchanged (case) or rejected by classifiers as appropriate. `casefold` is an ASCII alias of `lower`. `isdecimal`/`isdigit`/`isnumeric` all mean ASCII `'0'-'9'`. `isidentifier` follows the tokenizer’s ASCII name rules. `chr` accepts `0..255` only (not `0..0x10FFFF`). Literal escapes `\\ \' \" \n \r \t \xHH` are decoded when materializing string constants.
- Alternatives considered: Fake Unicode tables; leave escapes undecoded in constants.
- Consequences: Scripts that rely on Unicode casefolding, numeric characters, or code points above 255 are out of scope. Documented divergence from CPython is intentional.

## D-0025: `maketrans` builtin; deferred encode/bytes/eval/format_map

- Context: CPython exposes `str.maketrans` as a static method on the `str` type object. Python68K has no type objects. Full `str.format` / `format_map`, `bytes`/`bytearray`/`encode`, and `eval`/`exec`/`compile` conflict with Level 0.1 constraints (no kwargs, no bytes type, security).
- Decision: Expose `maketrans(x[, y[, z]])` as an import-free builtin returning a `dict` of int→int/None mappings; `str.translate(table)` consumes that dict (or any compatible dict). Provide minimal `format(value[, format_spec])` for ints (`''`, `d`, width, `0`-pad such as `04d`). Defer `bytes`/`bytearray`/`encode`, `eval`/`exec`/`compile`, full `str.format`/`format_map` with replacement fields and kwargs. General iterator builtins beyond `iter`/`next` (`enumerate`/`reversed`, `iter(callable, sentinel)`) remain deferred (D-0042). Restricted f-strings are provided in D-0043. `sorted(iterable)` is provided without `key`/`reverse` (D-0039). `ascii` escapes bytes `>= 128` as `\xHH`; `repr` leaves high bytes literal when printable.
- Alternatives considered: Opaque translation-table object; alias `ascii` to `repr`.
- Consequences: `maketrans` is a name in the builtin table, not `str.maketrans`. Advanced formatting and encoding remain future work. `sum` is provided separately (D-0041).

## D-0026: Comprehensions bind in the enclosing scope

- Context: Python 3 list/set/dict comprehensions run in a nested function scope so loop targets do not leak. Python68K does not implement nested `def`, closures, or cell variables.
- Decision: Compile `[elt for x in it if cond]`, `{elt for ...}`, and `{k: v for ...}` inline in the current code object using `BUILD_LIST`/`BUILD_SET`/`BUILD_DICT` plus the existing `RANGE_INIT`/`RANGE_NEXT` loop, then `LIST_APPEND`/`SET_ADD`/`MAP_ADD`. The target `x` is a normal `for` assignment: a function local if the comprehension appears in a function, otherwise a module global. Nested `for` clauses and zero or more `if` filters per clause are supported. Generator expressions `(elt for ...)` are a targeted syntax error.
- Alternatives considered: Desugar to an anonymous nested function (requires closures); emit only list comprehensions and reject set/dict forms; keep the comprehension result in a compiler-generated temp name.
- Consequences: `xs = [n for n in range(3)]; print(n)` prints `2`, matching `for`. A comprehension target assigned anywhere in a function makes that name local throughout the function (unbound reads raise `NameError`). Empty `{}` remains an empty dict; `{x for x in it}` is a set comprehension and `{k: v for ...}` is a dict comprehension, so they do not collide with `{k: v}` / `{a, b}` literals (D-0020).

## D-0034: VM break polling on backward branches; AmigaOS needs no cooperative yield

- Context: A CPU-bound Python68K loop cannot be aborted, because the VM never inspects the task signal set. AmigaOS is preemptively multitasking at the Exec level (Workbench is only the GUI launcher, not a scheduler), so a busy loop does not starve other programs and there is no Windows-style message pump that must be called for fairness.
- Decision: Add `py68_platform_poll(runtime, flags)` with `PY68_POLL_BREAK` (consume a pending user break) and `PY68_POLL_YIELD` (politeness hint only). The Amiga implementation uses `CheckSignal(SIGBREAKF_CTRL_C)` and, for the yield hint, `Forbid(); Permit();` because Exec has no `Yield()`. The host implementation uses a `SIGINT` handler and a `volatile sig_atomic_t` flag. `py68_platform_signal_break` posts a break to the current process (`Signal(FindTask(NULL), SIGBREAKF_CTRL_C)` on Amiga) and makes the behaviour testable on the host. The VM calls the hook only on taken backward branches, throttled by `runtime->poll_interval` (default `PY68_POLL_INTERVAL_DEFAULT`, 256); `poll_interval == 0` disables polling. A consumed break raises `PY68_ERROR_INTERRUPT` ("KeyboardInterrupt"), which is deliberately absent from `py68_error_is_catchable`, so `except:` cannot swallow it. `main` maps it to exit code 10 (AmigaDOS `RETURN_ERROR`).
- Alternatives considered: Poll every instruction (unaffordable dispatch cost on 68000); poll on call/return as well (recursion is already bounded by the recursion limit); `Delay(1)` as the yield primitive (costs a full 20 ms tick, so it is reserved for an explicit script-level yield); making `KeyboardInterrupt` catchable like CPython (would let a bare `except` inside a loop defeat Ctrl-C).
- Consequences: `PY68_ERROR_INTERRUPT` is appended last in `Py68ErrorKind` so existing kind numbers stay stable. Straight-line code pays nothing; a loop iteration pays one decrement and branch. Scripts cannot catch or suppress a user break at Language Level 0.5. Script-visible `yield_cpu` / `set_priority` / `check_break` and Ctrl-C verification under emulation remain future increments.

## D-0035: Break and scheduling builtins are import-free names

- Context: D-0034 added the VM-level break poll. Scripts still need a way to observe a break themselves, to tune the poll rate, and to be explicitly polite to other tasks.
- Decision: Register `check_break()`, `yield_cpu()`, `set_poll_interval(count)`, and `get_poll_interval()` as import-free builtins in the common table, alongside `time`/`sleep`, rather than behind an `amiga` or `sys` module. `check_break()` consumes the pending break and returns `True` exactly once, so a script that calls it takes responsibility for stopping. `yield_cpu()` passes only `PY68_POLL_YIELD` and never consumes a break. `set_poll_interval` accepts a non-negative int (`0` disables VM polling), rejects other types with `TypeError` and negatives with `ValueError`, and resets `poll_counter` so the new interval applies immediately.
- Alternatives considered: A `sys` module namespace (Python68K has no attribute-settable module objects for runtime knobs and `sys` is already a fixed module); making `check_break()` non-consuming (a loop would then see `True` forever and the VM poll would raise anyway); mapping `set_poll_interval` onto a CPython-style `sys.setcheckinterval` name (misleading, since Python68K counts backward branches, not instructions).
- Consequences: Four more names occupy the builtin table on every target, including the host, so host and Amiga scripts stay source compatible. `yield_cpu()` is a no-op on the host. Task priority control (`SetTaskPri`) is still not exposed.
