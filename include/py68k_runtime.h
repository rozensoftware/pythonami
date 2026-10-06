/* 2026 by Piotr Rozentreter (Rozsoft) */

#ifndef PY68K_RUNTIME_H
#define PY68K_RUNTIME_H

#include "py68k_error.h"
#include "py68k_code.h"
#include "py68k_memory.h"
#include "py68k_object.h"
#include "py68k_platform.h"
#include "py68k_status.h"
#include "py68k_types.h"

#define PY68_TRY_MAX 8
#define PY68_PATH_MAX 256
/* Backward branches between two py68_platform_poll calls. */
#define PY68_POLL_INTERVAL_DEFAULT 256U

struct Py68Generator;
struct Py68ExtServices;

/* How a generator activation hands its result back to the resuming site
   (D-0045). PY68_RESUME_NONE marks an ordinary call frame. */
#define PY68_RESUME_NONE 0
/* return_ip addresses the OP_RANGE_NEXT that resumed the generator: a yield
   continues after it, exhaustion re-executes it and takes its exit branch. */
#define PY68_RESUME_FOR 1
/* return_ip addresses the instruction after OP_CALL and stack_base - 1 holds
   the result slot: None for next(it), the default for next(it, default). */
#define PY68_RESUME_NEXT 2
#define PY68_RESUME_NEXT_DEFAULT 3
/* Draining a generator argument into a list for a builtin that consumes an
   iterable. return_ip addresses the OP_CALL itself and stack_base - 1 holds the
   list being filled: each yield appends and keeps running, and exhaustion
   replaces the generator argument with the list and re-executes the call. */
#define PY68_RESUME_COLLECT 4

typedef struct Py68TryBlock {
    Py68U32 handler_ip;
    Py68U16 stack_depth;
} Py68TryBlock;

typedef struct Py68Frame {
    Py68Code *code;
    Py68Code *return_code;
    Py68Value *locals;
    /* Borrowed defining module for LOAD_GLOBAL; NULL uses runtime->globals. */
    struct Py68Module *globals_owner;
    Py68U16 local_count;
    Py68U16 argument_count;
    Py68U32 return_ip;
    Py68TryBlock try_stack[PY68_TRY_MAX];
    Py68U16 try_count;
    /* Generator activation (D-0045): retained generator whose locals this
       frame borrows, the value-stack index where its operands begin, and how
       to deliver the next yielded value. NULL for ordinary call frames. */
    struct Py68Generator *generator;
    Py68U16 stack_base;
    Py68U16 resume_kind;
    /* When calling type.__init__, holds the constructing instance; on return
       the instance is pushed instead of __init__'s None (Level 0.8). */
    Py68Value constructing;
} Py68Frame;

typedef struct Py68GlobalEntry {
    const Py68U8 *name;      /* borrowed: bytes owned by a live source buffer */
    Py68U16 name_length;
    Py68Value value;
    Py68U16 occupied;
} Py68GlobalEntry;

struct Py68Runtime {
    /* Must remain first: plugins read via Py68ExtRuntimeHead (D-0048). */
    const struct Py68ExtServices *ext_services;
    Py68Allocator allocator;
    Py68Error error;
    Py68Value *value_stack;
    Py68U16 value_stack_count;
    Py68U16 value_stack_capacity;
    Py68Frame *frames;
    Py68U16 frame_count;
    Py68U16 frame_capacity;
    Py68U16 recursion_limit;
    Py68GlobalEntry *globals;
    Py68U16 global_count;
    Py68U16 global_capacity;
    Py68GlobalEntry *builtins;
    Py68U16 builtin_count;
    Py68U16 builtin_capacity;
    Py68Object *live_objects;
    Py68I32 requested_exit_code;
    Py68U32 poll_counter;
    Py68U32 poll_interval;
    Py68U16 trace_enabled;
    char traceback[512];
    Py68U16 traceback_length;
    Py68Value current_exception;
    char script_dir[PY68_PATH_MAX];
    struct Py68Object **import_modules;
    Py68U16 import_count;
    Py68U16 import_capacity;
    struct Py68List *sys_path;
    struct Py68List *sys_argv;
    struct Py68Object *sys_module;
    struct Py68NativeFunction *active_native;
    /* Set while executing an imported module body so MAKE_FUNCTION can bind
       LOAD_GLOBAL to that module's globals. */
    struct Py68Module *executing_module;
    struct Py68Type *object_type;
    struct Py68Type *type_type;
    struct Py68Type *base_exception_type;
    struct Py68Type *exception_type;
    /* Indexed by Py68ErrorKind; NULL for non-exception kinds. Borrowed from
       builtins. */
    struct Py68Type *exception_by_kind[32];
};

void py68_runtime_initialize_struct(Py68Runtime *runtime);
Py68Status py68_runtime_initialize(Py68Runtime *runtime);
void py68_runtime_shutdown(Py68Runtime *runtime);

#endif
