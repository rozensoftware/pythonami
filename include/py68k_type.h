/* 2026 by Piotr Rozentreter (Rozsoft) */

#ifndef PY68K_TYPE_H
#define PY68K_TYPE_H

#include "py68k_object.h"
#include "py68k_status.h"

struct Py68Dict;
struct Py68String;

struct Py68Type {
    Py68Object base;
    struct Py68String *name;      /* retained */
    struct Py68Type *base_type;   /* retained; NULL only for object/BaseException roots */
    struct Py68Dict *dict;        /* retained class namespace */
    Py68U16 exception_kind;       /* 0 = not builtin-exception-typed; else PY68_ERROR_* */
    Py68U16 flags;                /* bit0: is exception type */
};
typedef struct Py68Type Py68Type;

#define PY68_TYPE_FLAG_EXCEPTION 1

struct Py68Instance {
    Py68Object base;
    Py68Type *type;               /* retained */
    struct Py68Dict *dict;        /* retained; may be empty dict */
};
typedef struct Py68Instance Py68Instance;

Py68Status py68_type_new(struct Py68Runtime *runtime, struct Py68String *name,
                         Py68Type *base_type, struct Py68Dict *dict,
                         Py68Type **result);
Py68Status py68_instance_new(struct Py68Runtime *runtime, Py68Type *type,
                             Py68Instance **result);
int py68_type_is_subtype(Py68Type *derived, Py68Type *base);
/* Walk type dict then base chain; copies value out (retained). Sets *found
   without raising when the name is absent. */
Py68Status py68_type_lookup(struct Py68Runtime *runtime, Py68Type *type,
                            const Py68U8 *name, Py68U16 name_length,
                            Py68Value *result, int *found);
Py68Status py68_build_class(struct Py68Runtime *runtime,
                            Py68U16 argument_count, Py68Value *arguments,
                            Py68Value *result);
/* Install object, type, BaseException, Exception hierarchy and isinstance/
   issubclass into builtins. Call from py68_builtins_install AFTER other
   natives; REPLACE the old native-stub exception constructors. */
Py68Status py68_types_install(struct Py68Runtime *runtime);
/* Map error kind to installed exception type (borrowed). NULL if unknown. */
Py68Type *py68_type_for_error_kind(struct Py68Runtime *runtime, Py68U16 kind);
Py68Type *py68_runtime_object_type(struct Py68Runtime *runtime);
Py68Type *py68_runtime_type_type(struct Py68Runtime *runtime);
/* Call an exception type like TypeError([msg]) — used by OP_CALL until full
   type.__call__ exists. */
Py68Status py68_type_call_as_exception(struct Py68Runtime *runtime,
                                       Py68Type *type,
                                       Py68U16 argument_count,
                                       Py68Value *arguments,
                                       Py68Value *result);
/* type(obj) with one argument (instances, exception objects, type objects). */
Py68Status py68_type_of_value(struct Py68Runtime *runtime, Py68Value value,
                              Py68Value *result);

#endif
