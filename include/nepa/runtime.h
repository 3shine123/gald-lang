#ifndef NEPA_RUNTIME_H
#define NEPA_RUNTIME_H

/*
 * Nepa runtime header.
 *
 * Two modes:
 *   default               — host/OS mode: pulls in libc <setjmp.h>/<stdarg.h>,
 *                           exception state is thread-local (__thread).
 *   __NEPA_FREESTANDING   — bare-metal mode (set by `nepac -fno-libc`):
 *                           no libc headers; setjmp/longjmp map to Clang
 *                           builtins; exception state is plain globals
 *                           (single-core assumption). The user supplies
 *                           <stdint.h>/<stddef.h>/<stdbool.h> (freestanding).
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __NEPA_FREESTANDING
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
typedef struct nepa_root nepa_root;
typedef struct NPObject NPObject;
typedef struct NPString NPString;
typedef NPObject *id;
typedef NPObject *nepa_id_t;

/* Always declared: the transpiler's nepa_metaInit() references it.
 * Definition comes from the user (freestanding) or Foundation (host). */
extern NPClass NEPA_CLASS_$_nepa_root;

/* memcpy is used by the @try/@catch @finally jmp_buf save/restore
 * (the generated code always calls memcpy for nesting save/restore).
 * Guard against macOS's fortified memcpy macro. */
#ifndef memcpy
void *memcpy(void *dst, const void *src, size_t n);
#endif

#ifdef __NEPA_FREESTANDING

#ifndef NEPA_ROOT_DEFINED
#define NEPA_ROOT_DEFINED
struct nepa_root {
    struct NPClass *isa;
    uint32_t retain_count;
};
#endif

#endif /* end of __NEPA_FREESTANDING guarded structs */

#ifndef NPOBJECT_DEFINED
#define NPOBJECT_DEFINED
struct NPObject {
    struct NPClass *isa;
    uint32_t retain_count;
};
#endif

/* Key-value-coding accessor table for this class (NPPredicate support).
 * Emitted by codegen as the static array NEPA_KVC_$_<Class> when the TU can
 * see the NPPredicate declaration; the last entry's key is NULL (sentinel).
 * NULL on classes compiled without KVC emission — nepa_kvc_lookup skips
 * those levels and walks on to the superclass. Appended last: existing
 * designated-initializer metadata (`.field = ...`) leaves it NULL. */
/* Key-value-coding accessor table entry (NPPredicate support).
 * Emitted by codegen as the static array NEPA_KVC_$_<Class> when the TU can
 * see the NPPredicate declaration; the last entry's key is NULL (sentinel).
 * The getter wraps an ordinary static vtable dispatch and returns a boxed
 * result (scalars become NPNumber), so the engine needs no reflection. */
typedef struct nepa_kvc_entry {
    const char *key;            /* NULL marks the sentinel end */
    id (*get)(id self);
} nepa_kvc_entry;

struct NPClass {
    const char *name;
    NPClass *superclass;
    size_t instance_size;
    void *vtable;
    void *class_vtable;
    struct NPProtocol **protocols;
    int protocol_count;
    /* Populated by nepa_metaInit() (codegen). nepa_release() calls it when the
     * retain count reaches 0, so per-class dealloc cleanup (free-ing ivars)
     * actually runs. NULL if the class defines no instance dealloc. */
    void (*dealloc)(NPObject *, SEL);
    /* KVC: key → getter (id (*)(id)). Compiled-time table, no reflection:
     * a key lookup walks the isa chain comparing key strings, then calls
     * the getter — which is an ordinary static vtable dispatch inside. */
    const struct nepa_kvc_entry *kvc_entries;
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

/* Protocol conformance query (D5.2, doc/categories_protocol_plan.md): walks
   the class chain AND each class's protocol list (parents recursively),
   comparing selector-name coverage of `proto`'s required methods against
   the classes' vtables is NOT possible here (no selector → slot map), so
   conformance is metadata-based: proto identity/name match or required-method
   subset check against the class's protocol graph. NULL-safe: any argument
   NULL → 0. */
int nepa_class_conformsToProtocol(NPClass *cls, struct NPProtocol *proto);
/* Category TUs can add protocol conformances to the owning class metadata
 * after static initialization. The registration is idempotent. */
void nepa_register_category_protocols(NPClass *cls, struct NPProtocol **protocols, int count);

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

/* Class-metadata initialization. The transpiler emits `nepa_metaInit()` at the
 * top of every `main` and declares it in generated C; every other symbol in this
 * header is snake_case, so hand-written host code naturally reaches for
 * `nepa_meta_init()` and hits an implicit-declaration error. This alias accepts
 * that spelling. The target is defined by the generated C (weak, so exactly one
 * TU's copy wins the link); it is not declared here because a declaration would
 * force every TU — including ones with no classes — to define it. */
void nepa_meta_init(void);

/* Memory allocator (user-provided in freestanding; libc calloc/free on host).
 * nepa_malloc must zero-initialize memory. */
void *nepa_malloc(size_t size);
void  nepa_free(void *ptr);

NPObject *nepa_alloc(NPClass *cls);
NPObject *nepa_init(NPObject *self);

// Exception globals (TLS for thread safety; plain globals in freestanding)
#ifdef __NEPA_FREESTANDING
extern jmp_buf __nepa_exception_buf;
extern id     __nepa_exception_value;
#else
extern __thread jmp_buf __nepa_exception_buf;
extern __thread id     __nepa_exception_value;
#endif

// Checked-exception (-eh checked) error flag: 1 while an error propagates.
// Set by desugared `@throw`, cleared by the matching `@catch`; every nepa
// call site checks it and early-returns to propagate. Same TLS/freestanding
// split as the sjlj exception state above.
#ifdef __NEPA_FREESTANDING
extern int  __nepa_eh_flag;
extern id   __nepa_eh_val;
#else
extern __thread int  __nepa_eh_flag;
extern __thread id   __nepa_eh_val;
#endif
/* Isa check for typed catches: 1 when obj's isa chain matches cls.
 * NULL obj never matches. The desugar passes &NEPA_CLASS_$_<Flat> for the
 * catch type (codegen emits the extern declaration in the same TU). */
int __nepa_eh_isa(NPObject *obj, NPClass *cls);
/* Uncaught checked exception escaped main: print ObjC wording + abort().
 * Called by main's function-tail guard (desugar) instead of `return zero`. */
void nepa_eh_uncaught(void);

NPObject *nepa_retain(NPObject *obj);
void nepa_release(NPObject *obj);
NPObject *nepa_autorelease(NPObject *obj);
BOOL nepa_isKindOf(NPObject *obj, NPClass *cls);
/* Official ObjC spelling of nepa_isKindOf (same isa-chain walk). */
BOOL nepa_isKindOfClass(NPObject *obj, NPClass *cls);

/* KVC lookup: walk obj's isa chain, scan each class's kvc_entries table for
 * `key`, call the getter. NULL when no class in the chain declares the key
 * (the caller — valueForKey: / the predicate engine — reports the error).
 * Lives here so the NPPredicate engine (Foundation) and a direct valueForKey:
 * call share one implementation. */
id nepa_kvc_value(id obj, const char *key);

// ─── Autorelease pool API ────────────────────────────────────────────────────────

typedef struct nepa_autoreleasepool nepa_autoreleasepool_t;
nepa_autoreleasepool_t *nepa_autoreleasepoolPush(void);
void nepa_autoreleasepoolPop(nepa_autoreleasepool_t *pool);

// ─── Async task API (route map item #4) ─────────────────────────────────────

/* A cooperative async task. The desugared async method is a state machine:
 * each `await` splits the body into a segment stored in one switch state.
 * The generated method body creates a task, pumps nepa_task_resume until the
 * state machine finishes (single-threaded cooperative scheduling: pumping
 * always makes progress), and reads the result from the frame. */
typedef struct NPTask NPTask;

/* State-machine entry written by the desugar pass.
 * Returns 0 = suspended at an await, 1 = finished. */
typedef int (*nepa_task_entry_fn)(NPTask *task);

struct NPTask {
    int state;                          /* current state-machine state */
    int finished;                       /* 0 = running/suspended, 1 = done */
    int queued;                         /* 1 = linked in the ready queue
                                           (scheduling layer; distinct from
                                           `state` — legacy create uses
                                           state=1 for the entry's first
                                           segment) */
    nepa_task_entry_fn entry;           /* state machine body */
    NPObject *self_obj;                 /* receiver the method runs on */
    void *frame;                        /* lifted locals (per-method struct) */
    void *result;                       /* return value slot (caller casts) */
    NPTask *parent;                   /* awaiting task that created us, if any */
};

/* Create a task. `frame_size` is the size of the method's lifted-locals
 * struct; the frame is zero-initialized and owned by the task. */
NPTask *nepa_task_create(nepa_task_entry_fn entry, NPObject *self_obj, size_t frame_size);

/* Run the state machine until it suspends (return 0) or finishes (return 1). */
int nepa_task_resume(NPTask *task);

/* Pump the task to completion (cooperative single-thread scheduling) and
 * free it. Returns the task's result slot value. Legacy M1/M2 API — the
 * NPTask design (doc/async_nptask_plan.md) supersedes it with
 * start/await/mark_ready + nepa_sched_run; removed with the stage-D desugar
 * switch. */
void *nepa_task_join(NPTask *task);

/* Mark the task finished (called by the state machine's final state). */
void nepa_task_finish(NPTask *task);

// ─── NPTask scheduling layer (doc/async_nptask_plan.md, stage C) ────────────
/* Lazy tasks + a cooperative ready queue. Mechanism only: scheduling POLICY
 * (when to pump, threading, priorities) belongs to the caller / a future
 * runtime library. Zero libc dependencies — identical shape in
 * runtime_freestanding.c, fully usable on bare metal. */

/* Allocator injection (C++ promise_type custom operator new precedent):
 * defaults to malloc/free on the host and nepa_malloc/nepa_free in
 * freestanding. NULL arguments fall back to the defaults. Must be called
 * before the first task is created. */
typedef void *(*nepa_task_alloc_fn)(size_t);
typedef void (*nepa_task_free_fn)(void *);
void nepa_task_set_allocator(nepa_task_alloc_fn alloc, nepa_task_free_fn free_fn);

/* READY → enqueue. Idempotent: starting a started/finished task is a no-op. */
int nepa_task_start(NPTask *task);

/* Suspend the caller until `task` finishes; returns its (cached) result.
 * Auto-starts a not-yet-started task. Safe to call repeatedly — the result
 * is cached in the task state (promise/future-box semantics). A task
 * awaiting itself is fatal. When the caller is not itself a task (top-level
 * synchronous context), this pumps the queue until the task completes
 * (blocking-join shape). */
void *nepa_task_await(NPTask *task);

/* Event-source hook: re-enqueue a suspended task (interrupt / DMA callback /
 * I/O completion on the host). No-op for tasks that are finished or already
 * queued. */
void nepa_task_mark_ready(NPTask *task);

/* Pump: run every ready task until it suspends or finishes, until the queue
 * drains, then return. The bare-metal drive pattern is a main loop of
 * `nepa_sched_run()` + a WFI / event wait — the pump is never invoked
 * implicitly in freestanding mode. */
void nepa_sched_run(void);

/* The task currently executing inside nepa_sched_run (NULL at top level).
 * Used by nepa_task_await for self-await detection and by stage-D desugar
 * for in-task suspension. */
NPTask *nepa_task_current(void);

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
#ifndef __NEPA_FREESTANDING
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

void nepa_weakRegister(NPObject **weak_loc, NPObject *target);
void nepa_weakUnregister(NPObject **weak_loc);
void nepa_weakClearAll(NPObject *target);

// ─── String literals ──────────────────────────────────────────────────────────

NPObject *nepa_stringFromCstr(const char *cstr);
void nepa_weakAutoCleanup(void *ptr);

// ─── @synchronized monitors ──────────────────────────────────────────────────
//
// Per-object lock table keyed by object address (FNV-1a into a fixed bucket
// array). `nepa_syncLock` spins until the bucket's atomic flag is acquired and
// returns the bucket index to hand to `nepa_syncUnlock`; the generated code
// holds it in a `long` whose cleanup attribute releases the lock on every
// scope exit (return/break/continue included — `longjmp` past the scope is
// the one exit the attribute cannot see, so the checker warns on `@throw`
// escaping a `@synchronized` block). Buckets are global (not per-object
// allocations): correctness degrades to lock coarsening — two distinct
// objects hashing to the same bucket serialize — never to missed mutual
// exclusion. Freestanding builds keep the API (single-core: lock/unlock are
// no-ops) so transpiled code compiles unchanged.

long nepa_syncLock(void *object);
void nepa_syncUnlock(long bucket);
void nepa_syncAutoCleanup(void *ptr);   // cleanup attr: long* → unlock

// ─── helpers ──────────────────────────────────────────────────────────────────

#define NP_OBJECT_ISA(obj)        (((np_object_t *)(obj))->isa)
#define NP_CLASS_NAME(cls)        ((cls)->name)
#define NP_CLASS_SUPER(cls)       ((cls)->superclass)
#define NP_VTABLE_LOOKUP(obj, idx)  ((obj)->isa->vtable->methods[(idx)])

#endif /* NEPA_OBJECT_H */
