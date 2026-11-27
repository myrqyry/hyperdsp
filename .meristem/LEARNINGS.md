<!-- meristem-template:v1 -->
# Learnings

Project-local reusable lessons. A learning should prevent future work or error, not merely narrate what happened.

Use entries like:

## YYYY-MM-DD — Learning

- **Signal / failure:**
- **Mechanism:**
- **Scope:**
- **Evidence:**
- **Recommended behavior:**
**Validation status:** observed | tested | unvalidated

## 2026-11-27 — Real-time engine ownership

Clock Sync returned engine-owned output buffers after unlocking its processing mutex. A downstream libobs probe that changed quality twice on a control thread while consuming the packet reproduced a heap-use-after-free under AddressSanitizer. The bounded latest-wins handoff now exchanges ownership only at the next serialized callback boundary, and the control writer reclaims superseded or retired state. The actual probe passes ASan after the repair; a 50,000-publication ownership stress test passes ASan/UBSan with all destruction on control. Explicit converter replacement still re-primes; host-level libobs locks and third-party internals are outside this guarantee. Preserve audio history on ordinary parameter edits rather than rebuilding state.

## 2026-11-27 — Dynamics contention regression

With a constant full-wet −20 dB limiter and concurrent release edits, the old OBS adapter leaked 449,130 samples above the test ceiling across 3,000 packets (peak 0.4). Audio-owned DSP state with latest-wins routed settings now passes the identical test (peak 0.1, zero violations across 3,000 packets and 30,966 control updates on one run). Subscription references are prepared and reclaimed off audio; telemetry invalidates on publication and checks its epoch/generation so old blocks cannot revalidate stale data. Numerical dynamics, actual libobs capture and Qt filter-analysis focused tests pass. Remaining adapters still use try-lock bypass; prioritize Spectral/Color/Graph, and do not run allocating format resets on the callback.
