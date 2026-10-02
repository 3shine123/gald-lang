// gald/runtime_freestanding.c — Freestanding runtime for bare-metal Gald.
//
// Provides EVERYTHING the transpiled code needs on bare metal.
// Just link this file alongside the transpiled Gald code — no hand-written
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
// Compile with -D__GALD_FREESTANDING -I<galdc>/include.
// On the host (for testing), compile with -U__GALD_FREESTANDING
// (uses system setjmp/longjmp, __thread globals).

#include <gald/runtime.h>
#include <stddef.h>
#include <stdint.h>

// ─── Runtime globals (referenced by transpiled code) ─────────────────────────

NFClass GALD_CLASS_$_gald_root;

#ifdef __GALD_FREESTANDING
jmp_buf __gald_exception_buf;
id      __gald_exception_value;
#else
__thread jmp_buf __gald_exception_buf;
__thread id      __gald_exception_value;
#endif

// ─── memcpy (used by @try/@catch jmp_buf save/restore) ──────────────────────

#undef memcpy
__attribute__((weak)) void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

// ─── memset (used by gald_alloc zeroing; the compiler may emit it too) ───────

#undef memset
__attribute__((weak)) void *memset(void *s, int c, size_t n) {
    unsigned char *d = s;
    while (n--) *d++ = (unsigned char)c;
    return s;
}

// ─── Bump allocator ──────────────────────────────────────────────────────────

#ifndef GALD_HEAP_SIZE
#define GALD_HEAP_SIZE 16384
#endif

/* max_align_t (C11, <stddef.h>) — the heap and every allocation must satisfy
 * the strictest fundamental alignment (pointers, double, long double on some
 * targets); a plain char array only guarantees alignment 1. */
static _Alignas(max_align_t) unsigned char gald_heap[GALD_HEAP_SIZE];
static size_t gald_heap_off = 0;

void *gald_malloc(size_t size) {
    size_t align = _Alignof(max_align_t);
    size = (size + align - 1) & ~(align - 1);
    if (gald_heap_off + size > GALD_HEAP_SIZE) return NULL;
    void *p = &gald_heap[gald_heap_off];
    gald_heap_off += size;
    memset(p, 0, size);   /* contract (runtime.h): gald_malloc returns zeroed memory */
    return p;
}

void gald_free(void *ptr) {
    (void)ptr; /* bump allocator: never reuses memory */
}

// ─── Autorelease pool ────────────────────────────────────────────────────────
// Bare-metal: single-core, no __thread.

struct gald_autoreleasepool {
    struct gald_autoreleasepool *next;
    NFObject **objects;
    int count;
    int capacity;
};

static gald_autoreleasepool_t *current_pool = NULL;

gald_autoreleasepool_t *gald_autoreleasepoolPush(void) {
    gald_autoreleasepool_t *pool = gald_malloc(sizeof(gald_autoreleasepool_t));
    if (!pool) return NULL;
    pool->next = current_pool;
    pool->objects = NULL;
    pool->count = 0;
    pool->capacity = 0;
    current_pool = pool;
    return pool;
}

void gald_autoreleasepoolPop(gald_autoreleasepool_t *pool) {
    if (!pool) return;
    for (int i = 0; i < pool->count; i++) {
        gald_release(pool->objects[i]);
    }
    gald_free(pool->objects);
    current_pool = pool->next;
    gald_free(pool);
}

// ─── Type introspection (parity with hosted runtime.c) ──────────────────────
// Same isa-chain walk as the hosted runtime: walk obj->isa up the superclass
// chain looking for cls. Ten lines of pure C — no allocation, no libc.

BOOL gald_isKindOf(NFObject *obj, NFClass *cls) {
    if (!obj || !cls) return 0;
    NFClass *isa = obj->isa;
    while (isa) {
        if (isa == cls) return 1;
        isa = isa->superclass;
    }
    return 0;
}

/* Official ObjC spelling of isKindOf: (kept as a compatible alias).
 * Same isa-chain walk. */
BOOL gald_isKindOfClass(NFObject *obj, NFClass *cls) {
    return gald_isKindOf(obj, cls);
}

// ─── Checked-exception backend (-eh checked) ────────────────────────────────
// Flag/val follow the same dual-mode pattern as the exception globals above:
// __thread on the host (matches runtime.h declarations), plain globals in
// freestanding (single core).
#ifdef __GALD_FREESTANDING
int  __gald_eh_flag;
id   __gald_eh_val;
#else
__thread int  __gald_eh_flag;
__thread id   __gald_eh_val;
#endif

void gald_console_write(const char *s, unsigned len);
static const char *gald_safe_isa_name(id obj);

int __gald_eh_isa(NFObject *obj, NFClass *cls) {
    if (!obj || !cls) return 0;
    return gald_isKindOf(obj, cls) ? 1 : 0;
}

void gald_eh_uncaught(void) {
    const char *cls = gald_safe_isa_name(__gald_eh_val);
    gald_console_write("*** Terminating app due to uncaught exception of class '", 0);
    gald_console_write(cls, 0);
    gald_console_write("'\n", 0);
    __builtin_trap();
}

// ─── Async task API (same shape as hosted runtime.c) ────────────────────────
// Cooperative single-thread state machine pump; identical logic to the host,
// with gald_malloc/gald_free instead of calloc/free. No new overhead: a task
// costs one struct + its frame, same as hosted.

NFTask *gald_task_create(gald_task_entry_fn entry, NFObject *self_obj, size_t frame_size) {
    NFTask *t = (NFTask *)gald_malloc(sizeof(NFTask));
    if (!t) return NULL;
    t->state = 1;   /* state 1 = the entry's first case; 0 means "not started" */
    t->finished = 0;
    t->entry = entry;
    t->self_obj = self_obj;
    t->frame = frame_size ? gald_malloc(frame_size) : NULL;
    t->result = NULL;
    t->parent = NULL;
    return t;
}

int gald_task_resume(NFTask *task) {
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

void gald_task_finish(NFTask *task) {
    if (task) task->finished = 1;
}

void *gald_task_join(NFTask *task) {
    if (!task) return NULL;
    while (!task->finished) {
        (void)gald_task_resume(task);
    }
    void *result = task->result;
    /* bump allocator: gald_free is a no-op, kept for API parity */
    gald_free(task->frame);
    gald_free(task);
    return result;
}

// ─── Weak reference side table (fixed-size, no allocation) ──────────────────
// Hosted runtime uses a malloc'd slot array; bare metal uses a static table
// with a fixed number of slots per target. Zero heap overhead, linear scan.
// 64 targets × 8 slots each = 4 KiB static data — tune via GALD_WEAK_MAX.

#ifndef GALD_WEAK_MAX_TARGETS
#define GALD_WEAK_MAX_TARGETS 64
#endif
#define GALD_WEAK_SLOTS_PER_TARGET 8

typedef struct {
    NFObject *object;                 /* NULL = free entry */
    NFObject **slots[GALD_WEAK_SLOTS_PER_TARGET];
} WeakEntry;

static WeakEntry weak_table[GALD_WEAK_MAX_TARGETS];

static WeakEntry *find_weak_entry(NFObject *target) {
    for (int i = 0; i < GALD_WEAK_MAX_TARGETS; i++) {
        if (weak_table[i].object == target)
            return &weak_table[i];
    }
    return NULL;
}

void gald_weakRegister(NFObject **weak_loc, NFObject *target) {
    if (!target || !weak_loc) return;
    WeakEntry *entry = find_weak_entry(target);
    if (!entry) {
        /* first free entry; full table → silently drop (bare-metal policy) */
        for (int i = 0; i < GALD_WEAK_MAX_TARGETS; i++) {
            if (!weak_table[i].object) { entry = &weak_table[i]; break; }
        }
        if (!entry) return;
        entry->object = target;
        for (int j = 0; j < GALD_WEAK_SLOTS_PER_TARGET; j++) entry->slots[j] = NULL;
    }
    for (int j = 0; j < GALD_WEAK_SLOTS_PER_TARGET; j++) {
        if (!entry->slots[j]) { entry->slots[j] = weak_loc; return; }
    }
}

void gald_weakUnregister(NFObject **weak_loc) {
    if (!weak_loc) return;
    for (int i = 0; i < GALD_WEAK_MAX_TARGETS; i++) {
        WeakEntry *entry = &weak_table[i];
        for (int j = 0; j < GALD_WEAK_SLOTS_PER_TARGET; j++) {
            if (entry->slots[j] == weak_loc) {
                entry->slots[j] = NULL;
                /* last slot gone → release the entry so its target slot is
                 * reclaimable; otherwise every once-weak target pins its
                 * entry forever and the 64-target table silently saturates */
                int empty = 1;
                for (int k = 0; k < GALD_WEAK_SLOTS_PER_TARGET; k++) {
                    if (entry->slots[k]) { empty = 0; break; }
                }
                if (empty && entry->object) entry->object = NULL;
                return;
            }
        }
    }
}

void gald_weakClearAll(NFObject *target) {
    if (!target) return;
    WeakEntry *entry = find_weak_entry(target);
    if (!entry) return;
    for (int j = 0; j < GALD_WEAK_SLOTS_PER_TARGET; j++) {
        if (entry->slots[j]) *entry->slots[j] = NULL;
        entry->slots[j] = NULL;
    }
    entry->object = NULL;
}

void gald_weakAutoCleanup(void *ptr) {
    gald_weakUnregister((NFObject **)ptr);
}

// ─── @synchronized monitors ──────────────────────────────────────────────────
// Bare metal is single-core: mutual exclusion is trivially satisfied, so the
// monitor API is a no-op pair that keeps the same signatures as the hosted
// runtime — transpiled code compiles unchanged on both runtimes. (If SMP
// arrives, replace with a spinlock over the same bucket scheme; interrupt-
// based concurrency would additionally need irq-disable around the lock.)

long gald_syncLock(void *object) {
    (void)object;
    return 0;
}

void gald_syncUnlock(long bucket) {
    (void)bucket;
}

void gald_syncAutoCleanup(void *ptr) {
    (void)ptr;
}

// ─── Console hook (exception diagnostics only) ──────────────────────────────
// Bare metal has no stdout/stderr: I/O is NOT a language mechanism, so the
// runtime provides no printf/NFLog. The only output is the uncaught-
// exception report from gald_eh_uncaught(), routed through this hook.
// Users override it with their console driver (UART/kputs-style); the weak
// no-op default links cleanly and the program simply traps silently.
//   void gald_console_write(const char *s, unsigned len);
// len == 0 means NUL-terminated.

__attribute__((weak)) void gald_console_write(const char *s, unsigned len) {
    (void)s; (void)len; /* no console: discard */
}

/* 1 when p came from the bump allocator (a real object), 0 for raw
 * pointers (C string literals cast to id). Zero cost: two compares. */
static int gald_is_heap_object(const void *p) {
    return (const unsigned char *)p >= gald_heap
        && (const unsigned char *)p < gald_heap + GALD_HEAP_SIZE;
}

/* Safe class name for gald_eh_uncaught: raw literals have no isa. */
static const char *gald_safe_isa_name(id obj) {
    if (obj && gald_is_heap_object(obj) && obj->isa && obj->isa->name)
        return obj->isa->name;
    return "?";
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

NFObject *gald_alloc(NFClass *cls) {
    if (!cls) return NULL;
    NFObject *obj = (NFObject *)gald_malloc(cls->instance_size);
    if (obj) {
        memset(obj, 0, cls->instance_size);
        obj->isa = cls;
        obj->retain_count = 1;
    }
    return obj;
}

NFObject *gald_init(NFObject *self) {
    return self;
}

NFObject *gald_retain(NFObject *obj) {
    /* Raw pointers (@"..." literals without NFString, C strings cast to id)
     * live outside the bump heap: writing retain_count there faults. No-op. */
    if (!obj || !gald_is_heap_object(obj)) return obj;
    obj->retain_count++;
    return obj;
}

void gald_release(NFObject *obj) {
    if (!obj || !gald_is_heap_object(obj)) return;
    if (obj->retain_count > 0)
        obj->retain_count--;
    if (obj->retain_count == 0) {
        gald_weakClearAll(obj);
        // Call dealloc so ivar cleanup runs (e.g. NFString frees _cstr).
        // dealloc's own `[super dealloc]` calls the parent's dealloc directly
        // (not gald_release), so no double-free.
        if (obj->isa && obj->isa->dealloc) {
            obj->isa->dealloc(obj, (SEL){ .name = "dealloc", .hash = 0xD9929EB3 });
        }
        gald_free(obj);
    }
}

NFObject *gald_autorelease(NFObject *obj) {
    if (!obj || !gald_is_heap_object(obj)) return obj;
    gald_autoreleasepool_t *pool = current_pool;
    if (!pool) return obj;
    if (pool->count >= pool->capacity) {
        int new_cap = pool->capacity ? pool->capacity * 2 : 16;
        NFObject **new_objs = gald_malloc(new_cap * sizeof(NFObject *));
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