# AGENTS.md — Nopa Language Project

> **Read first:** `doc/architecture.md` is the single source of truth for the current
> architecture (pipeline, `.nh`/`.np` semantics, Foundation modes, owner/strong/weak
> metadata, vtable rules, EH backends). This file covers layout, commands, and the
> rules code changes must follow.
>
> **History:** detailed implementation logs and decision records live in
> `doc/stable_slots_plan.md`, `doc/arc_intern_uaf.md`, and
> `doc/archive/agents-history-2026-10.md` (the pre-2026-10 version of this file,
> including the legacy C-era project map). Do not re-derive them here.

## Current Phase

Maintenance + incremental features on the Rust implementation. The legacy C
version (`transpiler/`) is **gone** — the compiler is entirely Rust under `crates/`.

## Project Layout

```text
nopa-lang/
├── crates/                  # Rust workspace — the compiler
│   ├── lexer/ parser/ cst/ cst_visit/ elaborator/ binder/   # front end
│   ├── checker/             # type checker (default on)
│   ├── codegen/             # AST → C99
│   ├── arc/ eh/ async/ defer/ pattern/ trace/               # lowering passes (pipeline order)
│   ├── ownership/ preprocessor/ cpp/ symbol/ ast/ attrs/ cfg/ layout/
│   └── nopac/               # CLI driver + pipeline (main.rs, pipeline.rs)
├── include/
│   ├── nopa/                # runtime.h / runtime.c / runtime_freestanding.c
│   └── Foundation/          # 9 classes, each as a .nh (decl) + .np (impl) pair,
│                            #   plus Foundation.nh (decl umbrella) and
│                            #   Foundation.np (self-contained umbrella)
├── tests/
│   ├── multi_tu/            # 12 cross-TU scenarios (vtable/metadata rules)
│   ├── eh_diff/ eh_matrix/ arc_order/ strong_metadata/ arc_intern/
│   ├── golden/              # .np + expected .out pairs
│   ├── stress/              # baremetal / hosted / interop (own runners)
│   └── *.np                 # individual integration programs
├── tools/build-foundation-lib.sh   # → target/foundation/libnopafoundation.a
├── test_all.py              # full integration suite (test_all.sh = parallel wrapper)
├── doc/                     # architecture.md (start here), plans, archive/
└── ROADMAP.md               # what is actually not done yet
```

## File Extension Convention

- **`.nh`** — Nopa header: declarations (`@interface`, `@protocol`, typedefs, structs).
  Inlined by the preprocessor via `#import`, never compiled directly.
- **`.np`** — Nopa implementation: `@implementation`, functions, `int main`.
  Two uses: compiled as its own translation unit (multi-TU mode), or inlined via
  `#import` (self-contained mode).
- `#import` of `.nh`/`.np` is a nopa import; `#include` of `.h`/`.c` passes through to
  the C compiler verbatim. `.nh` (not `.h`) avoids colliding with C/ObjC system headers.
- nopac always defines `__NOPA__`; headers shared with plain C compilers guard
  nopa-only syntax with `#ifdef __NOPA__` / `#else`.
- A `.nh` consumed in multi-TU mode must keep **full struct layouts** (ivar section) —
  Nopa is a C superset, so `@public` ivars, struct fields, and member function pointers
  require clients to see the layout. Do not treat opaque structs as the default rule.

## Build & Test Commands

```bash
cargo build                      # debug nopac → target/debug/nopac (test_all uses this)
cargo build --release
cargo test --workspace           # Rust unit tests

./test_all.sh -j4                # full integration suite (wraps test_all.py; NOPAC= overrides binary)
./tests/multi_tu/run_multi_tu.sh
./tests/eh_matrix/run_eh_matrix.sh
./tests/eh_diff/run_eh_diff.sh           # NOPAC= must be an absolute path
./tests/strong_metadata/run_strong_metadata_test.sh
./tests/arc_intern/run_arc_intern_test.sh

./tools/build-foundation-lib.sh  # build target/foundation/libnopafoundation.a once
```

## Naming Conventions (used across all docs)

- Project: **Nopa**; compiler binary: **nopac**; runtime lib: **libnopa.a**;
  Foundation library: **libnopafoundation.a**.
- Classes keep the NP prefix: `NPObject`, `NPString`, `NPArray`, …
- Mode names: **self-contained / unity mode** (`#import <Foundation/Foundation.np>`) and
  **precompiled Foundation / multi-TU mode** (`#import <Foundation/Foundation.nh>` +
  auto-linked `libnopafoundation.a`) — the recommended mode for real projects.
- TU roles: **owner TU** (its main file holds the class's `@implementation`) and
  **declaration-only client** (imports `.nh` only). Historical synonyms
  (`decl-only`, `pure header`, `P3 header`) are deprecated.

## Multi-TU / Metadata / Vtable Constraints (iron laws)

1. **Owner rule (R2):** the TU whose **main file** holds `@implementation X` emits
   X's metadata (`NOPA_VTABLE_$_X`, `NOPA_CLASS_$_X`, getClass, …) as **strong**
   symbols; declaration-only clients emit **weak stubs**. Two main-file
   implementations of the same class fail at link time with a duplicate symbol —
   by design. `-fstrong-metadata` **does not exist**; ownership is derived.
2. **vtable layout (R1):** `[public segment]` = methods declared in the expanded
   `#import` set (shared `.nh`), sorted; `[private segment]` = methods only in this
   TU's main file, appended at the end. Public-segment offsets are identical in all
   TUs. **Private methods only dispatch inside their owner TU** — cross-TU methods
   must be declared in the shared `.nh`.
3. **`__sig` (R3):** the startup layout-signature check covers the **public segment
   only**; private tail differences are legal. A mismatch aborts loudly (never
   silently misdispatches).
4. **`nopa_metaInit`** is an idempotent compat backfill for self-contained umbrella
   builds only; owner/multi-TU paths rely on statically initialized `NPClass`
   metadata and do **not** call it.
5. **Static dispatch:** there is no `objc_msgSend`. All message sends compile to
   static vtable indexing; selector symbols are stored with **every colon stripped**
   (`setObject:atIndex:` → `setObjectatIndex`) — any code that looks up a selector
   by name must strip all colons, not just a trailing one.
6. **Variadic methods:** the vtable fn-ptr cast must include `, ...` (signature-exact).
7. **File-level RawLine vs structs:** emitted in source order (Section 5 walks source
   order); otherwise `#pragma mark` / `_Pragma` placement silently breaks.
8. **EH default is `checked`** (flag + call-site guards, zero setjmp); `-eh legacy`
   (alias `-eh sjlj`) is the old setjmp/longjmp backend, kept as an explicit fallback.
9. **Generics:** compile-time monomorphization is the primary model; bare container
   spellings keep erased-compat behavior (warning, not error).

## Rules for Code Changes

- **C superset iron law:** Nopa must remain a C superset. Never break legal C or
  existing nopa syntax while adding checks; C-level warnings only add diagnostics,
  never change parsing paths.
- **Verify before claiming done:** judge "feature works / not implemented" with a
  minimal probe first (past "not implemented" verdicts were disproven by probes);
  before reporting success run the regression baselines (`cargo test --workspace`,
  `./test_all.sh`, `multi_tu`, plus the affected specialized suite). Never mark an
  unverified fix complete.
- **Debug via generated C:** first step for miscompiles is
  `nopac -rewrite-nopa file.np -o file.c` and reading the C. `-asm` must come after
  `run` on the command line.
- **Parser discipline:** a failed `consume()` must not advance (it would eat the next
  declaration's first token); avoid positional line-number surgery when editing
  `parser.rs` (stale line numbers cut files — match content or diff against git).
- **Do not resurrect deleted things:** `Foundation.decl.nh`, `make-decl-headers.sh`,
  and `-fstrong-metadata` are gone on purpose. `Foundation.nh` = declarations only,
  `Foundation.np` = self-contained umbrella.
- **auto-link gating:** nopac auto-links `libnopafoundation.a` only for
  declaration-only clients (source-level transitive `#import` scan decides); a TU
  that inlines Foundation implementations is skipped on purpose — do not "simplify"
  this to a generated-C text heuristic (two such heuristics were already falsified).
- **Destructive git ops need user approval.**
