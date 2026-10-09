#include "ovel/runtime.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ─── OVEL_CLASS_$_ovel_root (defined weak; codegen's ovel_metaInit fills it) ──

__attribute__((weak)) NPClass OVEL_CLASS_$_ovel_root;

// ─── Protocol conformance (D5.2) ─────────────────────────────────────────────

/* Does `set` of protocol `p` (recursively including parents) require every
   required method that `target` requires? Name comparison only, colon-stripped
   (the selector symbol convention). */
static int proto_covers(const struct NPProtocol *p, const struct NPProtocol *target);

static int proto_requires(const struct NPProtocol *p, const char *sel) {
    for (int i = 0; i < p->required_count; i++) {
        if (strcmp(p->required_methods[i].name, sel) == 0) return 1;
    }
    for (int i = 0; i < p->parent_count; i++) {
        if (p->parents[i] && proto_requires(p->parents[i], sel)) return 1;
    }
    return 0;
}

static int proto_covers(const struct NPProtocol *p, const struct NPProtocol *target) {
    if (!p || !target) return 0;
    if (p == target) return 1;
    if (p->name && target->name && strcmp(p->name, target->name) == 0) return 1;
    /* structural: p requires everything target requires (with parents) */
    for (int i = 0; i < target->required_count; i++) {
        if (!proto_requires(p, target->required_methods[i].name)) return 0;
    }
    return 1;
}

static int class_conforms(const NPClass *cls, const struct NPProtocol *proto, int depth) {
    if (!cls || !proto || depth > 32) return 0;
    for (int i = 0; i < cls->protocol_count; i++) {
        if (cls->protocols[i] && proto_covers(cls->protocols[i], proto)) return 1;
    }
    /* walk the superclass chain — conformance is inherited */
    return cls->superclass ? class_conforms(cls->superclass, proto, depth + 1) : 0;
}

int ovel_class_conformsToProtocol(NPClass *cls, struct NPProtocol *proto) {
    return class_conforms(cls, proto, 0);
}

void ovel_register_category_protocols(NPClass *cls, struct NPProtocol **protocols, int count) {
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
    struct NPProtocol **merged = malloc((size_t)(old_count + add) * sizeof(*merged));
    if (!merged) abort();
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

// ─── Exception globals ────────────────────────────────────────────────────────

#ifdef __OVEL_FREESTANDING
jmp_buf __ovel_exception_buf;
id      __ovel_exception_value;
#else
__thread jmp_buf __ovel_exception_buf;
__thread id     __ovel_exception_value;
#endif

// ─── Checked-exception (Swift-scheme) error flag ──────────────────────────────

#ifdef __OVEL_FREESTANDING
int __ovel_eh_flag;
id   __ovel_eh_val;
#else
__thread int __ovel_eh_flag;
__thread id   __ovel_eh_val;
#endif

int __ovel_eh_isa(NPObject *obj, NPClass *cls) {
    if (!obj || !cls) return 0;
    return ovel_isKindOf(obj, cls) ? 1 : 0;
}

// Uncaught checked exception: an exception escaped `main` (flag still armed at
// the function-tail guard of main). Mirror ObjC's wording on stderr, then
// abort — the process MUST NOT exit 0 with an exception in flight.
void ovel_eh_uncaught(void) {
    const char *cls = (__ovel_eh_val && __ovel_eh_val->isa && __ovel_eh_val->isa->name)
        ? __ovel_eh_val->isa->name : "?";
    fprintf(stderr, "*** Terminating app due to uncaught exception of class '%s'\n", cls);
    abort();
}

// ─── Weak reference side table ───────────────────────────────────────────────

#define MAX_WEAK_ENTRIES 1024
#define INITIAL_SLOT_CAPACITY 4

typedef struct {
    NPObject *object;
    NPObject ***slots;
    int count;
    int capacity;
} WeakEntry;

static WeakEntry weak_table[MAX_WEAK_ENTRIES];
static int weak_entries = 0;

static WeakEntry *find_entry(NPObject *target) {
    for (int i = 0; i < weak_entries; i++) {
        if (weak_table[i].object == target)
            return &weak_table[i];
    }
    return NULL;
}

void ovel_weakRegister(NPObject **weak_loc, NPObject *target) {
    if (!target || !weak_loc) return;
    WeakEntry *entry = find_entry(target);
    if (!entry) {
        if (weak_entries >= MAX_WEAK_ENTRIES) return;
        entry = &weak_table[weak_entries++];
        entry->object = target;
        entry->slots = malloc(INITIAL_SLOT_CAPACITY * sizeof(NPObject **));
        entry->count = 0;
        entry->capacity = INITIAL_SLOT_CAPACITY;
    }
    if (entry->count >= entry->capacity) {
        entry->capacity *= 2;
        entry->slots = realloc(entry->slots, entry->capacity * sizeof(NPObject **));
    }
    entry->slots[entry->count++] = weak_loc;
}

void ovel_weakUnregister(NPObject **weak_loc) {
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

void ovel_weakClearAll(NPObject *target) {
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

void ovel_weakAutoCleanup(void *ptr) {
    ovel_weakUnregister((NPObject **)ptr);
}

// ─── @synchronized monitors ──────────────────────────────────────────────────
//
// Global bucket array of spinlocks keyed by object address (see runtime.h).
// Hosted implementation: C11 atomics — `atomic_flag` test_and_set is a lock
// primitive on every platform clang targets here, no pthread dependency, no
// allocation, no table-growth ceiling. A thread that returns from
// ovel_syncLock owns bucket[b] until the matching ovel_syncUnlock.

#include <stdatomic.h>

#define OVEL_SYNC_BUCKETS 256

static atomic_flag ovel_sync_flags[OVEL_SYNC_BUCKETS];

static unsigned ovel_sync_hash(void *object) {
    unsigned long v = (unsigned long)(uintptr_t)object;
    unsigned hash = 0x811C9DC5u;
    for (unsigned i = 0; i < sizeof(void *); i++) {
        hash ^= (unsigned char)(v & 0xFFu);
        hash *= 0x01000193u;
        v >>= 8;
    }
    return hash % OVEL_SYNC_BUCKETS;
}

long ovel_syncLock(void *object) {
    unsigned b = ovel_sync_hash(object);
    while (atomic_flag_test_and_set_explicit(&ovel_sync_flags[b], memory_order_acquire)) {
        // spin
    }
    return (long)b;
}

void ovel_syncUnlock(long bucket) {
    if (bucket < 0 || bucket >= OVEL_SYNC_BUCKETS) return;
    atomic_flag_clear_explicit(&ovel_sync_flags[bucket], memory_order_release);
}

void ovel_syncAutoCleanup(void *ptr) {
    ovel_syncUnlock(*(long *)ptr);
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

BOOL ovel_isKindOf(NPObject *obj, NPClass *cls) {
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
BOOL ovel_isKindOfClass(NPObject *obj, NPClass *cls) {
    return ovel_isKindOf(obj, cls);
}

// ─── Autorelease pool ─────────────────────────────────────────────────────────

struct ovel_autoreleasepool {
    struct ovel_autoreleasepool *next;
    NPObject **objects;
    int count;
    int capacity;
};

static __thread ovel_autoreleasepool_t *current_pool = NULL;

ovel_autoreleasepool_t *ovel_autoreleasepoolPush(void) {
    ovel_autoreleasepool_t *pool = calloc(1, sizeof(ovel_autoreleasepool_t));
    if (!pool) return NULL;
    pool->next = current_pool;
    current_pool = pool;
    return pool;
}

void ovel_autoreleasepoolPop(ovel_autoreleasepool_t *pool) {
    if (!pool) return;
    for (int i = 0; i < pool->count; i++) {
        ovel_release(pool->objects[i]);
    }
    free(pool->objects);
    current_pool = pool->next;
    free(pool);
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

NPObject *ovel_alloc(NPClass *cls) {
    if (!cls) return NULL;
    NPObject *obj = (NPObject *)calloc(1, cls->instance_size);
    if (obj) {
        obj->isa = cls;
        obj->retain_count = 1;
    }
    return obj;
}

NPObject *ovel_init(NPObject *self) {
    return self;
}

// ─── Refcount debug trace (OVEL_REFCOUNT_DEBUG) ─────────────────────────────
// Purely a debug aid: reads the env var once, then prints every retain /
// release event to stderr. Default OFF — zero behavior change when unset.
// Static language note: this is plain C control flow baked in at compile
// time; it adds no runtime dynamism to the language itself.
static int ovel_rc_debug = -1;   // -1 = not yet resolved

static int ovel_rc_debug_enabled(void) {
    if (ovel_rc_debug < 0)
        ovel_rc_debug = getenv("OVEL_REFCOUNT_DEBUG") != NULL;
    return ovel_rc_debug;
}

static void ovel_rc_trace(const char *op, NPObject *obj, uint32_t rc_after, const char *note) {
    const char *cls = (obj->isa && obj->isa->name) ? obj->isa->name : "?";
    if (note)
        fprintf(stderr, "[rc] %s %p %s rc=%u (%s)\n", op, (void *)obj, cls, rc_after, note);
    else
        fprintf(stderr, "[rc] %s %p %s rc=%u\n", op, (void *)obj, cls, rc_after);
}

NPObject *ovel_retain(NPObject *obj) {
    if (!obj) return NULL;
    obj->retain_count++;
    if (ovel_rc_debug_enabled())
        ovel_rc_trace("retain", obj, obj->retain_count, NULL);
    return obj;
}

void ovel_release(NPObject *obj) {
    if (!obj) return;
    if (obj->retain_count > 0)
        obj->retain_count--;
    if (ovel_rc_debug_enabled()) {
        // Under-counting release (already at 0) is a bug worth flagging.
        const char *note = (obj->retain_count == 0 && obj->isa && obj->isa->dealloc)
            ? "dealloc" : NULL;
        ovel_rc_trace("release", obj, obj->retain_count, note);
    }
    if (obj->retain_count == 0) {
        // Zero weak references BEFORE dealloc: dealloc may free other objects
        // (strong ivars) whose memory holds a weak slot pointing back to us
        // (e.g. a child's `__weak parent`). Zeroing first avoids a use-after-free.
        ovel_weakClearAll(obj);
        // Call dealloc so ivar cleanup runs. dealloc's `[super dealloc]` calls
        // the parent's dealloc directly (not ovel_release), so no double-free.
        if (obj->isa && obj->isa->dealloc) {
            obj->isa->dealloc(obj, (SEL){ .name = "dealloc", .hash = 0xD9929EB3 });
        }
        free(obj);
    }
}

NPObject *ovel_autorelease(NPObject *obj) {
    if (!obj) return obj;
    ovel_autoreleasepool_t *pool = current_pool;
    if (!pool) return obj;
    if (pool->count >= pool->capacity) {
        pool->capacity = pool->capacity ? pool->capacity * 2 : 16;
        pool->objects = realloc(pool->objects, pool->capacity * sizeof(NPObject *));
        if (!pool->objects) return obj;
    }
    pool->objects[pool->count++] = obj;
    return obj;
}

// ─── String literals ──────────────────────────────────────────────────────────
// ovel_stringFromCstr is emitted by the codegen in the generated C code.
// The runtime.h declaration is used by the generated code to call it.
// When NPString is not present, @"..." falls back to a regular C string literal.

// ─── Async tasks (route map item #4) ────────────────────────────────────────

// Cooperative single-thread async: the desugared method pumps its own task
// to completion, so a task's lifetime is fully contained in the call that
// created it (milestone 1). `parent` exists for milestone 2 (task graphs
// where a suspended task resumes its awaiter).

/* Allocator injection: NULL = malloc/free defaults (ovel_task_set_allocator
 * swaps in a static pool on bare metal). Read through function pointers so
 * the switch can happen before the first task is created. */
static ovel_task_alloc_fn task_alloc = NULL;   /* set lazily to malloc */
static ovel_task_free_fn  task_free_fn = NULL; /* set lazily to free */

static void ensure_default_allocator(void) {
    if (!task_alloc) task_alloc = malloc;
    if (!task_free_fn) task_free_fn = free;
}

void ovel_task_set_allocator(ovel_task_alloc_fn alloc, ovel_task_free_fn free_fn) {
    task_alloc = alloc;
    task_free_fn = free_fn;
}

NPTask *ovel_task_create(ovel_task_entry_fn entry, NPObject *self_obj, size_t frame_size) {
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

int ovel_task_resume(NPTask *task) {
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

void ovel_task_finish(NPTask *task) {
    if (task) task->finished = 1;
}

void *ovel_task_join(NPTask *task) {
    if (!task) return NULL;
    while (!task->finished) {
        (void)ovel_task_resume(task);
    }
    void *result = task->result;
    if (task->frame) free(task->frame);
    free(task);
    return result;
}

// ─── NPTask scheduling layer (doc/async_nptask_plan.md, stage C) ────────────
// Lazy tasks + a cooperative ready queue. Mechanism only — the pump is never
// invoked implicitly here; hosted entry wrappers (stage D) call
// ovel_sched_run explicitly, bare-metal main loops own their own pump.

/* READY → RUNNING → SUSPENDED → … → DONE, as a tiny explicit queue. */
enum { TASK_READY = 0, TASK_QUEUED = 1, TASK_DONE = 2 };

static NPTask *sched_head = NULL;
static NPTask *sched_tail = NULL;
static NPTask *sched_current = NULL;

NPTask *ovel_task_current(void) { return sched_current; }

static void sched_enqueue(NPTask *t) {
    t->parent = NULL;          /* reuse `parent` as the queue link */
    if (sched_tail) sched_tail->parent = t;
    else sched_head = t;
    sched_tail = t;
    t->queued = 1;
}

int ovel_task_start(NPTask *task) {
    if (!task || task->finished) return task != NULL;
    if (task->queued) return 1;                 /* idempotent */
    sched_enqueue(task);
    return 1;
}

void ovel_task_mark_ready(NPTask *task) {
    (void)ovel_task_start(task);
}

void ovel_sched_run(void) {
    while (sched_head) {
        NPTask *t = sched_head;
        sched_head = t->parent;
        if (!sched_head) sched_tail = NULL;
        t->queued = 0;
        if (t->finished) continue;
        sched_current = t;
        (void)ovel_task_resume(t);
        sched_current = NULL;
        /* finished tasks are dropped (frame/task freed by stage-D desugar at
         * the join point; legacy join still owns its own lifetime) */
    }
}

void *ovel_task_await(NPTask *task) {
    if (!task) return NULL;
    if (task == sched_current) {
        /* self-await cycle: fatal by design (no thread to make progress) */
        fprintf(stderr, "fatal: task awaits itself\n");
        abort();
    }
    (void)ovel_task_start(task);
    /* Blocking drive, top-level or in-task alike (M2 synchronous-drive
     * model): cooperative single-thread — an in-task await on an independent
     * nested task can drive it to completion inline; only a cycle deadlocks,
     * and direct self-await is checked above. */
    while (!task->finished) {
        if (sched_head) { ovel_sched_run(); continue; }
        (void)ovel_task_resume(task);
    }
    return task->result;
}

// ─── Logging ─────────────────────────────────────────────────────────────────

// NPLog takes a Foundation `NPString *` format. The `struct NPString` layout is
// generated by the transpiler (from NPString.oh's `@public` ivars), so runtime.c
// mirrors that layout here to reach `_cstr` without depending on the generated
// header. Keep in sync with NPString.oh (isa/retain_count base + _cstr/_length/
// _hash/_hashIsValid).
struct __ovel_npstring_layout {
    void *isa;
    uint32_t retain_count;
    char *_cstr;
    size_t _length;
    uint32_t _hash;
    int _hashIsValid;
};

void NPLog(NPString *format, ...) {
    const char *cstr = format ? ((struct __ovel_npstring_layout *)format)->_cstr : "";
    va_list args;
    va_start(args, format);
    vfprintf(stderr, cstr ? cstr : "", args);
    va_end(args);
    fprintf(stderr, "\n");
}

void __NPLogv(NPString *format, va_list args) {
    const char *cstr = format ? ((struct __ovel_npstring_layout *)format)->_cstr : "";
    vfprintf(stderr, cstr ? cstr : "", args);
    fprintf(stderr, "\n");
}

// ─── KVC lookup (NPPredicate support) ───────────────────────────────────────

// Walk obj's isa chain and scan each class's OVEL_KVC_$_<Class> table for
// `key`. Codegen emits the tables (see doc/nppredicate_plan.md §2); classes
// compiled without KVC emission leave the field NULL and the walk continues
// to the superclass. The getter itself is an ordinary static vtable dispatch,
// so this is table-driven lookup, never reflection.
id ovel_kvc_value(id obj, const char *key) {
    if (!obj || !key) return NULL;
    // Every object (NPObject or ovel_root) starts with the isa pointer.
    NPClass *cls = *(NPClass **)obj;
    for (int depth = 0; cls && depth < 64; cls = cls->superclass, depth++) {
        const ovel_kvc_entry *e = cls->kvc_entries;
        if (!e) continue;
        for (; e->key; e++) {
            if (strcmp(e->key, key) == 0) {
                return e->get(obj);
            }
        }
    }
    return NULL;
}
