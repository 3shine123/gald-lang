// ovic/runtime_freestanding.c — Freestanding runtime for bare-metal Ovic.
//
// Provides EVERYTHING the transpiled code needs on bare metal.
// Just link this file alongside the transpiled Ovic code — no hand-written
// globals, no runtime boilerplate.  The user only needs to provide the
// freestanding header set (stdint.h, stddef.h, stdbool.h).  memcpy and
// memset have weak fallbacks below (the compiler may emit calls to them
// under -ffreestanding; a kernel's own strong definitions win the link).
//
// NOT bundled here: the Clang Blocks runtime.  Block literals reference
// __NSConcreteStackBlock and _Block_copy/_Block_release — on real bare metal,
// either port a Blocks runtime or use `-backend portable|gcc` (blocks expand
// to plain C functions with no ABI symbols).
//
// Compile with -D__OVIC_FREESTANDING -I<ovicc>/include.
// On the host (for testing), compile with -U__OVIC_FREESTANDING
// (uses system setjmp/longjmp, __thread globals).

#include <ovic/runtime.h>
#include <stddef.h>
#include <stdint.h>

// ─── Runtime globals (referenced by transpiled code) ─────────────────────────

NPClass OVIC_CLASS_$_ovic_root;

#ifdef __OVIC_FREESTANDING
jmp_buf __ovic_exception_buf;
id      __ovic_exception_value;
#else
__thread jmp_buf __ovic_exception_buf;
__thread id      __ovic_exception_value;
#endif

// ─── memcpy (used by @try/@catch jmp_buf save/restore) ──────────────────────

#undef memcpy
__attribute__((weak)) void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

// ─── memset (used by ovic_alloc zeroing; the compiler may emit it too) ───────

#undef memset
__attribute__((weak)) void *memset(void *s, int c, size_t n) {
    unsigned char *d = s;
    while (n--) *d++ = (unsigned char)c;
    return s;
}

// ─── Bump allocator ──────────────────────────────────────────────────────────

#ifndef OVIC_HEAP_SIZE
#define OVIC_HEAP_SIZE 16384
#endif

/* max_align_t (C11, <stddef.h>) — the heap and every allocation must satisfy
 * the strictest fundamental alignment (pointers, double, long double on some
 * targets); a plain char array only guarantees alignment 1. */
static _Alignas(max_align_t) unsigned char ovic_heap[OVIC_HEAP_SIZE];
static size_t ovic_heap_off = 0;

void *ovic_malloc(size_t size) {
    size_t align = _Alignof(max_align_t);
    size = (size + align - 1) & ~(align - 1);
    if (ovic_heap_off + size > OVIC_HEAP_SIZE) return NULL;
    void *p = &ovic_heap[ovic_heap_off];
    ovic_heap_off += size;
    memset(p, 0, size);   /* contract (runtime.h): ovic_malloc returns zeroed memory */
    return p;
}

void ovic_free(void *ptr) {
    (void)ptr; /* bump allocator: never reuses memory */
}

void ovic_register_category_protocols(NPClass *cls, struct NPProtocol **protocols, int count) {
    if (!cls || !protocols || count <= 0) return;
    int old_count = cls->protocol_count;
    int add = 0;
    for (int i = 0; i < count; i++) {
        int duplicate = 0;
        for (int j = 0; j < old_count; j++) {
            if (cls->protocols && cls->protocols[j] == protocols[i]) { duplicate = 1; break; }
        }
        if (!duplicate) add++;
    }
    if (!add) return;
    struct NPProtocol **merged = ovic_malloc((size_t)(old_count + add) * sizeof(*merged));
    if (!merged) __builtin_trap();
    for (int i = 0; i < old_count; i++) merged[i] = cls->protocols[i];
    int n = old_count;
    for (int i = 0; i < count; i++) {
        int duplicate = 0;
        for (int j = 0; j < n; j++) if (merged[j] == protocols[i]) { duplicate = 1; break; }
        if (!duplicate) merged[n++] = protocols[i];
    }
    cls->protocols = merged;
    cls->protocol_count = n;
}

// ─── Autorelease pool ────────────────────────────────────────────────────────
// Bare-metal: single-core, no __thread.

struct ovic_autoreleasepool {
    struct ovic_autoreleasepool *next;
    NPObject **objects;
    int count;
    int capacity;
};

static ovic_autoreleasepool_t *current_pool = NULL;

ovic_autoreleasepool_t *ovic_autoreleasepoolPush(void) {
    ovic_autoreleasepool_t *pool = ovic_malloc(sizeof(ovic_autoreleasepool_t));
    if (!pool) return NULL;
    pool->next = current_pool;
    pool->objects = NULL;
    pool->count = 0;
    pool->capacity = 0;
    current_pool = pool;
    return pool;
}

void ovic_autoreleasepoolPop(ovic_autoreleasepool_t *pool) {
    if (!pool) return;
    for (int i = 0; i < pool->count; i++) {
        ovic_release(pool->objects[i]);
    }
    ovic_free(pool->objects);
    current_pool = pool->next;
    ovic_free(pool);
}

// ─── Type introspection (parity with hosted runtime.c) ──────────────────────
// Same isa-chain walk as the hosted runtime: walk obj->isa up the superclass
// chain looking for cls. Ten lines of pure C — no allocation, no libc.

BOOL ovic_isKindOf(NPObject *obj, NPClass *cls) {
    if (!obj || !cls) return 0;
    NPClass *isa = obj->isa;
    while (isa) {
        if (isa == cls) return 1;
        isa = isa->superclass;
    }
    return 0;
}

/* Official ObjC spelling of isKindOf: (kept as a compatible alias).
 * Same isa-chain walk. */
BOOL ovic_isKindOfClass(NPObject *obj, NPClass *cls) {
    return ovic_isKindOf(obj, cls);
}

// ─── Checked-exception backend (-eh checked) ────────────────────────────────
// Flag/val follow the same dual-mode pattern as the exception globals above:
// __thread on the host (matches runtime.h declarations), plain globals in
// freestanding (single core).
#ifdef __OVIC_FREESTANDING
int  __ovic_eh_flag;
id   __ovic_eh_val;
#else
__thread int  __ovic_eh_flag;
__thread id   __ovic_eh_val;
#endif

void ovic_console_write(const char *s, unsigned len);
static const char *ovic_safe_isa_name(id obj);

int __ovic_eh_isa(NPObject *obj, NPClass *cls) {
    if (!obj || !cls) return 0;
    return ovic_isKindOf(obj, cls) ? 1 : 0;
}

void ovic_eh_uncaught(void) {
    const char *cls = ovic_safe_isa_name(__ovic_eh_val);
    ovic_console_write("*** Terminating app due to uncaught exception of class '", 0);
    ovic_console_write(cls, 0);
    ovic_console_write("'\n", 0);
    __builtin_trap();
}

// ─── Async task API (same shape as hosted runtime.c) ────────────────────────
// Cooperative single-thread state machine pump; identical logic to the host,
// with ovic_malloc/ovic_free instead of calloc/free. No new overhead: a task
// costs one struct + its frame, same as hosted.

/* Allocator injection: NULL = ovic_malloc/ovic_free defaults (bump
 * allocator); ovic_task_set_allocator swaps in a static pool on bare metal. */
static ovic_task_alloc_fn task_alloc = NULL;
static ovic_task_free_fn  task_free_fn = NULL;

static void ensure_default_allocator(void) {
    if (!task_alloc) task_alloc = ovic_malloc;
    if (!task_free_fn) task_free_fn = ovic_free;
}

void ovic_task_set_allocator(ovic_task_alloc_fn alloc, ovic_task_free_fn free_fn) {
    task_alloc = alloc;
    task_free_fn = free_fn;
}

NPTask *ovic_task_create(ovic_task_entry_fn entry, NPObject *self_obj, size_t frame_size) {
    ensure_default_allocator();
    NPTask *t = (NPTask *)task_alloc(sizeof(NPTask));
    if (!t) return NULL;
    t->state = 1;   /* state 1 = the entry's first case; 0 means "not started" */
    t->finished = 0;
    t->entry = entry;
    t->self_obj = self_obj;
    t->frame = frame_size ? task_alloc(frame_size) : NULL;
    t->result = NULL;
    t->parent = NULL;
    return t;
}

int ovic_task_resume(NPTask *task) {
    if (!task || task->finished) return 1;
    if (task->entry) {
        if (task->entry(task) != 0) {
            task->finished = 1;
        }
    } else {
        task->finished = 1;
    }
    return task->finished ? 1 : 0;
}

void ovic_task_finish(NPTask *task) {
    if (task) task->finished = 1;
}

void *ovic_task_join(NPTask *task) {
    if (!task) return NULL;
    while (!task->finished) {
        (void)ovic_task_resume(task);
    }
    void *result = task->result;
    /* bump allocator: ovic_free is a no-op, kept for API parity */
    ovic_free(task->frame);
    ovic_free(task);
    return result;
}

// ─── NPTask scheduling layer (doc/async_nptask_plan.md, stage C) ────────────
// Same shape as the hosted runtime.c. Zero libc dependencies: the allocator
// injection above defaults to ovic_malloc/ovic_free (bump allocator), so the
// pump works on bare metal — the main loop owns it:
// `while (1) { ovic_sched_run(); __WFI(); }`

enum { TASK_READY = 0, TASK_QUEUED = 1, TASK_DONE = 2 };

static NPTask *sched_head = NULL;
static NPTask *sched_tail = NULL;
static NPTask *sched_current = NULL;

NPTask *ovic_task_current(void) { return sched_current; }

static void sched_enqueue(NPTask *t) {
    t->parent = NULL;          /* `parent` reused as the queue link */
    if (sched_tail) sched_tail->parent = t;
    else sched_head = t;
    sched_tail = t;
    t->queued = 1;
}

int ovic_task_start(NPTask *task) {
    if (!task || task->finished) return task != NULL;
    if (task->queued) return 1;                 /* idempotent */
    sched_enqueue(task);
    return 1;
}

void ovic_task_mark_ready(NPTask *task) {
    (void)ovic_task_start(task);
}

void ovic_sched_run(void) {
    while (sched_head) {
        NPTask *t = sched_head;
        sched_head = t->parent;
        if (!sched_head) sched_tail = NULL;
        t->queued = 0;
        if (t->finished) continue;
        sched_current = t;
        (void)ovic_task_resume(t);
        sched_current = NULL;
        /* finished tasks are dropped (frame/task freed by stage-D desugar at
         * the join point; legacy join still owns its own lifetime) */
    }
}

void *ovic_task_await(NPTask *task) {
    if (!task) return NULL;
    if (task == sched_current) {
        /* self-await cycle: fatal (single thread, no one to make progress) */
        ovic_console_write("fatal: task awaits itself\n", 0);
        __builtin_trap();
    }
    (void)ovic_task_start(task);
    /* Blocking drive, top-level or in-task alike (M2 synchronous-drive
     * model): an in-task await on an independent nested task drives it
     * inline; direct self-await is checked above. */
    while (!task->finished) {
        if (sched_head) { ovic_sched_run(); continue; }
        (void)ovic_task_resume(task);
    }
    return task->result;
}

// ─── Weak reference side table (fixed-size, no allocation) ──────────────────
// Hosted runtime uses a malloc'd slot array; bare metal uses a static table
// with a fixed number of slots per target. Zero heap overhead, linear scan.
// 64 targets × 8 slots each = 4 KiB static data — tune via OVIC_WEAK_MAX.

#ifndef OVIC_WEAK_MAX_TARGETS
#define OVIC_WEAK_MAX_TARGETS 64
#endif
#define OVIC_WEAK_SLOTS_PER_TARGET 8

typedef struct {
    NPObject *object;                 /* NULL = free entry */
    NPObject **slots[OVIC_WEAK_SLOTS_PER_TARGET];
} WeakEntry;

static WeakEntry weak_table[OVIC_WEAK_MAX_TARGETS];

static WeakEntry *find_weak_entry(NPObject *target) {
    for (int i = 0; i < OVIC_WEAK_MAX_TARGETS; i++) {
        if (weak_table[i].object == target)
            return &weak_table[i];
    }
    return NULL;
}

void ovic_weakRegister(NPObject **weak_loc, NPObject *target) {
    if (!target || !weak_loc) return;
    WeakEntry *entry = find_weak_entry(target);
    if (!entry) {
        /* first free entry; full table → silently drop (bare-metal policy) */
        for (int i = 0; i < OVIC_WEAK_MAX_TARGETS; i++) {
            if (!weak_table[i].object) { entry = &weak_table[i]; break; }
        }
        if (!entry) return;
        entry->object = target;
        for (int j = 0; j < OVIC_WEAK_SLOTS_PER_TARGET; j++) entry->slots[j] = NULL;
    }
    for (int j = 0; j < OVIC_WEAK_SLOTS_PER_TARGET; j++) {
        if (!entry->slots[j]) { entry->slots[j] = weak_loc; return; }
    }
}

void ovic_weakUnregister(NPObject **weak_loc) {
    if (!weak_loc) return;
    for (int i = 0; i < OVIC_WEAK_MAX_TARGETS; i++) {
        WeakEntry *entry = &weak_table[i];
        for (int j = 0; j < OVIC_WEAK_SLOTS_PER_TARGET; j++) {
            if (entry->slots[j] == weak_loc) {
                entry->slots[j] = NULL;
                /* last slot gone → release the entry so its target slot is
                 * reclaimable; otherwise every once-weak target pins its
                 * entry forever and the 64-target table silently saturates */
                int empty = 1;
                for (int k = 0; k < OVIC_WEAK_SLOTS_PER_TARGET; k++) {
                    if (entry->slots[k]) { empty = 0; break; }
                }
                if (empty && entry->object) entry->object = NULL;
                return;
            }
        }
    }
}

void ovic_weakClearAll(NPObject *target) {
    if (!target) return;
    WeakEntry *entry = find_weak_entry(target);
    if (!entry) return;
    for (int j = 0; j < OVIC_WEAK_SLOTS_PER_TARGET; j++) {
        if (entry->slots[j]) *entry->slots[j] = NULL;
        entry->slots[j] = NULL;
    }
    entry->object = NULL;
}

void ovic_weakAutoCleanup(void *ptr) {
    ovic_weakUnregister((NPObject **)ptr);
}

// ─── @synchronized monitors ──────────────────────────────────────────────────
// Bare metal is single-core: mutual exclusion is trivially satisfied, so the
// monitor API is a no-op pair that keeps the same signatures as the hosted
// runtime — transpiled code compiles unchanged on both runtimes. (If SMP
// arrives, replace with a spinlock over the same bucket scheme; interrupt-
// based concurrency would additionally need irq-disable around the lock.)

long ovic_syncLock(void *object) {
    (void)object;
    return 0;
}

void ovic_syncUnlock(long bucket) {
    (void)bucket;
}

void ovic_syncAutoCleanup(void *ptr) {
    (void)ptr;
}

// ─── Console hook (exception diagnostics only) ──────────────────────────────
// Bare metal has no stdout/stderr: I/O is NOT a language mechanism, so the
// runtime provides no printf/NPLog. The only output is the uncaught-
// exception report from ovic_eh_uncaught(), routed through this hook.
// Users override it with their console driver (UART/kputs-style); the weak
// no-op default links cleanly and the program simply traps silently.
//   void ovic_console_write(const char *s, unsigned len);
// len == 0 means NUL-terminated.

__attribute__((weak)) void ovic_console_write(const char *s, unsigned len) {
    (void)s; (void)len; /* no console: discard */
}

/* 1 when p came from the bump allocator (a real object), 0 for raw
 * pointers (C string literals cast to id). Zero cost: two compares. */
static int ovic_is_heap_object(const void *p) {
    return (const unsigned char *)p >= ovic_heap
        && (const unsigned char *)p < ovic_heap + OVIC_HEAP_SIZE;
}

/* Safe class name for ovic_eh_uncaught: raw literals have no isa. */
static const char *ovic_safe_isa_name(id obj) {
    if (obj && ovic_is_heap_object(obj) && obj->isa && obj->isa->name)
        return obj->isa->name;
    return "?";
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

NPObject *ovic_alloc(NPClass *cls) {
    if (!cls) return NULL;
    NPObject *obj = (NPObject *)ovic_malloc(cls->instance_size);
    if (obj) {
        memset(obj, 0, cls->instance_size);
        obj->isa = cls;
        obj->retain_count = 1;
    }
    return obj;
}

NPObject *ovic_init(NPObject *self) {
    return self;
}

NPObject *ovic_retain(NPObject *obj) {
    /* Raw pointers (@"..." literals without NPString, C strings cast to id)
     * live outside the bump heap: writing retain_count there faults. No-op. */
    if (!obj || !ovic_is_heap_object(obj)) return obj;
    obj->retain_count++;
    return obj;
}

void ovic_release(NPObject *obj) {
    if (!obj || !ovic_is_heap_object(obj)) return;
    if (obj->retain_count > 0)
        obj->retain_count--;
    if (obj->retain_count == 0) {
        ovic_weakClearAll(obj);
        // Call dealloc so ivar cleanup runs (e.g. NPString frees _cstr).
        // dealloc's own `[super dealloc]` calls the parent's dealloc directly
        // (not ovic_release), so no double-free.
        if (obj->isa && obj->isa->dealloc) {
            obj->isa->dealloc(obj, (SEL){ .name = "dealloc", .hash = 0xD9929EB3 });
        }
        ovic_free(obj);
    }
}

NPObject *ovic_autorelease(NPObject *obj) {
    if (!obj || !ovic_is_heap_object(obj)) return obj;
    ovic_autoreleasepool_t *pool = current_pool;
    if (!pool) return obj;
    if (pool->count >= pool->capacity) {
        int new_cap = pool->capacity ? pool->capacity * 2 : 16;
        NPObject **new_objs = ovic_malloc(new_cap * sizeof(NPObject *));
        if (!new_objs) return obj;
        if (pool->objects) {
            for (int i = 0; i < pool->count; i++) new_objs[i] = pool->objects[i];
        }
        pool->objects = new_objs;
        pool->capacity = new_cap;
    }
    pool->objects[pool->count++] = obj;
    return obj;
}
