#ifndef NUPA_RUNTIME_H
#define NUPA_RUNTIME_H

/*
 * Nupa runtime header.
 *
 * Two modes:
 *   default               — host/OS mode: pulls in libc <setjmp.h>/<stdarg.h>,
 *                           exception state is thread-local (__thread).
 *   __NUPA_FREESTANDING   — bare-metal mode (set by `nupac -fno-libc`):
 *                           no libc headers; setjmp/longjmp map to Clang
 *                           builtins; exception state is plain globals
 *                           (single-core assumption). The user supplies
 *                           <stdint.h>/<stddef.h>/<stdbool.h> (freestanding).
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __NUPA_FREESTANDING
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
typedef struct nupa_root nupa_root;
typedef struct NPObject NPObject;
typedef struct NPString NPString;
typedef NPObject *id;
typedef NPObject *nupa_id_t;

/* Always declared: the transpiler's nupa_metaInit() references it.
 * Definition comes from the user (freestanding) or Foundation (host). */
extern NPClass NUPA_CLASS_$_nupa_root;

/* memcpy is used by the @try/@catch @finally jmp_buf save/restore
 * (the generated code always calls memcpy for nesting save/restore).
 * Guard against macOS's fortified memcpy macro. */
#ifndef memcpy
void *memcpy(void *dst, const void *src, size_t n);
#endif

#ifdef __NUPA_FREESTANDING

#ifndef NUPA_ROOT_DEFINED
#define NUPA_ROOT_DEFINED
struct nupa_root {
    struct NPClass *isa;
    uint32_t retain_count;
};
#endif

#endif /* end of __NUPA_FREESTANDING guarded structs */

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
    /* Populated by nupa_metaInit() (codegen). nupa_release() calls it when the
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

/* Class-metadata initialization. The transpiler emits `nupa_metaInit()` at the
 * top of every `main` and declares it in generated C; every other symbol in this
 * header is snake_case, so hand-written host code naturally reaches for
 * `nupa_meta_init()` and hits an implicit-declaration error. This alias accepts
 * that spelling. The target is defined by the generated C (weak, so exactly one
 * TU's copy wins the link); it is not declared here because a declaration would
 * force every TU — including ones with no classes — to define it. */
void nupa_meta_init(void);

/* Memory allocator (user-provided in freestanding; libc calloc/free on host).
 * nupa_malloc must zero-initialize memory. */
void *nupa_malloc(size_t size);
void  nupa_free(void *ptr);

NPObject *nupa_alloc(NPClass *cls);
NPObject *nupa_init(NPObject *self);

// Exception globals (TLS for thread safety; plain globals in freestanding)
#ifdef __NUPA_FREESTANDING
extern jmp_buf __nupa_exception_buf;
extern id     __nupa_exception_value;
#else
extern __thread jmp_buf __nupa_exception_buf;
extern __thread id     __nupa_exception_value;
#endif

// Checked-exception (-eh checked) error flag: 1 while an error propagates.
// Set by desugared `@throw`, cleared by the matching `@catch`; every nupa
// call site checks it and early-returns to propagate. Same TLS/freestanding
// split as the sjlj exception state above.
#ifdef __NUPA_FREESTANDING
extern int  __nupa_eh_flag;
extern id   __nupa_eh_val;
#else
extern __thread int  __nupa_eh_flag;
extern __thread id   __nupa_eh_val;
#endif
/* Isa check for typed catches: 1 when obj's isa chain matches cls.
 * NULL obj never matches. The desugar passes &NUPA_CLASS_$_<Flat> for the
 * catch type (codegen emits the extern declaration in the same TU). */
int __nupa_eh_isa(NPObject *obj, NPClass *cls);
/* Uncaught checked exception escaped main: print ObjC wording + abort().
 * Called by main's function-tail guard (desugar) instead of `return zero`. */
void nupa_eh_uncaught(void);

NPObject *nupa_retain(NPObject *obj);
void nupa_release(NPObject *obj);
NPObject *nupa_autorelease(NPObject *obj);
BOOL nupa_isKindOf(NPObject *obj, NPClass *cls);
/* Official ObjC spelling of nupa_isKindOf (same isa-chain walk). */
BOOL nupa_isKindOfClass(NPObject *obj, NPClass *cls);

// ─── Autorelease pool API ────────────────────────────────────────────────────────

typedef struct nupa_autoreleasepool nupa_autoreleasepool_t;
nupa_autoreleasepool_t *nupa_autoreleasepoolPush(void);
void nupa_autoreleasepoolPop(nupa_autoreleasepool_t *pool);

// ─── Async task API (route map item #4) ─────────────────────────────────────

/* A cooperative async task. The desugared async method is a state machine:
 * each `await` splits the body into a segment stored in one switch state.
 * The generated method body creates a task, pumps nupa_task_resume until the
 * state machine finishes (single-threaded cooperative scheduling: pumping
 * always makes progress), and reads the result from the frame. */
typedef struct NupaTask NupaTask;

/* State-machine entry written by the desugar pass.
 * Returns 0 = suspended at an await, 1 = finished. */
typedef int (*nupa_task_entry_fn)(NupaTask *task);

struct NupaTask {
    int state;                          /* current state-machine state */
    int finished;                       /* 0 = running/suspended, 1 = done */
    nupa_task_entry_fn entry;           /* state machine body */
    NPObject *self_obj;                 /* receiver the method runs on */
    void *frame;                        /* lifted locals (per-method struct) */
    void *result;                       /* return value slot (caller casts) */
    NupaTask *parent;                   /* awaiting task that created us, if any */
};

/* Create a task. `frame_size` is the size of the method's lifted-locals
 * struct; the frame is zero-initialized and owned by the task. */
NupaTask *nupa_task_create(nupa_task_entry_fn entry, NPObject *self_obj, size_t frame_size);

/* Run the state machine until it suspends (return 0) or finishes (return 1). */
int nupa_task_resume(NupaTask *task);

/* Pump the task to completion (cooperative single-thread scheduling) and
 * free it. Returns the task's result slot value. */
void *nupa_task_join(NupaTask *task);

/* Mark the task finished (called by the state machine's final state). */
void nupa_task_finish(NupaTask *task);

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
#ifndef __NUPA_FREESTANDING
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

void nupa_weakRegister(NPObject **weak_loc, NPObject *target);
void nupa_weakUnregister(NPObject **weak_loc);
void nupa_weakClearAll(NPObject *target);

// ─── String literals ──────────────────────────────────────────────────────────

NPObject *nupa_stringFromCstr(const char *cstr);
void nupa_weakAutoCleanup(void *ptr);

// ─── @synchronized monitors ──────────────────────────────────────────────────
//
// Per-object lock table keyed by object address (FNV-1a into a fixed bucket
// array). `nupa_syncLock` spins until the bucket's atomic flag is acquired and
// returns the bucket index to hand to `nupa_syncUnlock`; the generated code
// holds it in a `long` whose cleanup attribute releases the lock on every
// scope exit (return/break/continue included — `longjmp` past the scope is
// the one exit the attribute cannot see, so the checker warns on `@throw`
// escaping a `@synchronized` block). Buckets are global (not per-object
// allocations): correctness degrades to lock coarsening — two distinct
// objects hashing to the same bucket serialize — never to missed mutual
// exclusion. Freestanding builds keep the API (single-core: lock/unlock are
// no-ops) so transpiled code compiles unchanged.

long nupa_syncLock(void *object);
void nupa_syncUnlock(long bucket);
void nupa_syncAutoCleanup(void *ptr);   // cleanup attr: long* → unlock

// ─── helpers ──────────────────────────────────────────────────────────────────

#define NP_OBJECT_ISA(obj)        (((np_object_t *)(obj))->isa)
#define NP_CLASS_NAME(cls)        ((cls)->name)
#define NP_CLASS_SUPER(cls)       ((cls)->superclass)
#define NP_VTABLE_LOOKUP(obj, idx)  ((obj)->isa->vtable->methods[(idx)])

#endif /* NUPA_OBJECT_H */