# HyperDSP

Standalone deterministic audio effects and DSP plugin host for OBS. HyperDSP is the audio counterpart to HyperShader, not a Live Latent Lab submodule.

## Ownership boundary (Mat, 2026-11-24)

- HyperDSP: conventional deterministic audio processing, programmable DSP, native audio effects and their controls.
- HyperShader: conventional deterministic video processing and shaders.
- Live Latent Lab.Sight: video/vision AI and ML, including inference and derived signals.
- Live Latent Lab.Sound: audio AI and ML, including inference and derived signals.
- Aftermarket: optional UI, mixer and routing integration; not a competing DSP engine.

HyperDSP and HyperShader can optionally consume signals from either Sight or Sound. AI-derived control data does not make the consuming effect an ML effect. No required model runtime, neural provider, or dependency on either Lab plugin; absent, disabled or stale signals must degrade safely.

Use existing versioned OBS procedure interfaces and stable source UUIDs where applicable, rather than a new broker by default. Discover/read on the control or worker thread, publish bounded validated parameter snapshots to the audio callback; no model execution, blocking IPC, discovery or allocation in the callback. Keep missing input policy explicit per effect. Signal-driven processing is deterministic conditional on the audio and control inputs; this does not imply neural upstream results are reproducible.

Current evidence: HyperShader already has an optional Sight client (src/interop/sight_client.h); Sound publishes versioned procedures documented in docs/sound-signal-interop-v1.md. HyperDSP has no signal client yet. Mic DSP is ported and verified in isolation and alongside YAMNet-only Sound; full migration and install remain outstanding.

First native host: LADSPA, verified against the installed SDK amplifier through actual libobs stereo capture, including stereo and duplicated mono instances, live parameter updates and dry failure. No neural dependencies. JSFX candidate YSFX was fetched for inspection only; Apache-2.0 vs inherited GPL-2.0 compatibility is unresolved.

LADSPA discovery now uses an explicit separate-process probe per library, atomic user JSON catalog, and native OBS search/category/effect controls. Catalog selection and default reset verified through real libobs audio capture. Local scan: 273 eligible descriptors across 215 libraries, zero failed probes; this does not qualify every effect. No GUI visual smoke or live installation yet.

LADSPA now uses backend-independent MixSettings for linear wet/dry and ±24 dB input/output gain. Defaults preserve prior effects. Failed/unavailable effect leaves original audio untouched; nonfinite wet samples fall back to dry. Actual OBS tests cover mix and trims. No latency compensation or parameter ramps yet. Sound v1 declaration and docs inspected; reads stay on control/worker paths. API Documentation Checker did not evaluate native OBS procedures (zero items); do not claim contract coverage from it.

MixModulator now provides shared sample-clocked sine LFO for wet/dry and output amplitude, 10 ms linear ramps for mix/gain/depth, and preallocated per-sample mix frames shared across voices. Core phase/ramp tests and actual OBS 2 Hz stereo tremolo test pass; no model dependencies. Native port modulation, external signals, envelopes and latency alignment remain outstanding.

Native HyperDSP Filter registered independently from LADSPA. TPT SVF modes, fractional comb, 12–48 dB/oct cascades, mixer, LFO (4 waveforms), linked peak-envelope follower, one modulation destination (frequency/Q/EQ gain/mix/output), smoothing and per-channel state. Numerical response and actual OBS audio verified, including envelope auto-wah. See docs/roadmap.md for preserved broader suggestions; not all implemented. Full production install, external signal binding, arbitrary modulation matrix and additional plugin formats remain pending.

Optional normalized Butterworth alignment added for 2/4/6/8-pole low/high-pass; stage Q and prewarped response independently evaluated using Wolfram Language. Numerical response matches reference and real OBS alignment switch is verified. Existing resonant default preserved. Q is ignored in fixed alignment; topology changes reset state. See docs/filter-math.md.

Reusable BandEnergy detector and native modulation bindings added: bass/mid/treble at fixed 250/2500 Hz crossovers, 24 dB/oct detector edges, linked maximum channel power, asymmetric attack/release and ±24 dB sensitivity. Core tested at 44.1/48/96 kHz with phase-opposed stereo; real OBS capture verifies bass and treble response and rejection after release settles. No LADSPA band bindings or external producers yet.

LADSPA reactive routing added using shared BandEnergy and reusable ReactiveSource (four LFO shapes, envelope, three bands). Signed parameter-unit amount; mix/output sample-clocked, continuous port modulation averaged per <=64-sample run. Port binding identity includes path/label/index and does not retarget on effect change. Real OBS SDK amplifier test verifies gain modulation, rejection, stale destination, output modulation and disable; old LFO retained. No general matrix or Sight/Sound bindings yet.

LADSPA matrix now supports four independent reactive slots, summing same-destination contributions before bounds, independent sources, signed amounts and stable effect-specific identities. First-slot saved keys retained; other slots default off. Real OBS SDK amplifier regression verifies simultaneous plugin-port and output modulation, additive same-port routes, cancellation by fourth route, disable/restoration and existing behavior. Native filter matrix and external signals remain pending.

Shared ModulationMatrix now owns four independent source states, signed depth ramps and sparse destination contributions for native filters and LADSPA. Native filters have four routes, with original first-slot keys preserved and extras default off. Backend-specific parameter bounds and LADSPA sub-block averaging retained. Native source changes reset state and disabled routes stop immediately. Core tests and real OBS native/LADSPA multi-route capture verify summation, cancellation and restoration. No external signals or new host formats yet.

Optional Sound category confidence source added to all four native/LADSPA routes. Existing v1 category stream with stable parent/filter UUID, worker-side reads, atomic timestamped score, 500 ms consumer expiry, 10 ms external ramps. Contract fixture through actual libobs verifies native and LADSPA audio, disabled/detached/missing/slow producer fallback and recovery. Actual neural model and installed frontend not yet tested; serial worker can be delayed by blocking producer. See docs/sound-interop.md.

Shared external adapter now supports Sight motion energy, hand X/Y and pinch closeness (source IDs 7–10) on all native/LADSPA routes. Motion uses parent/filter UUID; tracking uses Sight named hand channels with inventory uniqueness/enabled validation. Current producer pinch is closeness, not distance. Both wire headers copied unchanged from Sight d140d38. Actual libobs contract fixtures verify audio routing, invalid ABI/dimensions, ambiguous/absent/detached producers and picker. Live models/frontend/GPU lifecycle/CPU overhead unqualified. See docs/sight-interop.md.

Future direction proposed by user: Effect Builder dock analogous to HyperShader Shader Builder. Preserve native DSP expansion and optional neural signal boundaries; build validated reusable effect graph and parameter metadata before UI. See docs/effect-builder.md. No dock implementation yet.

Native HyperDSP Dynamics added: linked compressor, downward expander, noise gate (hold/hysteresis), sample-peak limiter, bypass, shared mix/gain and four modulation routes including optional Sight/Sound. Reusable OBS route property helper for future effects. Core curves/transients/gate behavior and real libobs stereo plus Sound contract fixture verified. Limiter wet branch only, no lookahead/true peak or guaranteed safety under callback bypass. See docs/dynamics.md. Next native saturation/time effects.

Native HyperDSP Color adds tanh saturation, cubic/hard clipping, triangle fold, signed 2–16 bit quantization and 1–64 sample hold; optional 2×/4× filtered FIR oversampling with 24-sample aligned dry path. Shared four routes, optional Sight/Sound, reusable OBS route properties. Core alias bin reduction, alignment, block continuity and actual OBS stereo/fixture tests verified. No scene-wide latency compensation, live CPU profile or GUI/install yet. See docs/color.md. Next delay/modulated time effects.
