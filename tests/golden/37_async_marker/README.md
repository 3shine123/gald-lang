# 37_async_marker — `NPAsync<T>` declaration marker

`- (NPAsync<T>)m` marks a suspending method in the **return-type position**.
The parser unwraps it to `T` + an `async_marker` flag (reserved name
`NPAsync`, registered in the parser's builtin type table + `generic_class_names`
so `<...>` routes to type args, not the protocol path). Pure compile-time
metadata: the emitted C signature is just `T` — `NPAsync` appears **0 times**
in the generated C (checked via grep), so vtable layout, cross-TU linking and
the bridge header are untouched.

## Reconciliation (`nopa_async::check_unit`, pre-desugar)

| declaration | body | verdict |
|-------------|------|---------|
| `NPAsync<T>` | has `@await` | ✅ |
| `NPAsync<T>` | no `@await` | **error** — the marker must not lie (same philosophy as bare `@throws` requiring a real throw) |
| bare `T` | has `@await` | **warning** (purple, `-Werror` escalates) — the "don't forget to mark" nudge |
| bare `T` | no `@await` | ✅ |

Interface/impl must agree: the marker is part of the signature → mismatch is
an **error**. Only implementations (methods with a body) reconcile against the
body; header-only `@interface` methods are exempt (cross-TU safety, same rule
as the protocol-conformance check).

## Test coverage (`async_marker_test.np`)

- `NPAsync<int>` compute with a `@await` suspension point → marked+await ok
- `NPAsync<void>` class-method entry, called from `main` (blocking wrapper)
- `@await` across a call chain (`runAll` → `compute:`)
- unmarked `plain:` with no `@await` → ok, callable without ceremony

Expected stdout: `runAll x=42 y=6`.

## Negative cases (`tests/negative/`)

| file | expected error |
|------|----------------|
| `async_marker_mismatch.np` | `@interface`/`@implementation` marker disagreement |
| `async_marker_no_await.np` | marked but body never suspends |
| `async_marker_value_pos.np` | `NPAsync<T>` in a variable position |
| `async_marker_reserved.np` | class named `NPAsync` (reserved) |

Also rejected in value positions: parameters and ivars (same checker helper,
verified by probe — see AGENTS.md `NPAsync<T>` section).

## Snapshot

`async_marker_test.out` is the default (ARC) run; deterministic, ARC/MRC
identical (no refcount-sensitive output).
