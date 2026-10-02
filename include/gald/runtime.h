#ifndef GALD_RUNTIME_H
#define GALD_RUNTIME_H

/*
 * Gald runtime header.
 *
 * Two modes:
 *   default               — host/OS mode: pulls in libc <setjmp.h>/<stdarg.h>,
 *                           exception state is thread-local (__thread).
 *   __GALD_FREESTANDING   — bare-metal mode (set by `galdc -fno-libc`):
 *                           no libc headers; setjmp/longjmp map to Clang
 *                           builtins; exception state is plain globals
 *                           (single-core assumption). The user supplies
 *                           <stdint.h>/<stddef.h>/<stdbool.h> (freestanding).
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __GALD_FREESTANDING
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

typedef struct NFClass NFClass;
typedef struct gald_root gald_root;
typedef struct NFObject NFObject;
typedef struct NFString NFString;
typedef NFObject *id;
typedef NFObject *gald_id_t;

/* Always declared: the transpiler's gald_metaInit() references it.
 * Definition comes from the user (freestanding) or Foundation (host). */
extern NFClass GALD_CLASS_$_gald_root;

/* memcpy is used by the @try/@catch @finally jmp_buf save/restore
 * (the generated code always calls memcpy for nesting save/restore).
 * Guard against macOS's fortified memcpy macro. */
#ifndef memcpy
void *memcpy(void *dst, const void *src, size_t n);
#endif

#ifdef __GALD_FREESTANDING

#ifndef GALD_ROOT_DEFINED
#define GALD_ROOT_DEFINED
struct gald_root {
    struct NFClass *isa;
    uint32_t retain_count;
};
#endif

#endif /* end of __GALD_FREESTANDING guarded structs */

#ifndef NFOBJECT_DEFINED
#define NFOBJECT_DEFINED
struct NFObject {
    struct NFClass *isa;
    uint32_t retain_count;
};
#endif

struct NFClass {
    const char *name;
    NFClass *superclass;
    size_t instance_size;
    void *vtable;
    void *class_vtable;
    struct NFProtocol **protocols;
    int protocol_count;
    /* Populated by gald_metaInit() (codegen). gald_release() calls it when the
     * retain count reaches 0, so per-class dealloc cleanup (free-ing ivars)
     * actually runs. NULL if the class defines no instance dealloc. */
    void (*dealloc)(NFObject *, SEL);
};

// ─── Protocol types ──────────────────────────────────────────────────────────

typedef struct NFProtocolMethod {
    const char *name;
    const char *encoding;
} NFProtocolMethod;

struct NFProtocol {
    const char *name;
    struct NFProtocol **parents;
    int parent_count;
    NFProtocolMethod *required_methods;
    int required_count;
    NFProtocolMethod *optional_methods;
    int optional_count;
};

// ─── Internal types (for runtime implementation) ────────────────────────────────

typedef struct nf_vtable nf_vtable_t;
typedef struct nf_class  nf_class_t;
typedef struct nf_object nf_object_t;

struct nf_vtable {
    nf_class_t *isa;
    void      (**methods)(void);
    int        method_count;
};

struct nf_class {
    nf_class_t  *superclass;
    const char  *name;
    size_t       instance_size;
    nf_vtable_t *vtable;
    void       (*constructor)(nf_object_t *self, ...);
};

struct nf_object {
    nf_class_t *isa;
};

// ─── Runtime API ────────────────────────────────────────────────────────────────

SEL sel_registerName(const char *name);

/* Class-metadata initialization. The transpiler emits `gald_metaInit()` at the
 * top of every `main` and declares it in generated C; every other symbol in this
 * header is snake_case, so hand-written host code naturally reaches for
 * `gald_meta_init()` and hits an implicit-declaration error. This alias accepts
 * that spelling. The target is defined by the generated C (weak, so exactly one
 * TU's copy wins the link); it is not declared here because a declaration would
 * force every TU — including ones with no classes — to define it. */
void gald_meta_init(void);

/* Memory allocator (user-provided in freestanding; libc calloc/free on host).
 * gald_malloc must zero-initialize memory. */
void *gald_malloc(size_t size);
void  gald_free(void *ptr);

NFObject *gald_alloc(NFClass *cls);
NFObject *gald_init(NFObject *self);

// Exception globals (TLS for thread safety; plain globals in freestanding)
#ifdef __GALD_FREESTANDING
extern jmp_buf __gald_exception_buf;
extern id     __gald_exception_value;
#else
extern __thread jmp_buf __gald_exception_buf;
extern __thread id     __gald_exception_value;
#endif

// Checked-exception (-eh checked) error flag: 1 while an error propagates.
// Set by desugared `@throw`, cleared by the matching `@catch`; every gald
// call site checks it and early-returns to propagate. Same TLS/freestanding
// split as the sjlj exception state above.
#ifdef __GALD_FREESTANDING
extern int  __gald_eh_flag;
extern id   __gald_eh_val;
#else
extern __thread int  __gald_eh_flag;
extern __thread id   __gald_eh_val;
#endif
/* Isa check for typed catches: 1 when obj's isa chain matches cls.
 * NULL obj never matches. The desugar passes &GALD_CLASS_$_<Flat> for the
 * catch type (codegen emits the extern declaration in the same TU). */
int __gald_eh_isa(NFObject *obj, NFClass *cls);
/* Uncaught checked exception escaped main: print ObjC wording + abort().
 * Called by main's function-tail guard (desugar) instead of `return zero`. */
void gald_eh_uncaught(void);

NFObject *gald_retain(NFObject *obj);
void gald_release(NFObject *obj);
NFObject *gald_autorelease(NFObject *obj);
BOOL gald_isKindOf(NFObject *obj, NFClass *cls);
/* Official ObjC spelling of gald_isKindOf (same isa-chain walk). */
BOOL gald_isKindOfClass(NFObject *obj, NFClass *cls);

// ─── Autorelease pool API ────────────────────────────────────────────────────────

typedef struct gald_autoreleasepool gald_autoreleasepool_t;
gald_autoreleasepool_t *gald_autoreleasepoolPush(void);
void gald_autoreleasepoolPop(gald_autoreleasepool_t *pool);

// ─── Async task API (route map item #4) ─────────────────────────────────────

/* A cooperative async task. The desugared async method is a state machine:
 * each `await` splits the body into a segment stored in one switch state.
 * The generated method body creates a task, pumps gald_task_resume until the
 * state machine finishes (single-threaded cooperative scheduling: pumping
 * always makes progress), and reads the result from the frame. */
typedef struct NFTask NFTask;

/* State-machine entry written by the desugar pass.
 * Returns 0 = suspended at an await, 1 = finished. */
typedef int (*gald_task_entry_fn)(NFTask *task);

struct NFTask {
    int state;                          /* current state-machine state */
    int finished;                       /* 0 = running/suspended, 1 = done */
    gald_task_entry_fn entry;           /* state machine body */
    NFObject *self_obj;                 /* receiver the method runs on */
    void *frame;                        /* lifted locals (per-method struct) */
    void *result;                       /* return value slot (caller casts) */
    NFTask *parent;                   /* awaiting task that created us, if any */
};

/* Create a task. `frame_size` is the size of the method's lifted-locals
 * struct; the frame is zero-initialized and owned by the task. */
NFTask *gald_task_create(gald_task_entry_fn entry, NFObject *self_obj, size_t frame_size);

/* Run the state machine until it suspends (return 0) or finishes (return 1). */
int gald_task_resume(NFTask *task);

/* Pump the task to completion (cooperative single-thread scheduling) and
 * free it. Returns the task's result slot value. */
void *gald_task_join(NFTask *task);

/* Mark the task finished (called by the state machine's final state). */
void gald_task_finish(NFTask *task);

// ─── Internal API (for runtime implementation) ───────────────────────────────────

void nf_class_register(nf_class_t *cls);
nf_class_t *nf_class_create(const char *name, nf_class_t *superclass, size_t instance_size);
void nf_class_set_vtable(nf_class_t *cls, nf_vtable_t *vtable);
nf_vtable_t *nf_vtable_alloc(int method_count);
void nf_vtable_set_method(nf_vtable_t *vt, int index, void (*method)(void));
nf_object_t *nf_object_alloc(nf_class_t *cls);
void nf_object_dealloc(nf_object_t *obj);

// ─── Logging (like NSLog / NSObjCRuntime.h) ───────────────────────────────────

void NFLog(NFString *format, ...);
#ifndef __GALD_FREESTANDING
void __NFLogv(NFString *format, va_list args);
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

void gald_weakRegister(NFObject **weak_loc, NFObject *target);
void gald_weakUnregister(NFObject **weak_loc);
void gald_weakClearAll(NFObject *target);

// ─── String literals ──────────────────────────────────────────────────────────

NFObject *gald_stringFromCstr(const char *cstr);
void gald_weakAutoCleanup(void *ptr);

// ─── @synchronized monitors ──────────────────────────────────────────────────
//
// Per-object lock table keyed by object address (FNV-1a into a fixed bucket
// array). `gald_syncLock` spins until the bucket's atomic flag is acquired and
// returns the bucket index to hand to `gald_syncUnlock`; the generated code
// holds it in a `long` whose cleanup attribute releases the lock on every
// scope exit (return/break/continue included — `longjmp` past the scope is
// the one exit the attribute cannot see, so the checker warns on `@throw`
// escaping a `@synchronized` block). Buckets are global (not per-object
// allocations): correctness degrades to lock coarsening — two distinct
// objects hashing to the same bucket serialize — never to missed mutual
// exclusion. Freestanding builds keep the API (single-core: lock/unlock are
// no-ops) so transpiled code compiles unchanged.

long gald_syncLock(void *object);
void gald_syncUnlock(long bucket);
void gald_syncAutoCleanup(void *ptr);   // cleanup attr: long* → unlock

// ─── helpers ──────────────────────────────────────────────────────────────────

#define NF_OBJECT_ISA(obj)        (((nf_object_t *)(obj))->isa)
#define NF_CLASS_NAME(cls)        ((cls)->name)
#define NF_CLASS_SUPER(cls)       ((cls)->superclass)
#define NF_VTABLE_LOOKUP(obj, idx)  ((obj)->isa->vtable->methods[(idx)])

#endif /* GALD_OBJECT_H */