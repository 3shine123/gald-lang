# strong_metadata — protection tests for `-fstrong-metadata`

Guards the precompiled-library (P3) contract described in
`doc/stable_slots_plan.md` §8.

```bash
./tests/strong_metadata/run_strong_metadata_test.sh
```

Three checks, because "it printed the right thing" does not by itself prove the
metadata linkage is correct:

| # | What | Why it matters |
|---|---|---|
| 1 | `nm` shows the library's metadata as **strong** | the real table must outrank a client's stub, by object-file evidence — not by luck of link order |
| 2 | a declaration-only client links against the strong library and runs | the actual P3 end-to-end path |
| 3 | a client *also* built with the flag fails with `duplicate symbol` | two strong tables must be a loud error, never a random winner |

Out-of-band, like `tests/multi_tu` — it needs two TUs compiled with **different
flags** plus a static library, which `test_all.py` (one TU per `.gm`) cannot
express.

## Not covered: duplicate `@implementation`

Two **weak** TUs that both `@implementation` the same class still merge
silently — method bodies and class metadata are emitted `__attribute__((weak))`
on purpose (`crates/codegen/src/codegen.rs`, so base-class methods repeated in
every self-contained TU coalesce). Turning that into a hard error is the
deferred **per-class ownership rule (R2)**: emit strong only in the TU that owns
the `@implementation`. Once R2 lands, "duplicate implementation" fails as a
duplicate symbol for free — exactly like check 3 does today for metadata.
`tests/multi_tu/09_layout_mismatch` already catches the *differing*-methods case.
