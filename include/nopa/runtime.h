#ifndef NOPA_RUNTIME_H
#define NOPA_RUNTIME_H

/*
 * Nopa runtime header.
 *
 * Two modes:
 *   default               — host/OS mode: pulls in libc <setjmp.h>/<stdarg.h>,
 *                           exception state is thread-local (__thread).
 *   __NOPA_FREESTANDING   — bare-metal mode (set by `nopac -fno-libc`):
 *                           no libc headers; setjmp/longjmp map to Clang
 *                           builtins; exception state is plain globals
 *                           (single-core assumption). The user supplies
 *                           <stdint.h>/<stddef.h>/<stdbool.h> (freestanding).
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __NOPA_FREESTANDING
typedef void *jmp_buf[16];
#define setjmp(env)         __builtin_setjmp(env)
#define longjmp(env, val)   __builtin_longjmp((env), (val))
#else
#include <stdarg.h>
#include <setjmp.h>
#endif

// ─── Public types (used by generated code) ─────────────────────────────────────

typedef bool Bool;
typedef int BOOL;
#define YES  true
#define NO   false

typedef struct {
    const char *name;
    unsigned hash;
} SEL;

typedef struct NPClass NPClass;
typedef struct nopa_root nopa_root;
typedef struct NPObject NPObject;
typedef struct NPString NPString;
typedef NPObject *id;
typedef NPObject *nopa_id_t;

/* Always declared: the transpiler's nopa_metaInit() references it.
 * Definition comes from the user (freestanding) or Foundation (host). */
extern NPClass NOPA_CLASS_$_nopa_root;

/* memcpy is used by the @try/@catch @finally jmp_buf save/restore
 * (the generated code always calls memcpy for nesting save/restore).
 * Guard against macOS's fortified memcpy macro. */
#ifndef memcpy
void *memcpy(void *dst, const void *src, size_t n);
#endif

#ifdef __NOPA_FREESTANDING

#ifndef NOPA_ROOT_DEFINED
#define NOPA_ROOT_DEFINED
struct nopa_root {
    struct NPClass *isa;
    uint32_t retain_count;
};
#endif

#endif /* end of __NOPA_FREESTANDING guarded structs */

#ifndef NPOBJECT_DEFINED
#define NPOBJECT_DEFINED
struct NPObject {
    struct NPClass *isa;
    uint32_t retain_count;
};
#endif

struct NPClass {
    const char *name;
    NPClass *superclass;
    size_t instance_size;
    void *vtable;
    void *class_vtable;
    struct NPProtocol **protocols;
    int protocol_count;
    /* Populated by nopa_metaInit() (codegen). nopa_release() calls it when the
     * retain count reaches 0, so per-class dealloc cleanup (free-ing ivars)
     * actually runs. NULL if the class defines no instance dealloc. */
    void (*dealloc)(NPObject *, SEL);
};

// ─── Protocol types ──────────────────────────────────────────────────────────

typedef struct NPProtocolMethod {
    const char *name;
    const char *encoding;
} NPProtocolMethod;

struct NPProtocol {
    const char *name;
    struct NPProtocol **parents;
    int parent_count;
    NPProtocolMethod *required_methods;
    int required_count;
    NPProtocolMethod *optional_methods;
    int optional_count;
};

// ─── Internal types (for runtime implementation) ────────────────────────────────

typedef struct np_vtable np_vtable_t;
typedef struct np_class  np_class_t;
typedef struct np_object np_object_t;

struct np_vtable {
    np_class_t *isa;
    void      (**methods)(void);
    int        method_count;
};

struct np_class {
    np_class_t  *superclass;
    const char  *name;
    size_t       instance_size;
    np_vtable_t *vtable;
    void       (*constructor)(np_object_t *self, ...);
};

struct np_object {
    np_class_t *isa;
};

// ─── Runtime API ────────────────────────────────────────────────────────────────

SEL sel_registerName(const char *name);

/* Class-metadata initialization. The transpiler emits `nopa_metaInit()` at the
 * top of every `main` and declares it in generated C; every other symbol in this
 * header is snake_case, so hand-written host code naturally reaches for
 * `nopa_meta_init()` and hits an implicit-declaration error. This alias accepts
 * that spelling. The target is defined by the generated C (weak, so exactly one
 * TU's copy wins the link); it is not declared here because a declaration would
 * force every TU — including ones with no classes — to define it. */
void nopa_meta_init(void);

/* Memory allocator (user-provided in freestanding; libc calloc/free on host).
 * nopa_malloc must zero-initialize memory. */
void *nopa_malloc(size_t size);
void  nopa_free(void *ptr);

NPObject *nopa_alloc(NPClass *cls);
NPObject *nopa_init(NPObject *self);

// Exception globals (TLS for thread safety; plain globals in freestanding)
#ifdef __NOPA_FREESTANDING
extern jmp_buf __nopa_exception_buf;
extern id     __nopa_exception_value;
#else
extern __thread jmp_buf __nopa_exception_buf;
extern __thread id     __nopa_exception_value;
#endif

// Checked-exception (-eh checked) error flag: 1 while an error propagates.
// Set by desugared `@throw`, cleared by the matching `@catch`; every nopa
// call site checks it and early-returns to propagate. Same TLS/freestanding
// split as the sjlj exception state above.
#ifdef __NOPA_FREESTANDING
extern int  __nopa_eh_flag;
extern id   __nopa_eh_val;
#else
extern __thread int  __nopa_eh_flag;
extern __thread id   __nopa_eh_val;
#endif
/* Isa check for typed catches: 1 when obj's isa chain matches cls.
 * NULL obj never matches. The desugar passes &NOPA_CLASS_$_<Flat> for the
 * catch type (codegen emits the extern declaration in the same TU). */
int __nopa_eh_isa(NPObject *obj, NPClass *cls);
/* Uncaught checked exception escaped main: print ObjC wording + abort().
 * Called by main's function-tail guard (desugar) instead of `return zero`. */
void nopa_eh_uncaught(void);

NPObject *nopa_retain(NPObject *obj);
void nopa_release(NPObject *obj);
NPObject *nopa_autorelease(NPObject *obj);
BOOL nopa_isKindOf(NPObject *obj, NPClass *cls);
/* Official ObjC spelling of nopa_isKindOf (same isa-chain walk). */
BOOL nopa_isKindOfClass(NPObject *obj, NPClass *cls);

// ─── Autorelease pool API ────────────────────────────────────────────────────────

typedef struct nopa_autoreleasepool nopa_autoreleasepool_t;
nopa_autoreleasepool_t *nopa_autoreleasepoolPush(void);
void nopa_autoreleasepoolPop(nopa_autoreleasepool_t *pool);

// ─── Async task API (route map item #4) ─────────────────────────────────────

/* A cooperative async task. The desugared async method is a state machine:
 * each `await` splits the body into a segment stored in one switch state.
 * The generated method body creates a task, pumps nopa_task_resume until the
 * state machine finishes (single-threaded cooperative scheduling: pumping
 * always makes progress), and reads the result from the frame. */
typedef struct NPTask NPTask;

/* State-machine entry written by the desugar pass.
 * Returns 0 = suspended at an await, 1 = finished. */
typedef int (*nopa_task_entry_fn)(NPTask *task);

struct NPTask {
    int state;                          /* current state-machine state */
    int finished;                       /* 0 = running/suspended, 1 = done */
    nopa_task_entry_fn entry;           /* state machine body */
    NPObject *self_obj;                 /* receiver the method runs on */
    void *frame;                        /* lifted locals (per-method struct) */
    void *result;                       /* return value slot (caller casts) */
    NPTask *parent;                   /* awaiting task that created us, if any */
};

/* Create a task. `frame_size` is the size of the method's lifted-locals
 * struct; the frame is zero-initialized and owned by the task. */
NPTask *nopa_task_create(nopa_task_entry_fn entry, NPObject *self_obj, size_t frame_size);

/* Run the state machine until it suspends (return 0) or finishes (return 1). */
int nopa_task_resume(NPTask *task);

/* Pump the task to completion (cooperative single-thread scheduling) and
 * free it. Returns the task's result slot value. */
void *nopa_task_join(NPTask *task);

/* Mark the task finished (called by the state machine's final state). */
void nopa_task_finish(NPTask *task);

// ─── Internal API (for runtime implementation) ───────────────────────────────────

void np_class_register(np_class_t *cls);
np_class_t *np_class_create(const char *name, np_class_t *superclass, size_t instance_size);
void np_class_set_vtable(np_class_t *cls, np_vtable_t *vtable);
np_vtable_t *np_vtable_alloc(int method_count);
void np_vtable_set_method(np_vtable_t *vt, int index, void (*method)(void));
np_object_t *np_object_alloc(np_class_t *cls);
void np_object_dealloc(np_object_t *obj);

// ─── Logging (like NSLog / NSObjCRuntime.h) ───────────────────────────────────

void NPLog(NPString *format, ...);
#ifndef __NOPA_FREESTANDING
void __NPLogv(NPString *format, va_list args);
#endif

// ─── Block runtime (Clang Blocks ABI) ─────────────────────────────────────────
/* HOSTED: provided by the platform's Blocks runtime (macOS libSystem; Linux
 * needs -fblocks + -lBlocksRuntime). FREESTANDING (-ffreestanding): no block
 * runtime is bundled — block literals reference __NSConcreteStackBlock and
 * _Block_copy/_Block_release. On real bare metal either link a Blocks runtime
 * port or use `-backend portable|gcc` (blocks lower to plain C functions with
 * no ABI symbols). */

void *_Block_copy(const void *aBlock);
void _Block_release(const void *aBlock);

// ─── Weak reference API ─────────────────────────────────────────────────────────

void nopa_weakRegister(NPObject **weak_loc, NPObject *target);
void nopa_weakUnregister(NPObject **weak_loc);
void nopa_weakClearAll(NPObject *target);

// ─── String literals ──────────────────────────────────────────────────────────

NPObject *nopa_stringFromCstr(const char *cstr);
void nopa_weakAutoCleanup(void *ptr);

// ─── @synchronized monitors ──────────────────────────────────────────────────
//
// Per-object lock table keyed by object address (FNV-1a into a fixed bucket
// array). `nopa_syncLock` spins until the bucket's atomic flag is acquired and
// returns the bucket index to hand to `nopa_syncUnlock`; the generated code
// holds it in a `long` whose cleanup attribute releases the lock on every
// scope exit (return/break/continue included — `longjmp` past the scope is
// the one exit the attribute cannot see, so the checker warns on `@throw`
// escaping a `@synchronized` block). Buckets are global (not per-object
// allocations): correctness degrades to lock coarsening — two distinct
// objects hashing to the same bucket serialize — never to missed mutual
// exclusion. Freestanding builds keep the API (single-core: lock/unlock are
// no-ops) so transpiled code compiles unchanged.

long nopa_syncLock(void *object);
void nopa_syncUnlock(long bucket);
void nopa_syncAutoCleanup(void *ptr);   // cleanup attr: long* → unlock

// ─── helpers ──────────────────────────────────────────────────────────────────

#define NP_OBJECT_ISA(obj)        (((np_object_t *)(obj))->isa)
#define NP_CLASS_NAME(cls)        ((cls)->name)
#define NP_CLASS_SUPER(cls)       ((cls)->superclass)
#define NP_VTABLE_LOOKUP(obj, idx)  ((obj)->isa->vtable->methods[(idx)])

#endif /* NOPA_OBJECT_H */