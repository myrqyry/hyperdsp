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

Each case creates a fresh filter per phase, warms 100 packets, then measures 2,000 packets at 48 kHz. The default is 480 stereo frames (10 ms); select `--frames 120` for a 2.5 ms budget. The second phase runs an unpaced writer repeatedly changing an irrelevant settings key. The output must match the uninterrupted reference within 1e-6 on both channels, have nonzero energy and differ from the input; an inactive or bypassed workload is not valid. Clock Sync instead requires variable packet sizes, contiguous output timestamps (within integer rounding), channel relation, and matching packet lengths, timestamp offsets and samples across phases, with a simulated 400 ppm source-clock drift. Strict mode fails on mismatches, missing samples or observed allocator calls. Wall-time mean, p50/p95/p99/max, count exceeding the selected block budget in wall time and thread CPU time, maximum thread CPU time among wall-time overruns, and measured thread CPU time as a percentage of audio duration are printed. Timings are not hard CI thresholds; CPU-time sampling and instrumentation add overhead.

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

The initial coverage above was six selected workloads, warm steady-state, 48 kHz stereo, with telemetry leases inactive.


## Expanded coverage, 2026-11-27

All 15 filter adapters, 16 workloads (both transient modes), including the SDK LADSPA stereo amplifier and medium-quality Clock Sync. The LADSPA case is explicitly skipped unless a fixture is provided; selecting it alone requires the fixture. Select a single workload with `--case name` (see effect names below). Clock Sync must be compiled into the tested module for the full matrix.

```sh
for n in 120 480; do
  LD_PRELOAD="$PWD/build/hyperdsp-benchmark-allocations.so" \
    build/hyperdsp-benchmark-audio "$PWD/build/hyperdsp.so" "$PWD" \
    --frames "$n" --telemetry --ladspa /usr/lib/ladspa/amp.so
done
```

The telemetry option renews the production capture lease before warmup and polls the actual graph V3 and standalone telemetry/status procedures on the calling thread outside the timed region and on a concurrent reader thread. Available reads are counted (not necessarily unique frames). Standalone telemetry may be unavailable while the settings writer continuously invalidates its epoch. This is intentionally an unpaced stress workload, not a model of the normal 20 Hz UI poll. Only the production callback is timed/audited, not the OBS host, producer or control thread. The synthetic inputs, block lengths and active settings are fixed across phases.

Final two strict glibc-audited runs: 64,000 measured callbacks each, **128,000 combined**, zero mismatches, zero observed allocation/free calls, zero wall or thread-CPU budget exceedances in those two runs. Clock Sync produced 240,012 and 960,247 measured samples per phase, respectively, with matching variable packets and timestamp continuity.

| Effect | p99 under edits, 120 frames (us) | p99 under edits, 480 frames (us) | Thread CPU budget, 120 / 480 frames |
|---|---:|---:|---:|
| limiter | 6.44 | 25.65 | 0.163% / 0.160% |
| color_4x | 47.46 | 192.69 | 1.410% / 1.372% |
| spectral_gate | 277.63 | 526.23 | 4.319% / 4.251% |
| echo | 2.90 | 16.89 | 0.108% / 0.106% |
| reverb | 8.81 | 40.66 | 0.242% / 0.274% |
| dynamic_eq | 6.89 | 33.38 | 0.216% / 0.236% |
| multiband | 33.15 | 116.45 | 0.810% / 0.805% |
| transient | 4.11 | 13.34 | 0.160% / 0.088% |
| band_transient | 23.47 | 90.24 | 0.590% / 0.572% |
| native_lowpass | 4.55 | 19.24 | 0.160% / 0.105% |
| formant | 11.12 | 45.56 | 0.281% / 0.278% |
| stereo | 1.83 | 3.61 | 0.069% / 0.034% |
| microphone | 8.01 | 33.04 | 0.235% / 0.221% |
| ladspa_amp | 1.26 | 5.21 | 0.052% / 0.039% |
| clock_sync | 34.89 | 164.33 | 0.835% / 0.911% |
| graph_chain | 298.69 | 644.80 | 4.859% / 4.908% |

An earlier expanded 120-frame run had one graph callback of 2.810 ms wall time with 0.268 ms of thread CPU time (below its 2.5 ms budget). The graph workload also failed the fixture's original arbitrary total-energy threshold (>1); its measured nonzero energy was 0.646, so the threshold was corrected to >1e-6, while retaining the sample equality and changed-output checks. The prior wall-time overrun is still relevant; elapsed minus thread CPU is consistent with scheduling interruption but does not establish its exact cause. Spectral processing is hop-based: 120-frame median was much lower than p99 because only some packets execute the FFT. Do not mistake the median for peak computation cost.

These results are warm steady-state, selected modes, stereo only, no effect topology changes, no physical audio devices and no end-to-end OBS deadline guarantee. Next cover first packets, topology changes, simultaneous sources, layouts/rates, paced playback with realistic UI edits, more representative LADSPA plugins and cross-platform qualification.
