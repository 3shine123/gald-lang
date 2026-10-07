# ROADMAP

> What is **actually not done yet**. Completed features are not listed here.
> For the current architecture see `doc/architecture.md`; historical decision
> records live in `doc/stable_slots_plan.md` and `doc/arc_intern_uaf.md`.

## Language / compiler gaps

- [ ] **Debug information** — nopac does not emit DWARF; debugging generated C is the only option today.
- [ ] **Generic class-method return instantiation** — type arguments on class methods (`+ (NPArray<T>)...`) are not substituted; monomorphization covers instance-side signatures and declared types.
- [ ] **Categories across TUs** — category method dispatch works single-TU; cross-TU category layout is limited. Design finalized (weak-symbol vtable slots), not implemented — see `doc/categories_protocol_plan.md`.
- [ ] **Protocol inheritance across TUs** — same caveat; protocol conformance checking is per-TU. Same plan, phase 3 (`doc/categories_protocol_plan.md`).
- [ ] **Pattern switch M2** — mixing plain constant arms with pattern arms in one `switch` (currently rejected with a clear error); exhaustiveness analysis.

## Foundation library gaps

- [x] **Container classes** — `NPSet` / `NPMutableSet` / `NPOrderedSet` exist
  (isEqual: value semantics, insertion order preserved by NPOrderedSet;
  mutator slots live on NPSet so the subclass vtables resolve — see the
  notes in `NPSet.nh`).
- [x] **`NPPredicate`** (the `NSPredicate` model) — shipped: a runtime
  format-string parser + evaluation engine (`NPPredicate.np`), compile-time KVC
  accessor tables (`NOPA_KVC_$_X`, see `doc/architecture.md` §12) and the host
  filtering API (`filteredArrayUsingPredicate:` /
  `indexOfObjectMatchingPredicate:` / `filterUsingPredicate:`; a `nil`
  predicate is the identity). Covered by `tests/predicate_filter_test.np` and
  `tests/multi_tu/13_kvc_predicate`. Block-based filtering
  (`indexesOfObjectsPassingTest:`) stays a valid alternative for one-off tests.
- [ ] **Variadic convenience constructors** — only `arrayWithObjects:count:`-style signatures exist; the variadic `initWithObjects:..., nil` form is not declared. Related known quirk: `[NPMutableArray arrayWithObjects:...]` desugars to the immutable array path.
- [ ] **`NSAssert`** macro is absent.
- [ ] **Object subscripting on dictionaries** — `d[@"k"]` is deliberately not wired (`objectForKeyedSubscript:` undeclared); the checker's subscript rewrite only knows `objectAtIndex:`.
- [ ] **`conformsToProtocol:` runtime** — class metadata carries a `protocol_count` field but codegen does not populate it; the runtime protocol query is unimplemented.

## EH known limitations (accepted, not scheduled)

- **`-eh legacy` (sjlj)**: a cross-function `@throw` skips scope-end releases in intermediate frames (leak). The default `checked` backend does not have this issue.
- **Bare generics are a warning, not an error**: bare container spellings (`NPArray` without type args) keep erased-compat behavior; use explicit type args for element-type checking.

## Checker known limitations (accepted, not scheduled)

- **Undeclared selector is a warning, not an error** — receiver-type resolution is unreliable under the self-contained umbrella (e.g. `[other count]` inside `NPMutableArray.np` misresolves `NPArray *` as `NPString *`), so an error would break Foundation itself. Consequence: calling an undeclared selector on a statically typed receiver still segfaults at runtime (NULL vtable slot); the warning is the only compile-time guard.

## Tooling

- [ ] **Windows native validation** — `nopac.exe` builds and cross-compiles, but runtime behavior on real Windows is untested (zig cc backend, `__thread` semantics).
