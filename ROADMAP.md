# ROADMAP

> What is **actually not done yet**. Completed features are not listed here.
> For the current architecture see `doc/architecture.md`; historical decision
> records live in `doc/stable_slots_plan.md` and `doc/arc_intern_uaf.md`.

## Language / compiler gaps

- [ ] **Debug information** — galdc does not emit DWARF; debugging generated C is the only option today.
- [ ] **Generic class-method return instantiation** — type arguments on class methods (`+ (NFArray<T>)...`) are not substituted; monomorphization covers instance-side signatures and declared types.
- [ ] **Categories across TUs** — category method dispatch works single-TU; cross-TU category layout is limited.
- [ ] **Protocol inheritance across TUs** — same caveat; protocol conformance checking is per-TU.
- [ ] **Pattern switch M2** — mixing plain constant arms with pattern arms in one `switch` (currently rejected with a clear error); exhaustiveness analysis.

## Foundation library gaps

- [ ] **Missing container classes** — `NFSet` / `NFMutableSet` / `NFOrderedSet` / `NSPredicate` do not exist.
- [ ] **Variadic convenience constructors** — only `arrayWithObjects:count:`-style signatures exist; the variadic `initWithObjects:..., nil` form is not declared. Related known quirk: `[NFMutableArray arrayWithObjects:...]` desugars to the immutable array path.
- [ ] **`NSAssert`** macro is absent.
- [ ] **Object subscripting on dictionaries** — `d[@"k"]` is deliberately not wired (`objectForKeyedSubscript:` undeclared); the checker's subscript rewrite only knows `objectAtIndex:`.
- [ ] **`conformsToProtocol:` runtime** — class metadata carries a `protocol_count` field but codegen does not populate it; the runtime protocol query is unimplemented.

## EH known limitations (accepted, not scheduled)

- **`-eh legacy` (sjlj)**: a cross-function `@throw` skips scope-end releases in intermediate frames (leak). The default `checked` backend does not have this issue.
- **Bare generics are a warning, not an error**: bare container spellings (`NFArray` without type args) keep erased-compat behavior; use explicit type args for element-type checking.

## Tooling

- [ ] **Windows native validation** — `galdc.exe` builds and cross-compiles, but runtime behavior on real Windows is untested (zig cc backend, `__thread` semantics).
