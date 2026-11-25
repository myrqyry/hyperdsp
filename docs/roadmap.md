# HyperDSP development direction

Standalone conventional audio effects and hosting for OBS, with optional inputs from Sight and Sound. The following captures the agreed direction; planned items are not shipped features.

## Verified foundation

- Mic DSP extraction (coordinated production migration still pending).
- Native low/high/band-pass, notch, peak, shelves, comb, all-pass; 12–48 dB/oct resonant and normalized Butterworth cascades for low/high-pass.
- Native filter LFO and peak-envelope cutoff/Q/gain/mix/output bindings (one source/destination).
- LADSPA hosting, searchable categorized catalog and parameter controls.
- Shared wet/dry, input/output gain and smoothing for mixer controls.
- LADSPA sine LFO for mix/output; existing OBS serial filter stack.

## Next layers

- Favorites, reusable presets, reset/import/export, better effect descriptions and a compact native frequency-response view.
- Reusable modulation sources and destination registry across hosts, additional waveforms for LADSPA, frequency-band energy, multiple simultaneous bindings, native port smoothing and a routing matrix.
- Optional Sight and Sound signal consumers using their current versioned OBS procedures and stable UUIDs, with missing/stale-data fallback. No inference in HyperDSP and no producer calls on the audio callback.
- Dedicated serial/parallel effect chain editor, latency compensation and alignment; sidechains and appropriate oversampling for nonlinear effects.
- Additional creative filters/effects: formant resonances, morphing responses, spectral gate; auto-wah can already be configured with the native envelope-controlled filter.
- Other hosting formats: JSFX (license compatibility review of YSFX), Faust, CLAP, LV2. No unsupported format claims.
- Finish coordinated Sound Mic DSP ownership migration, isolated coexistence validation, installation and actual OBS UI/audio smoke tests.
