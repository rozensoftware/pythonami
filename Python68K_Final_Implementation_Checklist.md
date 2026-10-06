# Python68K Final Implementation Checklist

## Phase 0 - Bootstrap
- [x] Repository skeleton created
- [x] Host build (GCC/Clang) working
- [x] Amiga build (vbcc) working
- [x] `-cpu=68000 -fpu=0` enforced
- [x] Platform abstraction layer implemented
- [x] Tracked allocator implemented
- [x] Type-width assertions implemented
- [x] `python -V` implemented
- [x] `python --help` implemented
- [x] Zero warnings in debug builds
- [x] Allocator tests passing

## Phase 1 - Tokenizer
- [x] Token definitions complete
- [x] Integer literals
- [x] String literals
- [x] Identifier parsing
- [x] Comment handling
- [x] NEWLINE generation
- [x] INDENT generation
- [x] DEDENT generation
- [x] Tab rejection in indentation
- [x] Line/column tracking
- [x] EOF token generation
- [x] Tokenizer test suite passing

## Phase 2 - Parser & AST
- [x] AST arena allocator implemented
- [x] Pratt expression parser implemented
- [x] Statement parser implemented
- [x] Operator precedence verified
- [x] Function parsing implemented
- [x] Loop parsing implemented
- [x] Slice parsing implemented
- [ ] Grammar compliance tests passing

## Phase 3 - Symbol Analysis
- [x] Function prepass implemented
- [x] Duplicate parameter detection
- [x] Local slot assignment
- [x] UNBOUND local support
- [x] Global name classification
- [x] Builtin resolution order implemented
- [x] Context validation (return/break/continue)

## Phase 4 - Bytecode Compiler
- [x] Constant pool implementation
- [x] Name table implementation
- [ ] String interning
- [x] FNV-1a hashing
- [x] Opcode metadata generation
- [x] Branch patching
- [x] Function code generation
- [x] Control-flow code generation
- [x] Short-circuit logic generation

## Phase 5 - Bytecode Verification
- [x] Instruction boundary validation
- [x] Branch validation
- [x] Index validation
- [x] Stack-depth validation
- [x] CFG traversal implemented
- [x] Maximum stack calculation
- [x] Malformed bytecode rejection

## Phase 6 - Runtime Values
- [x] Reference-counted objects
- [x] String object
- [x] List object
- [x] Function object
- [x] Native function object
- [x] Ownership rules implemented
- [x] Retain/release APIs implemented
- [x] Leak detection tests passing

## Phase 7 - VM Core
- [x] Value stack implemented
- [x] Frame stack implemented
- [x] Global table implemented
- [x] Builtin table implemented
- [x] Opcode dispatch loop implemented
- [x] Error propagation implemented
- [x] Full VM unwind implemented
- [x] Traceback support implemented

## Phase 8 - Arithmetic Semantics
- [x] Checked add
- [x] Checked subtract
- [x] Checked multiply
- [x] Checked negate
- [x] Python floor division semantics
- [x] Python modulo semantics
- [ ] Overflow tests passing

## Phase 9 - Strings, Lists, Range
- [x] String concatenation
- [x] String slicing
- [x] String indexing
- [x] List append
- [x] List pop
- [x] List indexing
- [x] List assignment
- [x] Range implementation
- [x] Cycle detection for lists

## Phase 10 - Functions
- [x] Parameter passing
- [x] Local variables
- [x] Recursion
- [x] Explicit return
- [x] Implicit None return
- [x] Call validation

## Phase 11 - Builtins
- [x] print
- [x] len
- [x] range
- [x] int
- [x] str
- [x] bool
- [x] abs
- [x] min
- [x] max
- [x] list_append
- [x] list_pop
- [x] exit
- [x] time
- [x] sleep
- [x] ctime
- [x] localtime
- [x] strftime
- [x] perf_counter

## Phase 12 - AmigaDOS Integration
- [x] Script execution
- [x] -c execution
- [ ] argv support
- [x] Synchronous `os.system` command execution
- [ ] `os.popen` `PIPE:` output capture verified on Amiga
- [x] File APIs
- [x] Environment APIs
- [x] Exit-code mapping
- [ ] Memory stats output

## Phase 13 - Comprehensions (Language Level 0.6)
- [x] List, set, and dict comprehension parsing and compilation
- [x] Nested clauses and filters
- [x] Comprehension target binding covered by host fixtures
- [x] Host comprehension language fixture passes (`make language-test`)

## Phase 14 - Generators (Language Level 0.7)
- [x] `yield` statement and lazy generator creation
- [x] Generator expression compilation and free-variable behavior
- [x] Supported builtins consume generators through VM execution
- [x] Host generator language fixtures pass (`make language-test`)

## Phase 15 - Classes and exception types (Language Level 0.8)
- [x] User type and instance runtime
- [x] Single inheritance, instance attributes, and bound methods
- [x] Restricted module-level class suite compilation
- [x] Builtin exception type hierarchy and user exception subclasses
- [x] Host class/exception language fixture passes (`make language-test`)

## Quality Gates
- [x] No memory leaks (full `make test` suite run under ASan/UBSan, zero findings)
- [x] All unit tests pass
- [ ] All negative tests pass
- [ ] All differential tests pass
- [ ] Allocation-failure tests pass
- [x] Host debug build passes
- [x] Host release build passes
- [x] Amiga debug build passes
- [x] Amiga release build passes
- [ ] Executes on emulator
- [ ] Executes on real 68000 hardware

## MVP Acceptance
- [x] python script.py works
- [x] python -c works
- [x] Bytecode VM executes code
- [x] Functions work
- [x] Recursion works
- [x] Strings work
- [x] Lists work
- [x] Tracebacks work
- [ ] Unsupported syntax rejected cleanly
- [ ] No 68020 instructions present
- [ ] No FPU required
- [ ] Documentation complete
- [ ] Language Level 0.1 frozen

## Future / post-0.8
- [ ] CPython-style `open` (option 1): `open()` + `file.read()`/`write()`/`close()`, limited keyword args for `encoding='utf-8'` (accept/ignore), `FileNotFoundError`, keep `fopen` as alias; driver: `examples/wordcount.py`
