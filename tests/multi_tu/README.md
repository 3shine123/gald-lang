# Cross-TU link consistency suite

Run: `./run_multi_tu.sh` (filter with `./run_multi_tu.sh 05_block`) · `GALDC=path/to/galdc` to pick a binary.

## Why this suite exists

Nupa's uniform vtable is synthesised **per translation unit**: `struct gald_vtable`
gets one member per instance method that TU happens to see. Plain C never hits
this because every struct is spelled out in the source, so C's type system
protects you. Nupa synthesises the layout, so nothing does.

When two TUs see different method sets they compile different layouts, while the
linker weak-merges the vtable *instances* into a single allocation. Dispatch
through the losing layout then reads the wrong slot — silent garbage, or a
segfault at a line that has nothing to do with the real cause.

This suite pins down, per language feature, whether it survives a cross-TU link.
The point is to turn "I discovered this three weeks later as a mystery crash"
into "the test told me this feature cannot cross a TU boundary."

## The rule

**Every instance method a class has must be visible to every TU that links
against it.** In practice: declare all of them in the shared `.gh`. A method that
exists only in an `@implementation` is seen by exactly one TU, which is
sufficient to break the link.

Codegen stamps an FNV-1a signature of the layout into the vtable struct itself
(`__sig`, first member) so it weak-merges together with the instance that won,
and a per-TU `__attribute__((constructor))` verifies it. A mismatch aborts at
load time with both signatures and this TU's method list — a loud failure rather
than a wrong-offset call. (`gald_metaInit` is the wrong place for that check: it
is weak-merged, so only one TU's copy ever runs and would only ever compare its
own layout against itself.)

## Cases

| Case | What it pins down |
|---|---|
| `01_basic` | Plain cross-TU method dispatch through a shared `.gh`. |
| `02_inheritance` | Subclass allocated in the lib TU, driven through a base-class pointer — every send is a real cross-TU vtable dispatch, and the subclass's struct must physically embed the parent's ivars. |
| `03_generic` | Monomorphised generic. **Requires the lib TU to name the instantiation** (`Stack<int *>`) inside a method body or variable declaration — instantiation is collected by *usage*, so an `@implementation` that never mentions it emits no specialised struct or vtable. |
| `04_namespace` | `::` → `__` C symbol encoding must round-trip: the client names `Geo::Origin`, the lib defines it. |
| `05_block` | A Nupa block handed to a lib-TU method, stored, and fired later. The block must be `_Block_copy`d (libui has no unregister API; a stack block would dangle) and any captured-and-mutated variable must be `__block`. |
| `06_protocol` | A class conforming to a protocol gets vtable slots for the protocol's required methods **even when the class does not redeclare them**. This needed a fix: the required methods used to be dropped after binding, so a header-only TU compiled a layout with fewer slots than the implementation TU. |
| `07_arc` | Compile-time ARC insertion stays correct when the halves were transpiled separately — including a convenience-style `+1`-free return from the lib TU. |
| `08_class_method` | `+` methods dispatch through `GALD_META_VTABLE_$_X`, a *second* per-class layout that must agree across the link independently of the instance vtable. |
| `09_layout_mismatch` | **Negative case.** The lib `@implementation` adds a method the shared `.gh` never declares, so the two layouts differ. Must fail loudly and mention the diagnosis — the contract is "fails legibly", not merely "fails", so a case that crashed for an unrelated reason does not pass by accident. Marked with `EXPECT_FAIL` + `EXPECT_FAIL_MATCH`. |

## Writing a case

```
NN_name/
  model.gh      shared declarations — the only channel between the TUs
  lib.gm        "library" TU: defines the classes
  main.gm       "client" TU: sees only model.gh, has main()
  expected.txt  exact stdout the program must print (optional)
```

Notes that cost time when the cases were first written:

- Ivars belong on the `@interface`. An `@implementation` ivar block is not merged
  into the class layout, so the methods referencing them fail to compile.
- `id` is a lexer keyword — `- (int)id;` does not parse. Name it `widgetId`.
- ARC rejects a bare `[x autorelease]`. Use `@noarc { }`, or have the factory
  method return `+1` and let the caller release.
- Most cases need `#import <Foundation/Foundation.gh>`: the generated C gets
  `NFObject` / `SEL` / `GALD_CLASS_$_*` from the C headers Foundation inlines.

## Not covered

- Categories split across TUs.
- Protocol *inheritance* (`@protocol A <B>`) across TUs.
- Generic monomorphisation triggered only from a class-method return type.
