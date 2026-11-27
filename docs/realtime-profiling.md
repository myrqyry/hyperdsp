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

## Cold, structural and multichannel coverage, 2026-11-27

Additional options:

- `--cold`: measure from the very first audio callback (no warmup), including initial prepared-engine adoption; retain the no-op concurrent writer.
- `--transitions`: implies cold; alternate two configurations every 250 packets, seven changes per phase. The reference phase applies updates inline between audio submissions; the handoff phase applies the identical schedule from a separate control thread, acknowledged before the next submission. This isolates callback adoption from control preparation time and avoids comparing different coalesced update schedules. It is not a claim that engine replacements are gapless.
- `--rate 48000|96000`, `--channels 1|2|6|8`: choose the OBS mono, stereo, 5.1 or 7.1 layout. Compare every channel against its reference and use distinct signed channel scales. Stereo's documented non-stereo bypass is explicitly required to be dry. For non-stereo layouts the SDK mono LADSPA amplifier is instantiated per channel, rather than treating unsupported stereo LADSPA as an active effect.

Structural changes include mode/bypass transitions, Color 4x/1x oversampling, echo/ping-pong, reverb/allpass, native stages/mode, transient mode, graph replacement, Clock Sync medium/best quality, and stereo-to-mono LADSPA replacement (stereo layout) or per-channel mono gain change. The two phase outputs must agree; the dry segments of applicable effects must also equal the original input. SDK amplifier output is checked independently against the exact expected gain (.25/.5), and every cold or structural Clock Sync start must re-prime, with its first output timestamp anchored to the first input timestamp of that segment. Clock Sync timestamps are continuous **within** each engine epoch, not necessarily across a reset. Other active-to-active changes use deterministic waveform comparison rather than independent mathematical verification of all modes.

```sh
audit="$PWD/build/hyperdsp-benchmark-allocations.so"
# Invoke only the benchmark with the preload, not the normal OBS application.
LD_PRELOAD="$audit" build/hyperdsp-benchmark-audio "$PWD/build/hyperdsp.so" "$PWD" --frames 120 --cold --telemetry --ladspa /usr/lib/ladspa/amp.so
for ch in 1 2 6 8; do
  rate=48000; if [ "$ch" -ge 6 ]; then rate=96000; fi
  LD_PRELOAD="$audit" build/hyperdsp-benchmark-audio "$PWD/build/hyperdsp.so" "$PWD" --frames 120 --rate "$rate" --channels "$ch" --transitions --telemetry --ladspa /usr/lib/ladspa/amp.so
done
```

The final six strict runs used the same Linux Release module (0c7d0ea); each measured 64,000 callbacks. The last run repeated the warm steady-state matrix to preserve compatibility with the previous benchmark. All six passed waveform/timestamp checks and observed zero allocator/free calls, including the first callback and structural adoption boundaries. Four structural runs covered 896 scheduled configuration changes (16 workloads x 2 phases x 7 changes x 4 layouts).

| Workload | Budget (us) | Largest p99 across phases (us) | Largest wall time (us) | Wall overruns | Thread CPU overruns |
|---|---:|---:|---:|---:|---:|
| 48 kHz stereo warm | 2,500 | 273.86 | 2,576.09 | 1 | 0 |
| 48 kHz stereo cold | 2,500 | 269.56 | 842.06 | 0 | 0 |
| 48 kHz mono structural | 2,500 | 137.25 | 3,697.97 | 1 | 0 |
| 48 kHz stereo structural | 2,500 | 269.58 | 1,459.83 | 0 | 0 |
| 96 kHz 5.1 structural | 1,250 | 799.94 | 5,968.34 | 12 | 0 |
| 96 kHz 7.1 structural | 1,250 | 1,077.00 | 2,642.08 | 17 | 0 |

Total: 384,000 callbacks, 31 wall-time budget overruns, zero measured thread-CPU budget overruns. Prior exploratory runs also showed occasional wall overruns. The elapsed/CPU distinction is consistent with scheduling interruptions, but not a trace of their exact cause. Largest initial cold callback in the final stereo cold run: 42.94 us (Color). Largest structural-boundary callback, including initial adoption: 170.23 us (Color, 7.1); zero allocation/free calls across all measured boundaries. On the 7.1 workload, the three-effect graph's p99 reached 86% of its 1.25 ms budget and averaged about 19% of the audio-thread CPU budget while alternating active and dry graph topologies. The spectral FFT hop is the principal measured workload to profile next; the average does not describe peak cost.

Still unqualified: multiple simultaneous audio sources, sustained paced workload, real-time scheduler traces, live rate/layout changes inside one instance (these layouts were separate processes), other LADSPA plugins, and cross-platform/desktop device behavior. This is a test-only change, not an optimization or a claim of end-to-end hard real-time safety.

## FFT optimization, 2026-11-27

The radix-2 FFT now uses stage-contiguous precomputed twiddles and explicit finite-input real/imaginary butterflies, with forward and inverse kernels specialized at compile time. GCC 13 Release optimization reports 16-byte vectorization of the butterfly loop without extra ISA flags or fast-math. The 1024-point FFT table grows by approximately 4 KiB per instance; construction remains on the preparation path, and no allocation occurs during transforms. The 32768-point graph preview's FFT and impulse workspace are heap-owned on the isolated preview worker, not the audio thread. The existing noinline spectral hop boundary from main (bcbb632) is carried forward to preserve packet partition behavior under FMA. No dependency or platform-specific intrinsic was introduced.

Three alternating runs per build/effect, Linux Mint, Ryzen 7 7700, GCC 13.3 Release, 120 frames, 96 kHz, 7.1, warm always-active effect, concurrent no-op settings updates and telemetry where supported. Baseline is the saved production module before this FFT change, at 79bacd3 (runtime source identical to 0c7d0ea), SHA256 e9ce09fa646e19cd1ff81138146cf91e8a5dea02902e4c7c63293bc7d1288c9a. Median of the three concurrent-phase results:

| Workload | Old p99 (us) | New p99 (us) | Old CPU budget | New CPU budget |
|---|---:|---:|---:|---:|
| spectral_gate | 1046.88 | 275.66 | 34.463% | 8.217% |
| graph_chain | 1205.47 | 357.45 | 39.046% | 12.507% |

Both builds measured 24,000 callbacks total (all phases); observed zero allocation/free calls and zero mismatches against each build's own reference, not a bitwise cross-build waveform comparison. Wall-time overruns fell from 156 to 8 across the combined cases and phases (not a guaranteed scheduling improvement). No measured thread-CPU budget overruns in either build. The optimized spectral gate had zero wall overruns under edits; graph had four. CPU and wall timing remain machine/load dependent, and update counts vary because the writer is unpaced.

A separate finite-input microbenchmark pinned to CPU 6 used 30,000 successive 1024-point forward/inverse pairs with initial real=sin(i*.1f), imag=cos(i*.07f), GCC -O3. Baseline median 132.178 us/pair; optimized median 9.783 us/pair (13.5x). To reproduce, compile the following against each revision's src directory with `g++ -std=c++20 -O3 -Isrc bench.cpp -o bench`, then run `taskset -c 6 ./bench` three times:

```cpp
#include "framework/dsp/fft.hpp"
#include <chrono>
#include <cstdio>
int main(){
    hyperdsp::FFT<1024> fft;
    std::array<std::complex<float>,1024> x{};
    for(unsigned i=0;i<x.size();++i)x[i]={std::sin(i*.1f),std::cos(i*.07f)};
    auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<30000;++i){fft.transform(x);fft.transform(x,true);}
    auto elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
    std::printf("FFT_PAIR_US=%.3f CHECKSUM=%g\n",elapsed/30000,x[12].real());
}
```

For production-path A/B, save the previous module before rebuilding and alternate invocations for the two modules, using `--frames 120 --rate 96000 --channels 8 --telemetry --case spectral_gate` and `--case graph_chain` with the allocator audit described above.

New independent double-precision direct-DFT test covers forward/inverse/round-trip at 2, 4, 8, 32 and 1024 points, impulse/DC/tone/random/low-level inputs. For the 1024-point case the maximum forward absolute error was 2.13073e-5 and inverse 2.08079e-8 on both implementations with the baseline compiler flags. GCC 13 x86-64-v3 builds of FFT and spectral core tests pass, including zero observed partition error; this does not substitute for GCC 15 or native Windows/macOS qualification. The isolated preview also passed on a 512 KiB Linux process stack after moving its large workspace off-stack (the pre-change candidate crashed under that limit).

## Paced simultaneous-source qualification, 2026-11-27

The opt-in benchmark now accepts `--sources 2|4|8` with `--case graph_chain` or `--case spectral_gate`. Each source has a separate real OBS source/filter instance, a distinct deterministic input, an independently rendered unpaced reference, and a dedicated producer thread. The paced phase uses fresh OBS sources and filters, aligned absolute release times, 100 warmup packets and 2,000 measured packets per source. One control thread polls available telemetry at 20 Hz and applies a no-op settings update at 10 Hz. All producer-owned buffers and counters are separate; the callback instrumentation uses a thread-local probe. No change to production DSP.

```sh
LD_PRELOAD="$PWD/build/hyperdsp-benchmark-allocations.so" \
  build/hyperdsp-benchmark-audio "$PWD/build/hyperdsp.so" "$PWD" \
  --case graph_chain --sources 8 --channels 8 --rate 96000 --frames 120 --telemetry
```

The callback timing excludes host routing and fixture comparison. Host submission timing includes the full synchronous obs_source_output_audio call and callback instrumentation/comparison. Wake lateness measures actual submission start relative to the absolute packet release; completion lateness measures submission return relative to that release. Deadline misses count completions after the next scheduled release, not observed device underruns. Combined CPU budget is summed measured callback thread CPU divided by nominal audio duration, in percent of one core (can exceed 100%). Producers use ordinary OS scheduling; no realtime priority or affinity changes. This is not a mixer/output-device test: the independent synthetic sources are not routed to a live scene or captured output.

Final runs on Ryzen 7 7700/Linux Mint, Release, glibc allocation audit:

| Build / workload | Sources | Block budget (us) | Combined CPU (%) | Worst source callback p99 (us) | Callback overruns | Host overruns | Submission deadline misses |
|---|---:|---:|---:|---:|---:|---:|---:|
| optimized graph, 48 kHz stereo, 120 | 4 | 2500 | 8.490 | 223.62 | 0 | 0 | 80 |
| optimized graph, 48 kHz stereo, 480 | 8 | 10000 | 15.285 | 1223.00 | 1 | 1 | 18 |
| pre-FFT graph, 96 kHz 7.1, 120 | 8 | 1250 | 325.636 | 4956.08 | 1795 | 2096 | 8671 |
| optimized graph, 96 kHz 7.1, 120 | 8 | 1250 | 115.041 | 969.27 | 78 | 152 | 1860 |
| optimized spectral gate, 96 kHz 7.1, 120 | 8 | 1250 | 74.121 | 1005.92 | 65 | 128 | 2357 |

All five runs passed the strict output and allocation checks; 56,000 paced measured callbacks on the optimized module and 16,000 on the pre-FFT module. The eight-source stereo run covered 20 seconds of measured audio per source, 160 source-seconds. The surround A/B reduced combined callback CPU by approximately 64.7%; scheduling misses remain significant in the deliberately synchronized 1.25 ms case. These are single-run observations, not statistical estimates of underrun probability. The initial exploratory runs reused OBS source timestamp state; the final measurements recreate sources for each phase to avoid host timestamp discontinuities. Future work: real mixed output capture, realistic staggered sources, scheduler tracing where permitted, and native platform qualification. Do not interpret BENCH_PASS as a deadline guarantee.
