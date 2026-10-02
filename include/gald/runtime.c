#include "gald/runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ─── GALD_CLASS_$_gald_root (defined weak; codegen's gald_metaInit fills it) ──

__attribute__((weak)) NFClass GALD_CLASS_$_gald_root;

// ─── Exception globals ────────────────────────────────────────────────────────

#ifdef __GALD_FREESTANDING
jmp_buf __gald_exception_buf;
id      __gald_exception_value;
#else
__thread jmp_buf __gald_exception_buf;
__thread id     __gald_exception_value;
#endif

// ─── Checked-exception (Swift-scheme) error flag ──────────────────────────────

#ifdef __GALD_FREESTANDING
int __gald_eh_flag;
id   __gald_eh_val;
#else
__thread int __gald_eh_flag;
__thread id   __gald_eh_val;
#endif

int __gald_eh_isa(NFObject *obj, NFClass *cls) {
    if (!obj || !cls) return 0;
    return gald_isKindOf(obj, cls) ? 1 : 0;
}

// Uncaught checked exception: an exception escaped `main` (flag still armed at
// the function-tail guard of main). Mirror ObjC's wording on stderr, then
// abort — the process MUST NOT exit 0 with an exception in flight.
void gald_eh_uncaught(void) {
    const char *cls = (__gald_eh_val && __gald_eh_val->isa && __gald_eh_val->isa->name)
        ? __gald_eh_val->isa->name : "?";
    fprintf(stderr, "*** Terminating app due to uncaught exception of class '%s'\n", cls);
    abort();
}

// ─── Weak reference side table ───────────────────────────────────────────────

#define MAX_WEAK_ENTRIES 1024
#define INITIAL_SLOT_CAPACITY 4

typedef struct {
    NFObject *object;
    NFObject ***slots;
    int count;
    int capacity;
} WeakEntry;

static WeakEntry weak_table[MAX_WEAK_ENTRIES];
static int weak_entries = 0;

static WeakEntry *find_entry(NFObject *target) {
    for (int i = 0; i < weak_entries; i++) {
        if (weak_table[i].object == target)
            return &weak_table[i];
    }
    return NULL;
}

void gald_weakRegister(NFObject **weak_loc, NFObject *target) {
    if (!target || !weak_loc) return;
    WeakEntry *entry = find_entry(target);
    if (!entry) {
        if (weak_entries >= MAX_WEAK_ENTRIES) return;
        entry = &weak_table[weak_entries++];
        entry->object = target;
        entry->slots = malloc(INITIAL_SLOT_CAPACITY * sizeof(NFObject **));
        entry->count = 0;
        entry->capacity = INITIAL_SLOT_CAPACITY;
    }
    if (entry->count >= entry->capacity) {
        entry->capacity *= 2;
        entry->slots = realloc(entry->slots, entry->capacity * sizeof(NFObject **));
    }
    entry->slots[entry->count++] = weak_loc;
}

void gald_weakUnregister(NFObject **weak_loc) {
    if (!weak_loc) return;
    for (int i = 0; i < weak_entries; i++) {
        WeakEntry *entry = &weak_table[i];
        for (int j = 0; j < entry->count; j++) {
            if (entry->slots[j] == weak_loc) {
                entry->slots[j] = entry->slots[--entry->count];
                return;
            }
        }
    }
}

void gald_weakClearAll(NFObject *target) {
    if (!target) return;
    for (int i = 0; i < weak_entries; i++) {
        WeakEntry *entry = &weak_table[i];
        if (entry->object == target) {
            for (int j = 0; j < entry->count; j++) {
                *entry->slots[j] = NULL;
            }
            free(entry->slots);
            weak_table[i] = weak_table[--weak_entries];
            return;
        }
    }
}

void gald_weakAutoCleanup(void *ptr) {
    gald_weakUnregister((NFObject **)ptr);
}

// ─── @synchronized monitors ──────────────────────────────────────────────────
//
// Global bucket array of spinlocks keyed by object address (see runtime.h).
// Hosted implementation: C11 atomics — `atomic_flag` test_and_set is a lock
// primitive on every platform clang targets here, no pthread dependency, no
// allocation, no table-growth ceiling. A thread that returns from
// gald_syncLock owns bucket[b] until the matching gald_syncUnlock.

#include <stdatomic.h>

#define GALD_SYNC_BUCKETS 256

static atomic_flag gald_sync_flags[GALD_SYNC_BUCKETS];

static unsigned gald_sync_hash(void *object) {
    unsigned long v = (unsigned long)(uintptr_t)object;
    unsigned hash = 0x811C9DC5u;
    for (unsigned i = 0; i < sizeof(void *); i++) {
        hash ^= (unsigned char)(v & 0xFFu);
        hash *= 0x01000193u;
        v >>= 8;
    }
    return hash % GALD_SYNC_BUCKETS;
}

long gald_syncLock(void *object) {
    unsigned b = gald_sync_hash(object);
    while (atomic_flag_test_and_set_explicit(&gald_sync_flags[b], memory_order_acquire)) {
        // spin
    }
    return (long)b;
}

void gald_syncUnlock(long bucket) {
    if (bucket < 0 || bucket >= GALD_SYNC_BUCKETS) return;
    atomic_flag_clear_explicit(&gald_sync_flags[bucket], memory_order_release);
}

void gald_syncAutoCleanup(void *ptr) {
    gald_syncUnlock(*(long *)ptr);
}

// ─── Selectors ───────────────────────────────────────────────────────────────

SEL sel_registerName(const char *name) {
    unsigned hash = 0x811C9DC5;
    for (const char *p = name; *p; p++) {
        hash ^= (unsigned char)*p;
        hash *= 0x01000193;
    }
    SEL sel = { name, hash };
    return sel;
}

// ─── Type introspection ─────────────────────────────────────────────────────────

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

// ─── Autorelease pool ─────────────────────────────────────────────────────────

struct gald_autoreleasepool {
    struct gald_autoreleasepool *next;
    NFObject **objects;
    int count;
    int capacity;
};

static __thread gald_autoreleasepool_t *current_pool = NULL;

gald_autoreleasepool_t *gald_autoreleasepoolPush(void) {
    gald_autoreleasepool_t *pool = calloc(1, sizeof(gald_autoreleasepool_t));
    if (!pool) return NULL;
    pool->next = current_pool;
    current_pool = pool;
    return pool;
}

void gald_autoreleasepoolPop(gald_autoreleasepool_t *pool) {
    if (!pool) return;
    for (int i = 0; i < pool->count; i++) {
        gald_release(pool->objects[i]);
    }
    free(pool->objects);
    current_pool = pool->next;
    free(pool);
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

NFObject *gald_alloc(NFClass *cls) {
    if (!cls) return NULL;
    NFObject *obj = (NFObject *)calloc(1, cls->instance_size);
    if (obj) {
        obj->isa = cls;
        obj->retain_count = 1;
    }
    return obj;
}

NFObject *gald_init(NFObject *self) {
    return self;
}

// ─── Refcount debug trace (GALD_REFCOUNT_DEBUG) ─────────────────────────────
// Purely a debug aid: reads the env var once, then prints every retain /
// release event to stderr. Default OFF — zero behavior change when unset.
// Static language note: this is plain C control flow baked in at compile
// time; it adds no runtime dynamism to the language itself.
static int gald_rc_debug = -1;   // -1 = not yet resolved

static int gald_rc_debug_enabled(void) {
    if (gald_rc_debug < 0)
        gald_rc_debug = getenv("GALD_REFCOUNT_DEBUG") != NULL;
    return gald_rc_debug;
}

static void gald_rc_trace(const char *op, NFObject *obj, uint32_t rc_after, const char *note) {
    const char *cls = (obj->isa && obj->isa->name) ? obj->isa->name : "?";
    if (note)
        fprintf(stderr, "[rc] %s %p %s rc=%u (%s)\n", op, (void *)obj, cls, rc_after, note);
    else
        fprintf(stderr, "[rc] %s %p %s rc=%u\n", op, (void *)obj, cls, rc_after);
}

NFObject *gald_retain(NFObject *obj) {
    if (!obj) return NULL;
    obj->retain_count++;
    if (gald_rc_debug_enabled())
        gald_rc_trace("retain", obj, obj->retain_count, NULL);
    return obj;
}

void gald_release(NFObject *obj) {
    if (!obj) return;
    if (obj->retain_count > 0)
        obj->retain_count--;
    if (gald_rc_debug_enabled()) {
        // Under-counting release (already at 0) is a bug worth flagging.
        const char *note = (obj->retain_count == 0 && obj->isa && obj->isa->dealloc)
            ? "dealloc" : NULL;
        gald_rc_trace("release", obj, obj->retain_count, note);
    }
    if (obj->retain_count == 0) {
        // Zero weak references BEFORE dealloc: dealloc may free other objects
        // (strong ivars) whose memory holds a weak slot pointing back to us
        // (e.g. a child's `__weak parent`). Zeroing first avoids a use-after-free.
        gald_weakClearAll(obj);
        // Call dealloc so ivar cleanup runs. dealloc's `[super dealloc]` calls
        // the parent's dealloc directly (not gald_release), so no double-free.
        if (obj->isa && obj->isa->dealloc) {
            obj->isa->dealloc(obj, (SEL){ .name = "dealloc", .hash = 0xD9929EB3 });
        }
        free(obj);
    }
}

NFObject *gald_autorelease(NFObject *obj) {
    if (!obj) return obj;
    gald_autoreleasepool_t *pool = current_pool;
    if (!pool) return obj;
    if (pool->count >= pool->capacity) {
        pool->capacity = pool->capacity ? pool->capacity * 2 : 16;
        pool->objects = realloc(pool->objects, pool->capacity * sizeof(NFObject *));
        if (!pool->objects) return obj;
    }
    pool->objects[pool->count++] = obj;
    return obj;
}

// ─── String literals ──────────────────────────────────────────────────────────
// gald_stringFromCstr is emitted by the codegen in the generated C code.
// The runtime.h declaration is used by the generated code to call it.
// When NFString is not present, @"..." falls back to a regular C string literal.

// ─── Async tasks (route map item #4) ────────────────────────────────────────

// Cooperative single-thread async: the desugared method pumps its own task
// to completion, so a task's lifetime is fully contained in the call that
// created it (milestone 1). `parent` exists for milestone 2 (task graphs
// where a suspended task resumes its awaiter).

NupaTask *gald_task_create(gald_task_entry_fn entry, NFObject *self_obj, size_t frame_size) {
    NupaTask *t = (NupaTask *)calloc(1, sizeof(NupaTask));
    if (!t) return NULL;
    t->state = 1;   /* state 1 = the entry's first case; 0 means "not started" */
    t->finished = 0;
    t->entry = entry;
    t->self_obj = self_obj;
    t->frame = frame_size ? calloc(1, frame_size) : NULL;
    t->result = NULL;
    t->parent = NULL;
    return t;
}

int gald_task_resume(NupaTask *task) {
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

void gald_task_finish(NupaTask *task) {
    if (task) task->finished = 1;
}

void *gald_task_join(NupaTask *task) {
    if (!task) return NULL;
    while (!task->finished) {
        (void)gald_task_resume(task);
    }
    void *result = task->result;
    if (task->frame) free(task->frame);
    free(task);
    return result;
}

// ─── Logging ─────────────────────────────────────────────────────────────────

// NFLog takes a Foundation `NFString *` format. The `struct NFString` layout is
// generated by the transpiler (from NFString.gh's `@public` ivars), so runtime.c
// mirrors that layout here to reach `_cstr` without depending on the generated
// header. Keep in sync with NFString.gh (isa/retain_count base + _cstr/_length/
// _hash/_hashIsValid).
struct __gald_npstring_layout {
    void *isa;
    uint32_t retain_count;
    char *_cstr;
    size_t _length;
    uint32_t _hash;
    int _hashIsValid;
};

void NFLog(NFString *format, ...) {
    const char *cstr = format ? ((struct __gald_npstring_layout *)format)->_cstr : "";
    va_list args;
    va_start(args, format);
    vfprintf(stderr, cstr ? cstr : "", args);
    va_end(args);
    fprintf(stderr, "\n");
}

void __NFLogv(NFString *format, va_list args) {
    const char *cstr = format ? ((struct __gald_npstring_layout *)format)->_cstr : "";
    vfprintf(stderr, cstr ? cstr : "", args);
    fprintf(stderr, "\n");
}