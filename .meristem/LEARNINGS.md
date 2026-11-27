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

## 2026-11-27 — Latency continuity during control edits

A downstream libobs probe compares each sample against a deterministic delayed stereo input. The old adapters produced 579,636 incorrect sample positions (Color, 24 samples delay) and 612,273 (Spectral, 1024 delay) of 960,000 positions each under concurrent control edits. Shared routed settings now optionally own a control-prepared format-dependent engine. Only control copies/reclaims references; audio borrows the engine and applies same-format settings in place. Both regression probes report zero errors (123,568 and 422,660 concurrent updates respectively on this run); seven focused numerical/libobs tests passed. Explicit topology changes still reset and are not crossfaded.

## 2026-11-27 — Graph control and audio ownership

Graph macro updates formerly acquired the graph processing mutex and caused dry latency discontinuities: 697,753 incorrect sample positions in a 960,000-position downstream libobs probe. Graph replacement and validated fixed-size macro snapshots now use the ownership handoff. The control-side accepted values/immutable metadata support saves, restore and properties without accessing mutable audio history. Same-graph macro edits retain node/delay state; topology replacements are still not crossfaded. The probe passed with zero errors, including a run with 3,248,554 updates and 203,034 property inspections. Full Linux Release rebuild and all 57 CTests passed, including builder, recovery, monitor and LADSPA graph. Five of 15 standalone filter adapters (Clock Sync, Dynamics, Color, Spectral, Graph) no longer take processing mutexes. Ten remain: Native, Formant, Time, Space, Stereo, Mic, Multiband, Dynamic EQ, Transient and LADSPA. The separate nonblocking telemetry publication mutexes remain. Next: reuse format-prepared routed settings for Time/Space/Formant; then migrate native/simple standalone effects, and separately refactor LADSPA metadata and parameter preparation. Live CPU timing, ThreadSanitizer and Windows/macOS qualification remain unverified.
