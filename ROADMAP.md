# ROADMAP

> What is **actually not done yet**. Completed features are not listed here.
> For the current architecture see `doc/architecture.md`; historical decision
> records live in `doc/stable_slots_plan.md` and `doc/arc_intern_uaf.md`.

## Language / compiler gaps

- [ ] **Debug information** — nepac does not emit DWARF; debugging generated C is the only option today.
- [x] **Generic class-method return instantiation (single TU)** — `[Box<NPString *> defaultValue]`
  substitutes T in the checker's result type and in the specialized vtable fn-ptr casts;
  ARC ownership on the returned T is correct (no extra release at the call site).
  Evidence: `probes/genret/` g1 (multi-instantiation), g3 (function pointers), g4 (ARC).
- [ ] **Generic class-method return across TUs** — a client-TU send `[Factory<NPString *> make]`
  does not link when the lib TU never mentions the instantiation (undefined
  `Factory_NPString_ptr_make`); when the lib TU mentions it only via a plain variable
  declaration, its specialized vtable shape diverges from the client's and the startup
  `__sig` check aborts loudly (R3 doing its job — no silent misdispatch). See
  `probes/genret/g2/`. Mixed class/instance sends on a class name are checker-rejected
  by design, non-generic classes included (`g5c` control).
- [x] **Categories across TUs** — implemented (weak-definition/strong-extern vtable slots, ownership excluded for category impls); see `doc/categories_protocol_plan.md` §8 and `tests/multi_tu/14`/`15`.
- [x] **Protocol inheritance across TUs** — implemented: static `NPProtocol` metadata + `conformsToProtocol:` runtime query (`doc/categories_protocol_plan.md` §8).
- [ ] **Pattern switch M2** — mixing plain constant arms with pattern arms in one `switch` (currently rejected with a clear error); exhaustiveness analysis.

## Foundation library gaps

- [x] **Container classes** — `NPSet` / `NPMutableSet` / `NPOrderedSet` exist
  (isEqual: value semantics, insertion order preserved by NPOrderedSet;
  mutator slots live on NPSet so the subclass vtables resolve — see the
  notes in `NPSet.nh`).
- [x] **`NPPredicate`** (the `NSPredicate` model) — shipped: a runtime
  format-string parser + evaluation engine (`NPPredicate.np`), compile-time KVC
  accessor tables (`NEPA_KVC_$_X`, see `doc/architecture.md` §12) and the host
  filtering API (`filteredArrayUsingPredicate:` /
  `indexOfObjectMatchingPredicate:` / `filterUsingPredicate:`; a `nil`
  predicate is the identity). Covered by `tests/predicate_filter_test.np` and
  `tests/multi_tu/13_kvc_predicate`. Block-based filtering
  (`indexesOfObjectsPassingTest:`) stays a valid alternative for one-off tests.
- [ ] **Variadic convenience constructors** — only `arrayWithObjects:count:`-style signatures exist; the variadic `initWithObjects:..., nil` form is not declared. Related known quirk: `[NPMutableArray arrayWithObjects:...]` desugars to the immutable array path.
- [ ] **`NSAssert`** macro is absent.
- [ ] **Object subscripting on dictionaries** — `d[@"k"]` is deliberately not wired (`objectForKeyedSubscript:` undeclared); the checker's subscript rewrite only knows `objectAtIndex:`.
- [x] **`conformsToProtocol:` runtime** — class metadata now carries a populated protocol table and the runtime query walks class + protocol parent chains (`doc/categories_protocol_plan.md` §8).

## EH known limitations (accepted, not scheduled)

- **`-eh legacy` (sjlj)**: a cross-function `@throw` skips scope-end releases in intermediate frames (leak). The default `checked` backend does not have this issue.
- **Bare generics are a warning, not an error**: bare container spellings (`NPArray` without type args) keep erased-compat behavior; use explicit type args for element-type checking.

## Checker known limitations (accepted, not scheduled)

- **Undeclared selector is a warning, not an error** — receiver-type resolution is unreliable under the self-contained umbrella (e.g. `[other count]` inside `NPMutableArray.np` misresolves `NPArray *` as `NPString *`), so an error would break Foundation itself. Consequence: calling an undeclared selector on a statically typed receiver still segfaults at runtime (NULL vtable slot); the warning is the only compile-time guard.

## Tooling

- [ ] **Windows native validation** — `nepac.exe` builds and cross-compiles, but runtime behavior on real Windows is untested (zig cc backend, `__thread` semantics).
