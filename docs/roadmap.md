# HyperDSP development direction

Standalone conventional audio effects and hosting for OBS, with optional inputs from Sight and Sound. The following captures the agreed direction; planned items are not shipped features.

## Verified foundation

- Mic DSP extraction (coordinated production migration still pending).
- Native low/high/band-pass, notch, peak, shelves, comb, all-pass; 12–48 dB/oct resonant and normalized Butterworth cascades for low/high-pass.
- Native filter LFO, peak-envelope and bass/mid/treble energy cutoff/Q/gain/mix/output bindings (four independent routes).
- LADSPA hosting, searchable categorized catalog and parameter controls.
- Shared wet/dry, input/output gain and smoothing for mixer controls.
- LADSPA sine LFO for mix/output; four independent reactive routes (four LFO shapes, envelope, bass/mid/treble) to mix/output or continuous plugin input ports; existing OBS serial filter stack.

- Shared fixed-capacity modulation matrix across native filters and LADSPA.

- Optional Sound classification confidence inputs on both effect backends, via the shared matrix and versioned producer procedure.

- Optional Sight motion and hand signals (X/Y and pinch closeness) on both backends through the shared external adapter.

- Native compressor, downward expander, gate and sample-peak limiter with four shared modulation routes.

- Native tanh saturation, cubic soft clip, hard clip, wavefolding and bitcrusher/sample hold; optional 2×/4× filtered oversampling and aligned parallel mix.

- Native delay/echo, ping-pong, two-tap chorus, flanger and vibrato with shared fractional-delay primitive and four reactive routes.

- Native 2–12-stage phaser and stereo four-line feedback-network reverb with input diffusion, nominal decay, damping, pre-delay, room size and width.

- Native FFT spectral gate, phase-advancing freeze and temporal magnitude smear, with normalized overlap-add and internally aligned dry path.

- Native peak-region phase-vocoder pitch shift and additive frequency shift within the spectral processor; shared modulation destinations and fixed latency.

## Next layers

- Favorites, reusable presets, reset/import/export, better effect descriptions and a compact native frequency-response view.
- Extend the shared modulation matrix with a destination registry, configurable detector bands, additional backends, external signals and native port smoothing.
- Extend optional interop to more Sight and Sound signals, handedness selection, Sound category picker and full live-producer qualification; retain versioned procedures, stable UUIDs and missing/stale-data fallback. No inference in HyperDSP and no producer calls on the audio callback.
- Dedicated serial/parallel effect graph and eventual Effect Builder dock, following HyperShader’s existing Shader Builder interaction and recovery patterns. See [Effect Builder direction](effect-builder.md). Latency compensation and alignment; sidechains and appropriate oversampling for nonlinear effects.
- Reverb quality enhancements (denser diffusion, smoother room retiming, convolution); additional delay enhancements (tempo sync, crossfaded retiming) and additional creative filters/effects: formant resonances, morphing responses, formant preservation, higher-quality pitch/transient processing and adaptive noise profiles; auto-wah can already be configured with the native envelope-controlled filter.
- Other hosting formats: JSFX (license compatibility review of YSFX), Faust, CLAP, LV2. No unsupported format claims.
- Finish coordinated Sound Mic DSP ownership migration, isolated coexistence validation, installation and actual OBS UI/audio smoke tests.
