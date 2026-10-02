# strong_metadata — metadata-linkage and class-ownership protection tests

Guards the precompiled-library (P3) contract (`doc/stable_slots_plan.md` §8) and
the per-class ownership rule R2 (§9).

```bash
./tests/strong_metadata/run_strong_metadata_test.sh
```

Four checks, because "it printed the right thing" does not by itself prove the
metadata linkage is correct:

| # | What | Why it matters |
|---|---|---|
| 1 | `nm` shows the library's metadata as **strong** | the real table must outrank a client's stub, by object-file evidence — not by luck of link order |
| 2 | a declaration-only client links against the strong library and runs | the actual P3 end-to-end path |
| 3 | a client *also* built with the flag fails with `duplicate symbol` | two strong tables must be a loud error, never a random winner |
| 4 | two TUs that both `@implementation` the same class fail with `duplicate symbol` (**no flag involved**) | rule R2: ownership is derived from the main file, so a second owner must never merge silently |

Check 3 covers the explicit `-fstrong-metadata` switch; check 4 covers R2, the
automatic rule that makes "duplicate implementation" loud without any flag.

Out-of-band, like `tests/multi_tu` — it needs several TUs compiled with
**different flags** plus a static library, which `test_all.py` (one TU per `.gm`)
cannot express. The run-time half of the same story lives in
`tests/multi_tu/09_layout_mismatch` (two owners, checked at link *and* run).

## Relationship to R2

Under R2 a class's metadata is emitted **strong** by the TU whose main file holds
that class's `@implementation`, and weak in every other TU. An
`@implementation` reaching a TU through `#import` does **not** confer ownership —
that is what keeps the self-contained mode (every TU inlines Foundation, and the
repeated copies must keep merging weakly) working unchanged.
