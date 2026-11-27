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

## 2026-11-27 — Time, Space and Formant history continuity

The real libobs history contention fixture compares fresh engines receiving identical deterministic stereo bursts and silence, changing an inactive parameter concurrently on one. Original adapters diverged at 268,360 (Time), 404,405 (Space), and 25,249 (Formant) of 480,000 sample positions each. Prepared routed settings now preserve DSP history on same-mode edits. All three probes report zero mismatches, including echo/reverb tails; 7 focused tests passed. Format-dependent allocation stays off audio. FractionalDelay::clear now resets position and valid-history length in constant time, treating unwritten taps as zero; all current callers write every channel before advance. Interpolation after clear, wrap and repeated clear has regression coverage. Explicit topology resets still discard history, no crossfade or live timing claim.

Full Linux Release rebuild and all 57 CTests passed; Time core ASan/UBSan passed. Eight of 15 adapters now avoid processing mutexes. Remaining: Native, Stereo, Mic, Multiband, Dynamic EQ, Transient, LADSPA. Next: fixed-size non-routed settings snapshots for the simpler telemetry-enabled adapters, and a dedicated LADSPA ownership pass. Telemetry publication try-locks, live CPU timing, ThreadSanitizer and other OS qualification remain outstanding.

## 2026-11-27 — Dynamic EQ, Multiband and Transient

Reused the bounded settings handoff with optional routes (non-routed settings do not subscribe). DSP histories remain audio-owned, control changes take effect at packet boundaries, and telemetry uses generation/epoch checks as Dynamics does. Baseline libobs history probes diverged at 76,451 Dynamic EQ, 120,034 Multiband, and 119,983 Transient sample positions out of 480,000 each under repeated no-op/inactive edits. After migration all three matched reference exactly; band-transient also matched under 214,917 updates. Ten focused tests passed. Mode/crossover resets remain deliberate; monitor publication try-locks remain.

Full Linux Release rebuild and all 57 CTests passed. Eleven of 15 adapters now avoid processing mutexes. Remaining: Native (legacy route keys, use custom control-side subscription preparation), Stereo (single-writer diagnostic generation), Microphone (profile/rate changes and fixed-size engines), LADSPA (separate metadata, immutable control packet and third-party instance lifetime). Telemetry publication try-locks, live CPU timing, ThreadSanitizer and cross-platform verification remain outstanding.

## 2026-11-27 — Native Filter, Stereo and Microphone

Standalone history contention probes reproduced 2,208 Native Filter, 7,680 Stereo and 11,147 Microphone incorrect sample positions out of 480,000; all matched uninterrupted reference after handoff migration. Native uses control-prepared legacy route keys and logical comb reset (no bulk clearing), with ring-wrap/reset impulse coverage. Stereo audio owns diagnostics; generation rejects outdated snapshots immediately on control updates, covered by procedure test. Mic profile stages now use four fixed-capacity biquads, no heap allocation on profile configuration; profile/rate changes still reset state. Full Linux Release rebuild and all 57 CTests passed. Fourteen of 15 adapters now avoid processing mutexes. Only LADSPA remains: separate control-side validated mix/LFO/route/port packet and saved-target strings from audio-owned engine state; use a bounded ownership handoff for same-instance parameter updates and off-callback retirement of replacement plugin handles. Existing properties read instance route strings under processing mutex; make these control-owned or immutable. Preserve pre-activation initial controls and invalid-library dry fallback. Then address the independent monitor/telemetry publication try-locks. ThreadSanitizer, cross-platform and live timing/installed GUI qualification remain outstanding.

## 2026-11-27 — LADSPA processing mutex migration complete

LADSPA now prepares validated numeric controls, route destinations and external subscriptions off audio and hands off packets retaining the existing plugin or a prepared replacement. Initial controls are connected before activation. Only audio mutates published instance state; property reads use accepted immutable metadata and saved target names. Invalid selections retain deliberate dry bypass, and replacements are not crossfaded. Real libobs SDK amplifier regressions reproduced 26,879 incorrect sample positions on same-instance updates and 960 during equivalent mono/stereo plugin replacement. Both now report zero across 480,000 positions each; final full suite also verified concurrent property inspection (17 reads during 272 replacements). Full Linux Release rebuild and 57/57 CTests passed. All 15 filter adapters have removed processing mutex acquisition. Only GraphMonitor and FilterTelemetry publication try-locks remain on audio paths; control/worker/catalog locks stay off audio. Third-party run() real-time behavior, installed GUI, callback timing, ThreadSanitizer and other platforms remain unqualified. Next use bounded single-writer/multi-reader snapshot storage for diagnostic publications with coherent-frame stress tests; do not use a non-atomic seqlock over mutable payload.

## 2026-11-27 — Diagnostic snapshot mutexes removed

GraphMonitor and FilterTelemetry share RealtimeSnapshot: three preallocated slots, single serialized publisher, bounded atomic reader pinning, no non-atomic seqlock payload. A publication may be skipped when both spare slots are pinned; readers may miss a frame under contention. Invalidation is epoch-checked before and after copying; existing versioned procedure layouts and leases are unchanged. New deterministic pinned-reader saturation and recovery test plus 100,000 publication attempts with four readers passed (261,522 coherent reads on full-suite run). ASan/UBSan passed; ThreadSanitizer failed to start with unexpected memory mapping (exit 66), so race-sanitizer qualification remains outstanding. Actual libobs graph monitor three-reader and four standalone analysis two-reader regressions passed, including in-flight invalidation. Full Linux Release rebuild and 58/58 CTests passed. No std::try_to_lock remains in src; remaining mutex acquisition sites audited as control/worker/catalog only. Do not infer end-to-end real-time safety or deadline compliance from this alone: next measure callback duration distributions, allocation behavior and OBS integration under load, then qualify platforms. Third-party LADSPA run() and libobs internals remain outside the host's mutex guarantee.
