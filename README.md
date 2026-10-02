[-> 中文](CHINESE.md)

<div align="center">
<img src="doc/assets/Nupa_avatar.svg" alt="Nupa_avatar" width="210">

# The Gald Programming Language

[**View Project Examples**](#project-examples)

[Overview](#overview) · [Why Gald?](#why-gald) · [Project Examples](#project-examples) · [Quick Start](#quick-start) · [Language Features](#language-features) · [New Features](#new-features) · [Compilation & CLI](#compilation--cli) · [Code Examples](#code-examples) · [Design Principles](#design-principles) · [Roadmap](#roadmap) · [FAQ](#faq)

</div>

---

> ⚠️Warning
> **The GitHub Releases page lags behind the source code.** The releases shown
> on the GitHub website are usually *older* than what's pushed to the `main`
> branch — I often forget to cut a new release after pushing new commits. If you
> want the latest features and fixes, **clone the repo and build from source**
> (see [Build](#build)); only use the downloaded releases if you prefer the
> stable, older snapshot.

---

## **Overview**

Gald is a **purely static** Objective-C dialect (C superset language). Gald source is transpiled to C99, then compiled to native machine code by Clang. No runtime message forwarding, no GC pauses, no JIT warm-up — all method dispatch, memory management, and polymorphism are resolved at compile time. It currently works — there are games and tools running in it. If you find it interesting, feel free to give it a try.

I don't intend to replace ObjC or Swift. I just miss ObjC's syntax and wanted to let it live again in a statically compiled world. ☺️

---

## Why Gald?

I simply like ObjC's message send syntax `[obj message]`. ObjC's runtime (`objc_msgSend`) is heavy, and I wanted to write ObjC-like code that compiles straight to C — so Gald was born: ObjC syntax compiled statically, no runtime dependency, generating clean C.

This is not a production-ready language. It's a toy, exploring the question: "what happens if you transpile ObjC into plain static C?"

### What it does

- Method calls → compile-time VTable offsets, no objc_msgSend
- Memory management → CFG static analysis, retain/release decided at compile time
- Output → human-readable C99, not compiler IR

### Design Goals

- **Fun**: that's the most important one
- **Readable**: generated C is meant to be read by humans
- **Lightweight**: just one small static runtime

---

## Project Examples

[![examples](https://img.shields.io/badge/examples-000?style=for-the-badge)](examples/)

`examples/`

| Project               | Description                                                                              | Run                            |
| --------------------- | ---------------------------------------------------------------------------------------- | ------------------------------ |
| **`04_soma-kernel/`** | Tiny 32‑bit i386 OS kernel (NASM + C + Gald), bare‑metal `-ffreestanding` mode                | `./run.sh` or `./run.sh --gui` |
| **`03_LibUI/`**       | GUI app via [libui-ng](https://github.com/libui-ng/libui-ng), all callbacks in pure Gald | `./run_libui.sh`               |
| **`02_ncurses/`**     | Terminal demos (`ncurses_demo`, `sysmon`) using `Terminal::Ncurses`                      | `make run`                     |
| **`01_JSONEditor/`**  | Multi‑file JSON editor with split‑screen terminal preview                                | `galdc run json_editor.gm`     |

---

## Quick Start

### Dependencies

- Clang (>= 14)
- Rust (stable, with cargo)
- Git

### Build

```bash
git clone https://github.com/3shine123/gald-lang.git
cd gald-lang
cargo build --release
```

### Install

The build automatically drops an `install.sh` (plus headers and `libgald.a`) next to the `galdc` binary. Install it to your system with:

```bash
# After building from source — the script lives next to the binary
cd target/release        # or target/debug if you ran a plain `cargo build`
./install.sh             # installs to /opt/gald by default
./install.sh /usr/local  # optional: pick a different prefix
```

This installs:

- **binary** → `<prefix>/bin/galdc`
- **static lib** → `<prefix>/lib/libgald.a`
- **headers** → `<prefix>/include/`
- **system headers** → `/usr/local/include/{Foundation,gald}/` (needs write permission; skip with `sudo` or pass a second arg like `./install.sh /opt/gald ~/include`)

The installer auto-detects your language (中文 / English).

Alternatively, download a prebuilt release archive (`gald-<platform>.tar.gz` or `.zip`) from the releases page, extract it, and run the `install.sh` inside:

```bash
tar xzf gald-x86_64-unknown-linux-musl.tar.gz
cd gald-x86_64-unknown-linux-musl
./install.sh
```

> **Tip:** with `galdc` on your PATH and system headers installed, `<gald/runtime.h>` and `<Foundation/...>` resolve automatically — no `-I include` needed.

### Compile a Gald Program

```bash
# Just output C code (auto-derives .gm → .c)
galdc -rewrite-gald hello.gm
galdc hello.gm -rewrite-gald               # flag works anywhere
galdc -rewrite-gald hello.gm -o out.c      # explicit path also works
# (--rewrite-gald double-dash form also accepted)

# Compile the transpiled C alone with Clang — two ways:
#   1) compile the runtime source directly
clang -I include -o hello hello.c include/gald/runtime.c
#   2) link the prebuilt libgald.a (lives next to the galdc binary)
clang -I include -o hello hello.c -Ltarget/release -lgald

# Output object file directly
galdc hello.gm -o hello.o                  # -c mode, no linking

# Compile to executable
galdc hello.gm -o hello_bin                # transpile + compile + link

# Compile + run
galdc run hello.gm
galdc run hello.gm -o hello_bin            # keep binary after run
galdc run hello.gm                          # auto-clean temp binary

# Show compilation warnings
galdc -v run hello.gm

# [!] Error: .c output without -rewrite-gald
galdc hello.gm -o hello.c   → Error: use -rewrite-gald to output C code

# [!] Error: no output method specified
galdc hello.gm              → Error: specify -o or -rewrite-gald
```

### Shell Completion (Tab autocomplete)

`galdc` ships with generated completion scripts for **zsh**, **bash** and **fish**, built with
[clap_complete](https://crates.io/crates/clap_complete). Regenerate them any time with:

```bash
galdc -gen-completions zsh > _galdc
galdc -gen-completions bash > galdc.bash
galdc -gen-completions fish > galdc.fish
```

The scripts are also copied into the install bundle (`share/galdc/completions/`) by `install.sh`.

**zsh** — add the directory to `fpath` before `compinit` runs:

```zsh
fpath=(/opt/gald/share/galdc/completions $fpath)
autoload -U compinit && compinit
```

**bash**:

```bash
source /opt/gald/share/galdc/completions/galdc.bash
```

**fish**:

```fish
source /opt/gald/share/galdc/completions/galdc.fish
```

After installing a new version, clear the zsh cache with `rm -f ~/.zcompdump*` and open a new terminal.

### Run Tests

```bash
# Run all tests
./test_all.sh -j4

# Run Rust unit tests
cargo test --workspace
```

---

## Language Features

### Class System

```gald
@interface Animal : NFObject {
@public
    NFString *_name;
}
- (instancetype)initWithName:(NFString *)name;
- (void)speak;
@property (readonly) NFString *name;
@end

@implementation Animal
- (instancetype)initWithName:(NFString *)name {
    self = [super init];
    if (self) {
        _name = name;
    }
    return self;
}
- (void)speak {
    printf("...\n");
}
@end
```

### Protocol

```gald
@protocol Drawable
- (void)draw;
- (BOOL)isVisible;
@end

@interface Shape : NFObject <Drawable>
@end
```

### Properties

```gald
@interface Person : NFObject
@property NFString *name;
@property int age;
@property (readonly) NFString *identifier;
@end
```

### Category

```gald
@interface Person (Printing)
- (void)printGreeting;
@end

@implementation Person (Printing)
- (void)printGreeting {
    printf("Hello, my name is %s\n", [self name]);
}
@end
```

### Block

```gald
int (^square)(int) = ^int(int x) {
    return x * x;
};

void (^logAndCall)(NFString *, void (^)(void)) = ^void(NFString *msg, void (^next)(void)) {
    printf("[LOG] %s\n", msg);
    if (next) next();
};
```

### @autoreleasepool

```gald
@autoreleasepool {
    NFString *temp = [NFString stringWithUTF8String:"hello"];
    // temp is released when the pool pops
}
```

### @selector

```gald
SEL sel = @selector(doSomething:);
```

### Full C Compatibility

```gald
#include <stdio.h>
#include <stdlib.h>

@interface Wrapper : NFObject
- (void)callCFunction;
@end
```

### C Attributes (__attribute__)

Gald supports `__attribute__((...))` pass-through. You can write C `__attribute__` on global declarations and struct fields, and the compiler preserves them verbatim in the generated C output.

```gald
__attribute__((packed))
struct Point {
    int x;
    int y;
};

__attribute__((format(printf, 1, 2)))
int my_log(const char *fmt, ...);
```

The compiler ships with a **590-attribute classification table** (scraped from Clang and GCC official docs). The `-backend` option controls which attributes are allowed:

| Option                        | Behavior                                                                            |
| ----------------------------- | ----------------------------------------------------------------------------------- |
| `-backend=clang` (default)    | Allow clang-specific attributes (e.g. `availability`, `diagnose_if`, `objc_direct`); gcc-only attributes error |
| `-backend=portable`           | Only attributes supported by both gcc and clang; others error                        |
| `-backend=gcc`                | Allow gcc-specific attributes (e.g. `strub`, `optimize`, `stack_protect`)            |

Unknown attributes (not in the table) produce a warning and pass through — never a hard error.

### Memory Management

Gald uses **compile-time static ARC**. The compiler determines each object reference's lifetime through CFG dataflow analysis and inserts retain/release calls automatically. No manual `retain`/`release`/`autorelease` needed.

In MRC mode (`-fno-gald-arc`):

```gald
NFObject *obj = [[NFObject alloc] init];
// ... use obj ...
[obj release]; // MRC manual release
```

### C Bridge (`-emit-bridge-header`)

Gald transpiles to C, but calling Gald object methods from C normally requires verbose vtable-index and SEL-constant boilerplate. `-emit-bridge-header` generates a header with `static inline` wrappers for every method, so C code can call Gald objects like ordinary C functions.

**Usage**: transpile a Gald library to C, then generate the bridge header:

```bash
galdc -rewrite-gald lib.gm -o lib.c -emit-bridge-header lib.h
```

Then include the bridge header from C:

```c
#include "lib.h"

int main(void) {
    gald_metaInit();  // required before using any Gald objects

    // Class method: gald_<Class>_<method>(params...)
    NFString *s = gald_NFString_stringWithUTF8String_("Hello");

    // Instance method: gald_<Class>_<method>(self, params...)
    size_t len = gald_NFString_length(s);
    const char *cstr = gald_NFString_UTF8String(s);

    // Nested message send (like Gald's [[s UTF8String] ...])
    const char *nested = gald_NFString_UTF8String(
        gald_NFString_stringWithUTF8String_("nested")
    );

    // Multi-argument message send (like Gald's [arr replaceObjectAtIndex:0 withObject:obj])
    NFArray *arr = gald_NFArray_arrayWithObject_(s);
    gald_NFArray_replaceObjectAtIndex_withObject_(arr, 0, s);

    // Each colon in the selector becomes an underscore in the function name:
    //   [obj foo:arg1 bar:arg2] → gald_<Class>_foo_bar_(obj, arg1, arg2)
    //   [m replaceCharactersInRange:rng withString:str]
    //   → gald_NFMutableString_replaceCharactersInRange_withString_(m, rng, str)
}
```

Link against the transpiled `.c` and `runtime.c`:

```bash
clang caller.c lib.c include/gald/runtime.c -I include -o app
```

⚠️ The caller's `main` must call `gald_metaInit()` first. The bridge header uses `sel_registerName` to resolve selectors at runtime, so it does **not** depend on the codegen-generated `static const` SEL constants (which are file-local and invisible across translation units).

#### Memory Management from C

Gald's **ARC is compile-time and applies only to `.gm` source** — it never sees calls coming from C. When C code calls bridge functions, objects are **not** automatically retained or released. Manage them manually, following the ObjC memory-management naming convention:

| Method family                                  | Caller owns?       | What C code must do                                                             |
| ---------------------------------------------- | ------------------ | ------------------------------------------------------------------------------- |
| `alloc`, `new`, `copy`, `mutableCopy`          | ✅ +1               | Must call `gald_release(obj)` when done                                         |
| `init`                                         | ❌ consumes `alloc` | Nothing                                                                         |
| everything else (e.g. `stringWithUTF8String:`) | ❌ autoreleased     | Nothing, but `gald_retain(obj)` if it must outlive the current autorelease pool |

```c
#include "lib.h"

int main(void) {
    gald_metaInit();
    gald_autoreleasepool_t *pool = gald_autoreleasepoolPush();

    // +1 (returns autoreleased convenience object); use within this pool only
    NFString *s = gald_NFString_stringWithUTF8String_("hello");
    printf("%s\n", gald_NFString_UTF8String(s));

    // If it must outlive the pool: retain now, release later
    NFString *t = gald_NFString_stringWithUTF8String_("world");
    gald_retain(t);
    gald_autoreleasepoolPop(pool);      // t survives (was retained)
    printf("%s\n", gald_NFString_UTF8String(t));
    gald_release(t);

    // alloc-family returns +1 → must release
    NFString *u = gald_NFString_alloc(gald_NFString_stringWithUTF8String_("x") /* placeholder */);
    // (real usage: gald_NFString_copy(s) returns +1, release it)
    NFString *copy = gald_NFString_copy(s);
    gald_release(copy);
}
```

`gald_retain`, `gald_release`, `gald_autorelease`, `gald_autoreleasepoolPush`/`gald_autoreleasepoolPop` are declared in `<gald/runtime.h>` and work on any Gald object. This is exactly the manual-retain-count (MRC) model — from the C side you can think of Gald objects as raw pointers you own or don't own by convention.

---

## New Features

Gald adds features on top of Objective-C syntax that ObjC itself doesn't have.

**Recent highlights:**

- **Native bare-metal support (`-ffreestanding`)** — compiles to self-contained C with no libc, no Foundation, no TLS; `@try/@catch` uses `__builtin_setjmp/longjmp`, and a zero-boilerplate `runtime_freestanding.c` provides the bump allocator, `GALD_CLASS_$_gald_root`, exception state, and `memcpy`.
- **C superset** — `@protocol` + conformance, `@property` + `@synthesize`, `instancetype`, `@public` ivars, dot syntax, structs + function pointers, inline asm, C-style casts.
- **Typed `@catch`** — catch arms match via `__gald_eh_isa` (isKindOf: superclass-chain semantics, like ObjC): a parent-class arm catches subclass instances, and the first matching arm consumes the exception so later arms never double-catch.
- **ARC fixes** — scope-stack model no longer releases parent-scope variables at nested scope end; `for`-init object hoisting stops leaks and invalid `for` headers.
- **`@noarc` block** — block-level MRC: in ARC mode, manual `retain`/`release`/`dealloc`/`autorelease` inside `@noarc { }` is allowed; the block-level analogue of `-fno-gald-arc` and clang's `-fno-objc-arc`.
- **`__attribute__` pass-through + `-backend`** — full support for C `__attribute__((...))` and all `__`-prefixed C predefined identifiers (`__FILE__`, `__LINE__`, `__builtin_*`, `__extension__`, `__typeof__`, `__alignof__`, ...); the `-backend` flag controls which compiler-specific attributes are allowed.

### for-in Enumeration

```gald
for (NFString *s in arr) {
    printf("%s\n", [s UTF8String]);
}
```

Desugared at parse time into an index loop over `[coll count]` / `[coll objectAtIndex:]` — the collection expression is evaluated once, nil-safe (`[nil count]` is 0), and elements are borrowed (no retain/release). Plain C arrays and `NFArray` both work.

### Protocol Conformance Checking

The checker now verifies that a class declaring `<Proto>` implements every required method (recursively through parent protocols; `@optional` methods are exempt):

```
class 'Circle' does not implement required method 'draw' from protocol 'Drawable'
```

### Protocol Composition (`P & Q`)

Reuse C's `&` operator to require several protocols at once — no new syntax:

```gald
// ① Intersection type: the receiver must implement both
void render(id<Drawable & Serializable> item);

// ② Composed protocol declaration
@protocol Renderable <Drawable & Serializable>
@end
```

Protocol types stay compile-time constraint labels only — vtable slots are unaffected, so multi-TU layout is unchanged.

### Foundation Type Dispatch (`isKindOfClass:` / `respondsToSelector:` / `isEqual:`)

The official ObjC spellings are now implemented on the root class, enabling idiomatic multi-way dispatch without any new language construct:

```gald
for (id item in items) {
    if ([item isKindOfClass:[Dog class]]) {
        [(Dog *)item bark];
    } else if ([item respondsToSelector:@selector(draw)]) {
        [(id<Drawable>)item draw];
    }
}
```

`isKindOfClass:` walks the isa chain, `respondsToSelector:` queries the unified vtable, and `isEqual:` defaults to pointer identity on the root class (matching `NSObject`) — while `NFString` and `NFNumber` override it with **value** equality, which is what makes dictionary keys work. The legacy `isKindOf:` remains as a compatibility alias.

### Struct `==` / `!=` Value Comparison

C rejects `a == b` on structs outright; Gald reuses the existing operators and desugars to a generated field-by-field compare function:

```gald
struct Point a = {1, 2};
struct Point b = {1, 2};

if (a == b) { ... }   // field-wise compare
if (a != b) { ... }

p == &a               // pointer comparison semantics unchanged
```

### async/await (`@await`)

A method whose body contains `@await` is async — no annotation needed, mirroring C++20's `co_await`-based coroutines (the declaration looks like a perfectly ordinary ObjC method, so vtable layout is unchanged):

```gald
@interface Fetcher : NFObject
- (int)compute:(int)n;
- (void)runAll;
@end

@implementation Fetcher
- (int)compute:(int)n {
    int raw = @await n;               // suspension point
    return raw * 2;
}

// async void = the entry method (blocks and pumps to completion)
- (void)runAll {
    int x = @await [self compute:21]; // awaiting a call infects this method too
    NFLog(@"result=%d", x);
}
@end

int main() {
    Fetcher *f = [[Fetcher alloc] init];
    [f runAll];                       // async void is callable from sync context
    return 0;
}
```

Design rules:

- **Infection is chain-based** — a method calling `@await` becomes async itself; async methods with a return value may only be awaited from async contexts (compile-time rejected otherwise).
- **`@await` lowers to a state machine** — the body is split at suspension points into a `switch(task->state)` driver over a heap `NFTask`; locals that survive a suspension are lifted into a per-method frame struct.
- **`@try` spanning an `@await`** is rejected (a `jmp_buf` cannot survive a suspension point); `@noarc` across awaits is allowed; break/continue across awaits become state jumps.
- A cooperative single-thread scheduler (`gald_run_all`) and I/O integration are planned as the next milestone.

### Switch Pattern Matching (`case` patterns)

`case` labels accept **patterns**, not just integer constants. Type dispatch stays a method chain in spirit — the patterns desugar to `isKindOfClass:` / `isEqual:` / comparisons — but you write them declaratively:

```gald
// Object patterns mix freely in one switch:
switch (subject) {
    case NFString *s:                      // type binding → isKindOfClass:
        NFLog(@"string: %s", [s UTF8String]);
        break;
    case NFNumber *n when [n intValue] > 3:  // type binding + `when` guard
        NFLog(@"number: %d", [n intValue]);
        break;
    case @"literal":                       // object literal → isEqual: (value semantics)
        NFLog(@"matched a literal");
        break;
    default:
        break;
}

// Comparison patterns, on a SCALAR subject:
switch (n) {
    case > 100:
        NFLog(@"big");
        break;
    case > 0 && < 100:                     // range
        NFLog(@"small");
        break;
    default:
        break;
}

// Plain multi-value constants stay plain C:
switch (n) {
    case 1, 2, 3:
        NFLog(@"one of 1-3");
        break;
    default:
        break;
}
```

| pattern | lowers to |
|---------|-----------|
| `T *name` | `gald_isKindOfClass(subject, &GALD_CLASS_$_T)`; inside the arm, `name` is already bound to `(T *)subject` |
| `> 10`, `< 10`, `>= 0`, `<= 9` | `subject > 10` (the subject is spliced into the dangling operand) |
| `> 0 && < 100` | `subject > 0 && subject < 100` |
| `@"lit"`, `@42`, `@YES`, `@'c'`, `@(expr)` | `[subject isEqual:<literal>]` — value semantics, so `@"lit"` matches a *different* NFString with the same contents |
| `T *x when <expr>` | the type test, `&&`-ed with the guard |
| anything else | plain C constant, compared with `==` |

`when` is a **contextual keyword** — `int when = 1;` still compiles. Arms are tested top to bottom and the first match wins; `break` (or falling off the end) leaves the switch. **Fallthrough follows C**: an arm without `break` runs into the next arm's body.

`case` labels that could never be a C constant — `case [obj msg]:`, `case f():`, `case a = b:` — are rejected with `case label is not a constant expression` instead of leaking into the generated C, where the error used to point at generated code with no source correspondence.

M1 limits, all diagnosed rather than silently miscompiled:

- **Constant arms can't mix with pattern arms** — the C path needs the original body, the pattern path discards it. Split them into separate switches.
- **A dangling comparison can't mix with a type-binding / object-literal arm**, and **can't be used on a syntactically-object subject** (`switch (@7) { case > 100: ... }`). Both would make the subject an object, so `subject > 100` becomes a *pointer* compare — always true, silently. Switch on a scalar instead (`switch ([o intValue])`).
- Residual M1 gap: a *variable* subject holding an object (`id o = @7; switch (o) { case > 100: ... }`) still degenerates — deciding it needs type information the parser does not have.
- Nested patterns get no cross-level `case` scoping (an inner `case` belongs to the inner switch, same as C).

Implementation: the parser classifies each label and flattens the whole switch into a single arm list; a new lowering pass rewrites that into a `goto`/`if` chain with C labels before the checker runs — so the generated C stays plain C99 and there is no new IR. Golden: `tests/golden/42_switch_pat/`.

### Boxed Literals (`@(expr)` / `@YES` / `@NO` / `@'c'`)

```gald
NFNumber *a = @123;          // int
NFNumber *b = @1.5;          // double
NFNumber *c = @YES;          // BOOL → 1
NFNumber *d = @'c';          // char
NFNumber *e = @(i + 1);      // factory chosen by the operand's STATIC type
```

Every form yields a real `NFNumber`: the literal's own type picks the factory (`numberWithInt:` / `numberWithDouble:` / `numberWithChar:`), and `@(expr)` picks by the operand's static type — `double`/`float` → `numberWithDouble:`, `BOOL` → `numberWithBool:`, `char` → `numberWithChar:`, `long`/`long long` → `numberWithLongLong:`, any other integer → `numberWithInt:`. Boxed results are ordinary objects, so they dispatch like anything else: `[@(i * 2) intValue]`.

`@(expr)` is rewritten in the **checker**, not the parser: the parser has no types, and the C99 backend has no `_Generic` to fall back on. The rewrite reuses ordinary message-send nodes, so static dispatch and the nil guard come for free — the same mechanism as object subscripts and struct `==`. Non-arithmetic operands are rejected rather than silently boxed:

```
illegal type 'NFString *' in a boxed expression — '@(...)' accepts arithmetic and BOOL values only
```

### Dictionary Literals (`@{ key: value }`)

```gald
NFDictionary *d = @{ @"a": @1, @"b": @2, @"c": @3 };
NFLog(@"%d", [[d objectForKey:@"b"] intValue]);   // 2
printf("%lu\n", (unsigned long)[d count]);        // 3

NFMutableDictionary *m = [NFMutableDictionary dictionary];
[m setObject:@10 forKey:@"x"];
[m setObject:@11 forKey:@"x"];   // equal key → replaced, not appended
[m removeObjectForKey:@"x"];

NFDictionary *empty = @{};       // `@{}` is an empty dictionary (`@[]` is the array)
```

Keys compare with `isEqual:`, so `NFString`/`NFNumber` keys have **value** semantics. String literals are **interned** (same contents → the same object, like ObjC constant strings), and value equality remains the semantic guarantee — a lookup with a fresh `@"b"` finds the entry either way. Storage mirrors `NFArray` — two parallel object arrays with a linear scan — and `count` / `objectForKey:` / `allKeys` / `allValues` / `copy` / `description` / content-based `isEqual:` round out the API. Entries must be objects, as in ObjC:

```
illegal type 'int' in a dictionary literal — keys and values must be Objective-C objects
```

### Exception Semantics (`-eh checked` — the default backend)

Gald's exceptions are **ObjC exceptions by value, without unwinding**. `@try`/`@catch`/`@finally`/`@throw` behave exactly like clang's `-fobjc-arc-exceptions` mode — and a differential test suite (`tests/eh_diff/run_eh_diff.sh`) locks this in by running each case under both galdc and real clang/ObjC, then diffing stderr line by line (7/7 cases pass).

```gald
@interface Boom : NFObject
- (void)fire;
@end

@implementation Boom
- (void)fire {
    @throw @"negative input";
}
@end

int main() {
    @try {
        Boom *b = [[Boom alloc] init];
        [b fire];                       // execution stops HERE
        NFLog(@"never runs");
    }
    @catch (NFString *e) {
        NFLog(@"caught: %@", e);
    }
    @finally {
        NFLog(@"finally always runs");
    }
    return 0;
}
```

The semantics you get:

- **ARC settles every frame** — an exception that crosses frames releases each frame's owned locals on the way out. There is no `longjmp`, so nothing is skipped and nothing leaks.
- **A throw interrupts immediately** — the rest of the statement never evaluates: `x = [a risky] + [b sideEffect];` never runs `sideEffect`.
- **Typed catch chains match by isa** — an unmatched `@catch` lets the exception continue to the enclosing `@try`; a rethrow inside `@catch` propagates to the outer handler, never re-enters the same one.
- **`@finally` ordering** — inner finally runs before the outer catch; the outer finally runs after the outer catch.
- **Throws inside block literals** propagate to the enclosing `@try` like any other call.
- **Uncaught exceptions abort** with ObjC's wording: `*** Terminating app due to uncaught exception of class 'NFString'`, exit code 1.
- **C callers can't miss an exception** — bridge-header wrappers check the error flag and abort rather than silently returning a zero value.

**`-eh checked` is the default backend** — a plain `galdc run` compiles with it. `-eh legacy` (alias `-eh sjlj`) selects the old zero-overhead setjmp backend and remains a complete rollback; that backend has the classic limitation: a cross-frame throw skips intermediate frames' cleanup (documented below).

### `@throws` — Declared Exceptions

`@throw` and `@throws` differ by one letter and mean completely different things — the split Java draws between `throw` (a statement) and `throws` (a declaration clause).

| | `@throw` | `@throws` |
|--|----------|-----------|
| What | **statement** — raises an exception at runtime | **declaration annotation** — compile-time metadata |
| Where | inside a body | trailing, before the `;` or `{` of a declaration |
| Shape | `@throw expr;` | `@throws(T *)` or bare `@throws` |
| In generated C | yes (the setjmp/flag machinery) | **never** — no code, no vtable slot |

```gald
@interface Repo : NFObject
- (NFString *)fetch:(const char *)url @throws(NFError *);   // throws NFError *
- (int)parse:(const char *)s @throws;                       // throws; type unstated
- (int)count;                                              // never throws
@end

@implementation Repo
- (NFString *)fetch:(const char *)url @throws(NFError *) {
    if (!url) {
        @throw [[NFError alloc] init];   // the statement
    }
    return @"ok";
}
@end
```

Apple has occupied exactly this slot — trailing metadata before the `;` — with macros for over a decade (`NS_DESIGNATED_INITIALIZER`, `NS_REQUIRES_NIL_TERMINATION`, `API_AVAILABLE(...)`). Gald promotes the slot to first-class syntax and lets the checker reconcile it.

**What the checker enforces**

- `@throws(T *)` — every `@throw` that escapes the declaration must have a static type compatible with `T` (subclasses allowed):
  `error: '@throw' of type 'AppError *' does not match the declared '@throws(NFString *)'`
- Bare `@throws` — the body must really contain an escaping `@throw`:
  `error: 'liar' is marked '@throws' but its body never executes '@throw'`
- No annotation — an escaping `@throw` is an error:
  `error: '@throw' escapes 'bad' without a '@throws' annotation; declare it with '@throws(<type>)' or handle it with a local '@try'`

A `@throw` caught by a `@try` **in the same body** is never an escape, so `main` and locally-guarded helpers need no annotation:

```gald
static void bad(int n) {                        // error: escapes 'bad'
    if (n < 0) {
        @throw [[AppError alloc] init];
    }
}

static void good(int n) @throws(AppError *) {   // declared — fine
    if (n < 0) {
        @throw [[AppError alloc] init];
    }
}

static void guarded(int n) {                    // fine — caught locally
    @try {
        if (n < 0) {
            @throw [[AppError alloc] init];
        }
    }
    @catch (AppError *e) {
    }
}
```

The type check is deliberately conservative: `@"..."` literals, bare C strings, casts, and variables of known type are judged; a message send is not (its class is not knowable from a selector-only registry), so it satisfies any declared type. Annotations are compile-time only — adding or removing `@throws` never changes generated C, program output, or ARC behaviour. Misusing the pair is itself an error: `@throws` inside a body, or `@throw(...)` on a declaration, each gets a diagnostic naming the other keyword.

### Implicit Root Class (`gald_root`)

Gald now supports user-defined root classes. You no longer need to inherit from `NFObject` — an `@interface` without a superclass automatically gets a compiler-injected implicit root class `gald_root`, while keeping `id` type uniformity and static dispatch.

**Before:**

```gald
@interface Animal : NFObject   // had to inherit NFObject
```

**After:**

```gald
@interface Animal              // no superclass → implicit root class
@interface Animal : NFObject   // explicit NFObject still works
```

Both are valid, and `id` can point to any Gald object.

#### How It Works

When no superclass is specified, the compiler injects `gald_root`:

```gald
// User code:
@interface Animal {
    int age;
}
- (void)speak;
@end

// Compiler treats as:
@interface Animal : gald_root {
    int age;
}
- (void)speak;
@end
```

Generated C code:

```c
// Built-in structures
struct gald_object_header {
    struct gald_vtable *vtable;
};

struct gald_root {
    struct gald_object_header header;
};

// Animal's struct
struct Animal {
    struct gald_root __super;  // contains header
    int age;
};
```

#### `id` Type

```c
typedef struct gald_root *gald_id_t;
```

`id` is no longer tied to `NFObject` — it only requires the object to start with `gald_root`. This means:

```gald
Animal *a = [[Animal alloc] init];
id obj = a;                    // valid: Animal inherits from gald_root
[obj speak];                   // static dispatch: obj->header.vtable[...]
```

#### Explicit Inheritance Still Works

```gald
@interface Dog : Animal {
    NFString *breed;
}
@end
```

Generated C:

```c
struct Dog {
    struct Animal __super;     // contains gald_root → header
    struct NFString *breed;
};
```

#### `NFObject` vs `gald_root`

| Declaration                 | Means                             | Use Case                        |
| --------------------------- | --------------------------------- | ------------------------------- |
| `@interface Xxx`            | Implicit `gald_root`, lightweight | Custom layout, kernel, embedded |
| `@interface Xxx : NFObject` | Explicit NFObject, full runtime   | User apps, ARC, retain/release  |

```gald
// Lightweight root class, no refcounting overhead
@interface KernelTask {
    int pid;
    int priority;
}
- (void)run;
@end

// Full NFObject with automatic memory management
@interface UserModel : NFObject
@property NFString *name;
@end
```

#### Bare-Metal / Freestanding Support (`-ffreestanding`)

Gald can compile to **self-contained C with no libc, no Foundation, no TLS**, for kernels, MCUs, and bare-metal embedded development.

```bash
galdc -rewrite-gald -ffreestanding kernel.gm   # emits self-contained C
```

In `-ffreestanding` mode the transpiled C:

- does **not** `#include <string.h>`; instead `#include <gald/runtime.h>` (freestanding branch)
- implements `@try/@catch/@finally` with the default `-eh checked` backend — plain flag + guard control flow, **no `setjmp`/`longjmp` and no `jmp_buf` at all**, which is what makes the bare-metal target work. (`-eh legacy` falls back to `__builtin_setjmp/longjmp`, with plain non-`__thread` exception globals.)
- is self-contained for `SEL`/`NFClass`/`NFObject`/`id`
- does **not** bundle the Clang Blocks runtime — block literals reference `__NSConcreteStackBlock`/`_Block_copy`/`_Block_release`; on real bare metal, either link a Blocks runtime port or use `-backend portable`/`-backend gcc` (blocks lower to plain C functions, no ABI symbols)

The user only provides: `GALD_CLASS_$_gald_root`, the exception globals (if using `@try`), `memcpy` (if using `@try`), and freestanding headers (`stdint.h`/`stddef.h`/`stdbool.h`).

**Bare-metal allocator + `[[Class alloc] init]`** (`include/gald/runtime_freestanding.c`):

```gald
@interface HeapCounter {
    int total;
}
+ (id) alloc;
- (id) init;
- (int) add:(int)x;
@end
@implementation HeapCounter
+ (id) alloc  { return gald_alloc(self); }   // bump allocator
- (id) init   { return self; }
- (int) add:(int)x { total += x; return total; }
@end

void demo(void) {
    HeapCounter *c = [[HeapCounter alloc] init];  // bare-metal heap alloc
    [c add:10];                                    // → 10
}
```

Features verified bare-metal (`examples/04_soma-kernel/` i386 protected-mode kernel + `tests/golden/25_freestanding/`):

- `@namespace` + `@interface` (implicit root class)
- Class / instance method messaging
- `@try/@catch/@finally`
- `@selector`, inline asm, C-style casts
- `[[Class alloc] init]` heap allocation + ARC auto-`gald_release`

Sample output (soma-kernel under qemu):

```
[gald] class method [SomaCore::Calculator compute:21] = 43
[gald] instance methods on C-created obj: add:7 -> 7, add:35 -> 42, value = 42
[gald] @try/@catch demo:
       try body, throwing...
       caught [e errorCode] = 42
       finally always runs
       after-try continues
[gald] alloc+init (bump allocator):
       [c add:10]=10 [c add:20]=30 [c value]=30
```

#### Method Dispatch

All Gald objects dispatch through a unified VTable mechanism:

```c
// [obj doSomething:arg]
obj->header.vtable[INDEX_doSomething](obj, arg);
```

The compiler assigns a fixed global index to each selector. All classes place the function pointer for the same selector at the same VTable position. If a class doesn't implement a method, the slot holds the parent's implementation or NULL.

#### Kernel-Friendly Design

The object header is minimal:

```c
struct gald_object_header {
    struct gald_vtable *vtable;
    // no retain count, no flags
};
```

Reference counting is managed by compile-time static ARC analysis, not stored in the object. `gald_id_t` is a plain C pointer (8 bytes on 64-bit), zero ABI overhead for passing, assigning, and array storage.

#### Status

   Implemented:

- [x] Implicit root class injection (semantic analysis)
- [x] `gald_root` and `gald_object_header` C code generation
- [x] `id` → `gald_id_t` type mapping
- [x] Unified VTable index allocation
- [x] Root/subclass struct generation
- [x] Unit test coverage

### @namespace

`@namespace` organizes classes, functions, and constants, avoiding global name collisions. This is a feature ObjC lacks — traditional ObjC relies on prefix conventions (e.g., `NS`, `UI`) to simulate namespacing.

```gald
@namespace Game
    @interface Player : NFObject {
        int health;
    }
    - (id)init;
    - (int)getHealth;
    @end

    @implementation Player
    - (id)init {
        self = [super init];
        if (self) health = 100;
        return self;
    }
    - (int)getHealth { return health; }
    @end
@endnamespace

@namespace UI
    @interface HUD : NFObject {}
    - (void)showPlayerHealth:(Game::Player *)player;
    @end
@endnamespace
```

**Encoding rules**: `::` separators are encoded as `__` in C symbols.

| Gald Symbol                   | Transpiled C Symbol          |
| ----------------------------- | ---------------------------- |
| `Game::Player`                | `Game__Player`               |
| `Game::Entities::Enemy`       | `Game__Entities__Enemy`      |
| Method `-[Game::Player init]` | `Game__Player_init`          |
| VTable                        | `GALD_VTABLE_$_Game__Player` |
| Class metadata                | `GALD_CLASS_$_Game__Player`  |

**Features**:

- Nested namespaces supported (`Game::Entities::Enemy`)
- Cross-namespace references (`Game::Player *player`)
- `@class` forward declarations inside namespaces (`@class Player;`) — codegen emits `struct Game__Player;` + `typedef struct Game__Player Game__Player;` so forward-declared types are usable in method signatures
- Cross-namespace inheritance (`@interface HUD : Engine::Graphics::Renderable`)
- No prefix convention needed — C symbols are encoded automatically
- Classes without namespaces remain backward-compatible

#### @using Import Mechanism

`@using` imports symbols from other namespaces into the current scope, avoiding the need to write fully qualified names each time. Three forms are supported:

**Form 1: Import a fully qualified name**

```gald
@using Game::Player;
Game::Player *p = [[Game::Player alloc] init];
// After @using, the short name Player can be used instead
Player *p = [[Player alloc] init];
```

**Form 2: Import with an alias**

```gald
@using GP = Game::Player;
// GP is an alias for Game::Player
GP *p = [[GP alloc] init];
```

**Form 3: Import an entire namespace**

```gald
@using namespace Game;
// All classes under Game can be accessed by short name
Player *p = [[Player alloc] init];
Enemy *e = [[Enemy alloc] init];
```

**Conflict detection**:

- If a short name conflicts with an existing symbol in the current scope, the compiler reports an error
- If two `@using` entries import the same short name, the compiler reports an ambiguity error
- Aliases and short names are valid within the file scope of the `@using` declaration

### `@noarc` Block — Block-level MRC

In ARC mode, the checker forbids manual memory management:

```gald
[obj release]; // error: explicit 'release' not allowed in ARC mode
```

`@noarc { }` scopes a block where you manage memory manually — the block-level analogue of `-fno-gald-arc` (and clang's `-fno-objc-arc`):

```gald
@noarc {
    [obj retain];
    [obj release];
    [obj autorelease];
}
```

Key points:

- **Block-level scope** — only statements inside `@noarc { }` are exempt. Everything outside still uses static ARC, and manual `retain`/`release`/`dealloc`/`autorelease` outside the block is a compile error.
- **No ARC injection** — the ARC analyzer skips `@noarc` blocks entirely, inserting no retain/release for objects used there.
- **Runtime-method exemption** — the implementations of `retain`/`release`/`dealloc`/`autorelease` themselves may call these methods without `@noarc`.
- **Whole-program analogue** — `-fno-gald-arc` switches the whole program to MRC; `@noarc` does the same for a single block.
- **Foundation** — the NFString/NFMutableString convenience constructors (`+stringWithUTF8String:`, `+stringWithString:`) wrap their deliberate `autorelease` in `@noarc { }`.

---

### Refcount Trace (`-trace-refcount`)

`-trace-refcount` runs a static reference-count simulator over the AST **after** ARC injection, printing a chronological, color-coded trace of every retained object's count, then exits without codegen or compilation. It is a debug aid for verifying that each object is released exactly once (no leaks, no double-releases).

```bash
galdc -trace-refcount app.gm                          # color trace
galdc -trace-refcount -trace-no-color -trace-max-iters 2 app.gm
```

Options:

- **`-trace-no-color`** — disable ANSI colors (for diffs / CI piping).
- **`-trace-max-iters <N>`** — how many loop iterations each loop simulates (default 2); every iteration gets fresh `Cat#N` object identities.
- Colors: **green** = count increased · **blue** = count decreased (still alive) · **cyan** = freed (reached 0) · **red** = double-release/over-release and `possible leak`/`over-released` in the summary · **yellow** = informational untracked-target warnings.
- Object identity = allocation site (`Class#N` per class); creations are `alloc`/`new`/`copy`/`mutableCopy`-prefixed, `init` chains to its receiver, and `@"..."`/`@[...]` literals.
- Objects that leave the traced scope are excluded from the leak summary: `return`/`@throw` results, `static` singletons, `@"..."`/`@[...]` literals, and method parameters. A final `== Summary ==` reports live (`possible leak`), over-released (negative count), and freed objects with their allocation positions.

### `@defer` — Scope-Exit Execution

Go-style deferred cleanup: `@defer { ... }` registers its body with the innermost enclosing block, and the body runs at **every exit** of that block — the natural end, a `return` at any depth, a `break`/`continue` that jumps out of it, and a same-function `@throw` — innermost first (LIFO).

```gald
- (void)work {
    FILE *f = fopen("cfg.txt", "r");
    @defer { fclose(f); }            // runs at every exit below
    if (!ready) { return; }          // defer runs before returning
    @defer { printf("second\n"); }   // multiple defers: LIFO
    ...
}                                    // block end: "second" first, then fclose
```

Semantics:

- **Loop bodies** re-run their defers **every iteration** — and on `continue`/`break`.
- `break`/`continue` fire only the defers registered between the jump and the innermost loop/switch; defers registered **outside** the loop are not double-fired.
- Variables are read directly — no capture, no copy (plain C scoping, deliberately unlike blocks).
- **ARC order**: user defers run *before* the ARC-injected scope-end release, so objects are still alive inside your defer (`dealloc` prints last).
- `-eh checked` needs no special case: its throws are ordinary returns by the time the defer pass runs.

M1 limits (compile-time enforced): `@defer` must sit directly inside a block; the body must not contain `return`/`break`/`continue`/`@throw` (`error: 'return' inside an '@defer' body is not supported (M1)`). A cross-function `@throw` (sjlj longjmp past intermediate frames) skips those frames' defers — the same documented limitation as ARC's scope-end releases, gone under `-eh checked`.

Implementation: pure desugar (`crates/defer`, pipeline step 3.9 — after the `-eh checked` rewrite, before ARC). Codegen, checker, and the runtime see ordinary statements — zero changes downstream. Golden: `tests/golden/36_defer/`.

### `NFAsync<T>` — Declared Async Marker

`@await` M1/M2 left one soft spot: a header cannot tell you whether a method suspends. `NFAsync<T>` promotes async-ness to a **return-type marker** that is visible in the declaration — the parser unwraps it to `T`, so it is pure compile-time metadata: `NFAsync` appears **zero times** in the generated C, and vtable layout, cross-TU linking, and the bridge header are untouched.

```gald
@interface Fetcher : NFObject
- (NFAsync<int>)compute:(int)n;   // suspends, yields an int
+ (NFAsync<void>)runAll;          // entry point
- (int)plain:(int)n;              // unmarked = promises never to suspend
@end
```

The body's awaits decide the truth, and the checker reconciles both directions:

| declaration | body | verdict |
|-------------|------|---------|
| `NFAsync<T>` | has `@await` | ✅ |
| `NFAsync<T>` | no `@await` | **error** — `'compute:' is marked 'NFAsync<T>' but its body never suspends — remove the marker or add an '@await'` |
| bare `T` | has `@await` | **warning** — `'compute:' contains '@await' but its return type is not marked 'NFAsync<T>' — mark it so callers can see it suspends` (`-Werror` escalates) |
| bare `T` | no `@await` | ✅ |

- The marker is part of the signature: `@interface` and `@implementation` must agree — `'NFAsync' marker mismatch on 'compute:': the @interface and @implementation disagree` is an error. Header-only `@interface` methods are exempt (cross-TU safety).
- Value positions are rejected — variables, parameters, ivars, properties: `'NFAsync<T>' is a declaration marker, not a value type (variable) — '@await' the async call instead`.
- `NFAsync` is a reserved class name.

Golden: `tests/golden/37_async_marker/`; negatives under `tests/negative/async_marker_*.gm`.

### Object Subscripting (`a[0]` on NFArray)

`recv[i]` and `recv[i] = v` on container objects now work as sugar. The checker rewrites them — type-aware, judged by the **symbol table** (does the class, or a superclass, actually declare the methods?), not by "looks like an object":

| source | rewritten to | condition |
|--------|--------------|-----------|
| `recv[i]` | `[recv objectAtIndex:i]` | receiver's class declares `objectAtIndex:` |
| `recv[i] = v` | `[recv setObject:v atIndex:i]` | class also declares `setObject:atIndex:` |

```gald
NFArray *a = @[ @"x", @"y", @"z" ];
NFLog(@"%@", a[0]);            // → [a objectAtIndex:0]
NFMutableArray *m = [NFMutableArray array];
[m addObject:@"first"];
m[0] = @"hello";               // → [m setObject:@"hello" atIndex:0] — replaces, not appends
```

Plain C is never touched: `int c[3]; c[1]`, `char *p; p[0]`, and `const char *s; s[2]` all pass through as raw C subscripts (probe-verified, zero false positives). The rewrite lands in the checker (not the parser — the parser has no variable types, and the emit stage has no vtable metadata), so downstream vtable dispatch, nil guards, and SEL constants work with zero special cases.

Dictionary subscripting (`d[@"k"]`) is deliberately **not** part of this rewrite: the mapping is `objectAtIndex:`-only, so `NFDictionary` does not declare `objectForKeyedSubscript:` — that would advertise a spelling which the rewrite would send to the wrong selector. Use `[d objectForKey:@"k"]`.

### Real Generic Checking (monomorphization + element types)

Generic containers **monomorphize and are type-checked**. `NFArray<NFString *>` and `NFDictionary<NFString *, NFNumber *>` generate real specialized C (struct, vtable, class metadata, method copies with substituted types), and the checker substitutes the element types into method signatures — so the element type is enforced, not erased:

```gald
NFMutableArray<NFString *> *m = [NFMutableArray array];
[m addObject:@"a"];
NFString *s = [m objectAtIndex:0];      // NFString *, not id

[m addObject:@42];                      // ✗ error: NFNumber* into an NFString* container
int bad = [m objectAtIndex:0];          // ✗ error: pointer into scalar
```

`@[...]` and `@{...}` literals **infer** their element types when every element agrees, so `NFArray<NFString *> *a = @[ @"x", @"y" ];` needs no annotation; a mixed array falls back to bare `NFArray`.

Both spellings coexist: bare `NFArray` stays fully supported (zero migration) and simply erases to `id`. Assigning a bare container into a specialized variable is allowed but warns, because the element type is then unverified:

```text
warning: assigning a bare 'NFArray *' to a specialization of it — the bare
container's element type is unchecked; add an explicit cast if the contents are known to match
```

`-Werror` escalates it. `NFArray<A>` and `NFArray<B>` remain mutually assignable without complaint — the same permissiveness as ObjC lightweight generics (you asked for `id` back, you get `id` back).

Note the cost: specialization is compile-time code, not free type safety. The same program using containers generically instead of bare compiles to ~42 KB / +41% more C — all duplicated method bodies and metadata, byte-identical layout, so zero runtime benefit. Golden: `tests/golden/40_nfarray_generic/`.

### Gald-Syntax Macros (dual-track `#define`)

`#define` bodies containing **gald syntax** (`[recv msg]`, `@`-literals, `^{}` blocks) used to be passed through verbatim to the C compiler — a syntax error. galdc now parses and expands them at the source level. Plain-C macro bodies pass through unchanged and are expanded by the C compiler as before; behavior is identical there.

```gald
#define TAG(o)      [o tag]                    // gald track: expanded by galdc
#define BUMP(o, n)  [o addTo:n times:1]
#define LOG(x)      NFLog(@"tag=%d", x)        // body contains an @literal
#define TWICE(x)    ((x) + (x))                // C track: expanded by clang

int t = TAG(w);                                    // → [w tag]
BUMP(w, 3);
LOG(TAG(w));
```

Expansion rules follow ISO C §6.10.3 (implemented independently in `crates/cpp`, cross-checked line-by-line against `clang -E`): arguments are fully expanded before substitution (`#`/`##` operands use raw text), `#param` stringifies, `a ## b` pastes, `__VA_ARGS__` joins with commas, self-recursive macros freeze (blue-paint), a function-like macro's bare name outside a call does not expand, and `\` continuations join logical lines. Conditional directives (`#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif`) are evaluated by galdc too — `defined(X)` operands are exempt from expansion, skipped groups don't define macros, and malformed conditionals error instead of silently swallowing the file.

Limits (clear errors, not silent): a macro invocation must close on one line (use `\` to continue), and macro bodies may not contain `_Pragma`. Golden: `tests/golden/38_macros/`.

### C99 Designated Initializers

All six C99 designated-initializer forms work, including the ones ObjC's C subset never needed:

```gald
struct Point { int x; int y; };
struct Point p1 = { .x = 1, .y = 2 };      // 1. full designated
struct Point p2 = { .y = 5 };               // 2. partial — omitted fields zero-filled
struct Point p3 = { .x = 1, 7 };           // 3. designated mixed with positional

NFRange r = (NFRange){ .location = 3,      // 4. compound literal + designators
                        .length = 9 };

CGPoint pts[3] = { [0].wx = 1, [2].wy = 6 };  // 5. array elements

struct Outer o = { .in.a = 3, .tag = 9 }; // 6. nested member chains
```

Positional entries continue from the last designated field (form 3 puts `7` in `.y`), unspecified fields are zero-filled, and designator chains like `[1].wx` work too. They pass through as ordinary C initializers — no new IR.

### C99 `_Complex` Passthrough

`float _Complex` / `double _Complex` declarations, typedefs, and parameters pass through untouched, and imaginary literals (`2.0i`, `1e3j`) are emitted **raw** — the imaginary part used to be silently dropped (`2.0i` → `2.0f`).

```gald
#include <complex.h>
typedef float _Complex cfloat;

double _Complex z = 1.0 + 2.0i;   // mixed real + imaginary
double _Complex w = 2.0i;         // bare imaginary
cfloat f = 1.5;
printf("A=%.1f+%.1fi\n", creal(z), cimag(z));
```

Known limit: the gald checker has no complex type inference (narrowing between complex widths isn't warned; semantics are enforced by the C compiler). Golden: `tests/golden/39_complex/`.

---

## Compilation & CLI

### Command-Line Options

```bash
galdc [options] <input.gm>

Modes:
  (none)            Default: transpile + compile to binary (requires -o)
  run               Transpile + compile + run (auto-clean temp binary)

Options:
  -o <file>         Output file (.o produces object file, otherwise executable)
  -I <dir>          Add include search path
  -L <dir>          Add library search path
  -v, --verbose     Show verbose output (including Clang warnings)
  -V, --version     Show version number
  -rewrite-gald     Output C code only (no compilation)
  -fgald-arc        Enable ARC (default)
  -fno-gald-arc     Disable ARC (manual MRC mode)
  -fno-checker      Skip type checking
  -eh <mode>        Exception backend: checked (default) or legacy (alias sjlj)
  -ffreestanding    Bare-metal/freestanding output (no libc, no TLS)
  -backend <mode>   C compiler backend: clang (default), portable, or gcc
  -arch <target>    Build for target architecture (e.g. -arch x86_64)
  -asm <file.s>     Link a real assembly file (repeatable)
  -gen-completions <shell>  Generate shell completion script (zsh|bash|fish)
  -emit-bridge-header <file.h>  Generate a C bridge header for calling Gald from C

Refcount trace (debug aid):
  -trace-refcount                Print a static reference-count trace of each retained object, in source order
  -trace-max-iters <N>           Loop iterations simulated in the trace (default 2)
  -trace-no-color                Disable colors in the trace
```

### Build System Integration

**cargo**:

```bash
cargo build
cargo test --workspace
```

---

## Code Examples

### Hello World

```gald
#include <stdio.h>
#import <Foundation/Foundation.gh>

@interface Greeter : NFObject
- (void)greet;
@end

@implementation Greeter
- (void)greet {
    printf("Hello, Gald!\n");
}
@end

int main() {
    @autoreleasepool {
        Greeter *g = [[Greeter alloc] init];
        [g greet];
    }
    return 0;
}
```

### Polymorphism

```gald
@interface Animal : NFObject
- (void)speak;
@end

@interface Dog : Animal
@end

@interface Cat : Animal
@end

@implementation Animal
- (void)speak { printf("...\n"); }
@end

@implementation Dog
- (void)speak { printf("Woof!\n"); }
@end

@implementation Cat
- (void)speak { printf("Meow!\n"); }
@end

int main() {
    Animal *animals[2];
    animals[0] = [[Dog alloc] init];
    animals[1] = [[Cat alloc] init];
    for (int i = 0; i < 2; i++)
        [animals[i] speak];  // VTable static dispatch
    return 0;
}
```

### Block + ARC

```gald
typedef void (^EventHandler)(int code, NFString *msg);

@interface Engine : NFObject
- (void)onEvent:(EventHandler)handler;
@end

int main() {
    @autoreleasepool {
        Engine *e = [[Engine alloc] init];
        int captured = 42;
        [e onEvent:^void(int code, NFString *msg) {
            printf("code=%d msg=%s captured=%d\n", code, msg, captured);
        }];
    }
    return 0;
}
```

### Static Generics

Gald compiles generics at compile time via **monomorphization** — each `DataPack<QuantumToken *>` becomes a standalone C struct `DataPack_QuantumToken_ptr` with concrete type substitutions. No type erasure, no boxing, no runtime overhead.

```gald
@interface DataPack<T> : NFObject {
    @public
    int _count;
    T _storage[2];
}
- (void)pushItem:(T)item;
- (T)popItem;
@end

@implementation DataPack
- (void)pushItem:(T)item {
    if (_count < 2) {
        _storage[_count++] = item;
    }
}
- (T)popItem {
    if (_count > 0) {
        _count--;
        T item = _storage[_count];
        _storage[_count] = 0;
        return gald_autorelease(item);
    }
    return 0;
}
@end

int main() {
    @autoreleasepool {
        // Each instantiation generates specialized C code
        DataPack<QuantumToken *> *tokenPack = [[DataPack<QuantumToken *> alloc] init];
        DataPack<EncryptedMetric *> *metricPack = [[DataPack<EncryptedMetric *> alloc] init];
    }
    return 0;
}
```

**How it works**:

- `DataPack<T>` → `struct DataPack` (generic) is skipped; only specialized structs are emitted
- `DataPack<QuantumToken *>` → `struct DataPack_QuantumToken_ptr` with `QuantumToken * _storage[2]`
- Methods are cloned per instantiation with substituted return/param/body types
- VTable, meta VTable, and class metadata are generated per instantiation
- Type name encoding: `DataPack<QuantumToken *>` → `DataPack_QuantumToken_ptr`

**Status**: ✅ Fully implemented for single type parameter. Multiple parameters (`<K, V>`) in progress.

---

## Design Principles

### 1. Static > Dynamic

ObjC's runtime is powerful, but I don't want to depend on it. Make all decisions at compile time — what you generate is what runs, no surprises.

- Method dispatch → VTable offsets
- Memory management → CFG static analysis
- Protocol conformance → compile-time checks

### 2. Generate Human-Readable C

Gald's "backend" is **human-readable C99**, not LLVM IR. This means:

- Debug with standard Clang/LLDB tools
- Generated C can be reviewed, modified, embedded in other projects
- No LLVM backend lock-in — wherever Clang runs, Gald runs

### 3. Incremental

Start from a class system, add things gradually:

- ✅ Class/Protocol/Category/Properties
- ✅ Block / @autoreleasepool
- ✅ Static ARC
- ✅ @selector / VTable polymorphism
- ✅ @namespace
- ✅ Exception handling (`@try`/`@catch`/`@finally`/`@throw`) — **default backend is `-eh checked`** (flag + guard lowering, unwind-safe ARC: a cross-function throw releases every frame's owned locals; no `setjmp`/`longjmp`, so it works on bare metal)
  - `-eh legacy` (alias `-eh sjlj`) selects the old `setjmp`/`longjmp` backend. ⚠️ Its documented limit (verified with ASan): an object owned by an **intermediate frame** leaks on a **cross-function throw** — `longjmp` skips its scope-end `gald_release`. That limit does not apply to the default backend.
- ⏳ Foundation standard library
- ⏳ Compiler self-hosting

### 4. Readability

Generated C should be as clear as handwritten C:

- `struct` + `->` for ivar access
- `static const SEL` constants
- Consistent and predictable naming
- Explicit temporary variable names

---

## Roadmap

### Phase 1: Infrastructure ✅

- [x] Lexer
- [x] Preprocessor
- [x] Parser
- [x] CST validation & printing

### Phase 2: Semantic Analysis ✅

- [x] Symbol table
- [x] Name binding
- [x] Type checking
- [x] Property elaboration
- [x] Protocol conformance

### Phase 3: VTable + Object Layout ✅

- [x] VTable layout
- [x] Object memory layout
- [x] Class metadata

### Phase 4: Intermediate Representation ✅

- [x] Typed AST
- [x] CST → AST
- [x] CFG construction

### Phase 5: Static ARC ✅

- [x] Ownership inference
- [x] Local + global ARC
- [x] Retain/Release insertion
- [x] ARC verification

### Phase 6: C99 Code Generation ✅

- [x] C99 AST
- [x] AST → C99 conversion
- [x] Header generation
- [x] Compiler options

### Phase 7: Runtime ✅

- [x] Core retain/release/alloc/init
- [x] Autorelease pool

### Phase 8-10: In Progress

- [ ] Foundation standard library

- [x] Block runtime

- [x] Weak references

- [✅] Generics (monomorphization)

- [x] Exception handling (`@try`/`@catch`/`@finally`/`@throw`; `-eh checked` for full unwind-safe ARC)

- [ ] Debug information

- [x] VSCode/IDE support

---

## FAQ

### **Is it production-ready?**

Not yet. But it is **real** - it compiles, it runs, and it is designed with growth in mind. If you find syntax appealing and want to contributem, you are welcome.

### What can Gald do?

Write small games, tools, toys. The snake game, Flappy Bird, space shooter, tic-tac-toe in this repo are all written in Gald, running in the terminal.

### What's missing compared to ObjC?

- No `objc_msgSend` — VTable static dispatch
- No runtime Method Swizzling
- No `forwardInvocation:`
- Selectors are compile-time constants, not runtime strings

### Why C99 as output?

Because C99 compiles everywhere. Generate human-readable C, compile with Clang, debug with lldb. No need to bind to any specific backend.

---

## License

MIT License

Copyright (c) 2026 3shine123