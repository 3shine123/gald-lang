# 36_defer — `@defer` scope-exit execution

`@defer { ... }` registers its body with the innermost enclosing block; the body
runs at EVERY exit of that block (natural end, any-depth `return`,
`break`/`continue` that leaves the block, same-function `@throw`),
innermost-first (LIFO).

## Test coverage (`defer_test.ov`)

| section | verifies |
|---------|----------|
| LIFO + early return | multiple defers run LIFO; `return` from an inner block fires them |
| loop body | body defers fire every iteration AND on `continue`/`break` |
| outer defer fires once | `break` does NOT fire defers registered outside the loop (no double-fire) |
| nested blocks | inner block end vs outer block end ordering |
| defer before throw | same-function `@throw` fires the defer before `@catch` runs |
| ARC order | defer runs BEFORE the ARC-injected scope-end `ovel_release` (object still alive: `dealloc` prints last) |
| block literal scope | a block literal is its own function — fresh defer scope inside |

## Implementation

- Pass: `crates/defer` (`ovel_defer::desugar_unit`), pipeline **Step 3.9** —
  after `-eh checked` desugar (its throws are already plain returns, so they
  splice with zero special-casing) and BEFORE ARC (so ARC's release injection
  lands after the user's defer statements).
- Desugar only: deferred bodies are spliced as ordinary statements; codegen /
  runtime / checker never see a `Defer` node.
- Two tracked sets: `pending` (fires on `return`/`@throw`) and `jump` (fires on
  `break`/`continue` — defers registered between here and the innermost
  loop/switch only).
- M1 limits: no `return`/`break`/`continue`/`@throw` inside a defer body
  (negative: `tests/negative/defer_return.ov`); `@defer` must sit directly in
  a block.

## Snapshot

`defer_test.out` is the default (ARC) run; output is deterministic and
byte-identical under `-eh checked`. Under `-fno-ovel-arc` only the `tracker
dealloc` line disappears (MRC never releases — no dealloc), by design.
