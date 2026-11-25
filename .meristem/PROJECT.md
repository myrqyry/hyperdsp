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
