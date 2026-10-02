# 37_async_marker — `NFAsync<T>` declaration marker

`- (NFAsync<T>)m` marks a suspending method in the **return-type position**.
The parser unwraps it to `T` + an `async_marker` flag (reserved name
`NFAsync`, registered in the parser's builtin type table + `generic_class_names`
so `<...>` routes to type args, not the protocol path). Pure compile-time
metadata: the emitted C signature is just `T` — `NFAsync` appears **0 times**
in the generated C (checked via grep), so vtable layout, cross-TU linking and
the bridge header are untouched.

## Reconciliation (`gald_async::check_unit`, pre-desugar)

| declaration | body | verdict |
|-------------|------|---------|
| `NFAsync<T>` | has `@await` | ✅ |
| `NFAsync<T>` | no `@await` | **error** — the marker must not lie (same philosophy as bare `@throws` requiring a real throw) |
| bare `T` | has `@await` | **warning** (purple, `-Werror` escalates) — the "don't forget to mark" nudge |
| bare `T` | no `@await` | ✅ |

Interface/impl must agree: the marker is part of the signature → mismatch is
an **error**. Only implementations (methods with a body) reconcile against the
body; header-only `@interface` methods are exempt (cross-TU safety, same rule
as the protocol-conformance check).

## Test coverage (`async_marker_test.gm`)

- `NFAsync<int>` compute with a `@await` suspension point → marked+await ok
- `NFAsync<void>` class-method entry, called from `main` (blocking wrapper)
- `@await` across a call chain (`runAll` → `compute:`)
- unmarked `plain:` with no `@await` → ok, callable without ceremony

Expected stdout: `runAll x=42 y=6`.

## Negative cases (`tests/negative/`)

| file | expected error |
|------|----------------|
| `async_marker_mismatch.gm` | `@interface`/`@implementation` marker disagreement |
| `async_marker_no_await.gm` | marked but body never suspends |
| `async_marker_value_pos.gm` | `NFAsync<T>` in a variable position |
| `async_marker_reserved.gm` | class named `NFAsync` (reserved) |

Also rejected in value positions: parameters and ivars (same checker helper,
verified by probe — see AGENTS.md `NFAsync<T>` section).

## Snapshot

`async_marker_test.out` is the default (ARC) run; deterministic, ARC/MRC
identical (no refcount-sensitive output).
