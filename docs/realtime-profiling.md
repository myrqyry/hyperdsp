# Real-time profiling

This opt-in Linux benchmark wraps the exported production filter callbacks in registered libobs filter types, then drives a real synthetic stereo OBS source. It is not a mock DSP engine or a GUI/physical-device benchmark. Timing is scoped to the actual filter callback, excluding host routing, fixture input generation and output checking. Nothing is added to the production module.

## Run

Use the same OBS SDK as the target module. Build a Release configuration; if the local headless OBS setup requires a display, use Xvfb as for the regular tests.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DHYPERDSP_BUILD_BENCHMARKS=ON
cmake --build build --target hyperdsp hyperdsp-benchmark-audio -j4
build/hyperdsp-benchmark-audio "$PWD/build/hyperdsp.so" "$PWD"
```

For glibc only, the optional test-only allocator preload uses initial-exec TLS (required to avoid allocator recursion during loader/thread startup). It counts calls to interposed malloc/calloc/realloc/reallocarray/aligned_alloc/memalign/posix_memalign/free on the audio-calling thread while the filter runs. A self-check must detect an explicit allocation and release. It does not intercept custom allocators, direct mmap, hidden libc calls or other threads, and is not an allocator replacement for normal OBS sessions. Do not preload it into normal OBS.

```sh
cmake --build build --target hyperdsp-benchmark-allocations
LD_PRELOAD="$PWD/build/hyperdsp-benchmark-allocations.so" \
  build/hyperdsp-benchmark-audio "$PWD/build/hyperdsp.so" "$PWD"
```

Each case creates a fresh filter per phase, warms 100 packets, then measures 2,000 packets of 480 stereo frames at 48 kHz (10 ms of audio per callback). The second phase runs an unpaced writer repeatedly changing an irrelevant settings key. The output must match the uninterrupted reference within 1e-6 on both channels, have nonzero energy and differ from the input; an inactive or bypassed workload is not valid. Strict mode fails on mismatches, missing samples or observed allocator calls. Wall-time mean, p50/p95/p99/max, count exceeding the 10 ms budget, and measured thread CPU time as a percentage of audio duration are printed. Timings are not hard CI thresholds; CPU-time sampling and instrumentation add overhead.

For comparison with an intentionally known-broken module, use the module and matching data directory as arguments plus `--allow-mismatch`. This permits reporting concurrent-phase errors and observed allocator calls rather than treating them as a benchmark execution failure. It does not certify that build.

## Initial measurements, 2026-11-27

Ryzen 7 7700, Linux Mint, Release, current branch at 0c7d0ea. One unpaced run with the allocator audit enabled; previous installed binary separately measured without the allocator audit (exact source revision unknown). Each workload compared 960,000 stereo sample positions under edits to its own idle reference.

| Active effect | Old installed mismatches | Current mismatches | Current p99 under edits (us) | Current thread CPU budget under edits |
|---|---:|---:|---:|---:|
| Limiter | 301,977 | 0 | 24.25 | 0.163% |
| Color 4x | 314,384 | 0 | 187.13 | 1.393% |
| Spectral gate | 959,520 | 0 | 567.33 | 4.362% |
| Echo | 926,841 | 0 | 21.34 | 0.128% |
| Reverb | 958,552 | 0 | 42.07 | 0.294% |
| Graph: compressor, spectral gate, reverb | 958,078 | 0 | 605.25 | 4.903% |

All 24,000 current measured callbacks had zero observed allocation/free calls, zero output mismatches and zero callbacks exceeding 10 ms. The longest observed wall time was 5.536 ms (idle spectral gate); the longest during concurrent edits was 3.079 ms (spectral gate). An earlier uninstrumented run also passed. A subsequent CMake-built audit run again measured zero allocations and waveform errors, but recorded one spectral-gate callback at 11.955 ms under edits (one 10 ms budget exceedance out of 24,000 callbacks). Thread CPU budget for that phase was 4.55%; scheduler delay is plausible but the specific cause is not established. Do not infer zero deadline overruns from the first run. The old module's deceptively low median under concurrent spectral/graph edits (0.18/0.05 us) reflects contention-induced bypass, not more efficient DSP. These are observations, not proof of universal deadline compliance or precise speedup; old binary provenance and differing concurrent update counts limit comparisons.

Coverage is six selected workloads, warm steady-state, 48 kHz stereo, with telemetry leases inactive. Next expand to other modes and adapters, first packets/topology changes, smaller blocks/multiple layouts, active telemetry and a paced load/real OBS capture. Clock Sync requires variable-length/timestamp-aware checks; third-party LADSPA behavior requires representative plugins. Test other platforms and actual desktop/audio device scheduling separately.
