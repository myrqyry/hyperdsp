# HyperDSP

Standalone deterministic audio effects and DSP hosting for OBS Studio, the audio counterpart to HyperShader. It is not part of Live Latent Lab.

## Plugin family

| Plugin | Responsibility |
| --- | --- |
| HyperDSP | Deterministic audio effects |
| HyperShader | Deterministic video effects |
| Live Latent Lab.Sound | Audio AI/ML and derived signals |
| Live Latent Lab.Sight | Video AI/ML and derived signals |

Both deterministic effect plugins may optionally consume data published by either Lab plugin without owning inference or requiring its installation. HyperDSP must keep working independently. Future signal consumers should reuse the existing versioned OBS interfaces, read outside the real-time audio callback, and safely handle missing or stale inputs. Signal integration in HyperDSP is planned, not implemented.

The first verified target is the microphone coloration filter: dynamic, condenser, ribbon, telephone, vintage radio, tape and dry bypass, with spectral sweep and resonance controls. These are procedural tones, not measured microphone reproductions.

## Build and test (Linux)

Requires a C++20 compiler, CMake, OBS development headers, libobs, and LADSPA SDK headers (on Mint: ladspa-sdk). Run `tests/verify.sh` for the DSP unit test and real OBS post-filter stereo capture test. No model, download, audio device or neural dependency is needed.

## Native filter

Add **HyperDSP Filter** to an OBS audio source. The native topology-preserving state-variable engine provides low-pass, high-pass, band-pass, notch, peaking EQ, low/high shelves and all-pass, plus a fractional-delay feedforward comb and bypass. It has independent state per channel and shares parameter changes across channels. Low/high-pass cascades offer 12, 24, 36 and 48 dB/octave asymptotic slopes with two response alignments. Resonant cascade (existing default) uses adjustable Q per identical stage. Butterworth uses fixed stage-specific Q and a maximally flat passband with a −3.01 dB cutoff at every slope. Q controls and Q modulation do not affect Butterworth; shelves use fixed slope. Changing alignment resets filter state. Comb frequency sets notch spacing, depth controls cancellation.

Frequency (20–20,000 Hz, capped at 45% of sample rate), Q (0.25–12), EQ gain (±24 dB) and comb depth ramp over 10 ms. Input/output gain and wet/dry use the shared mixer; bypass is unprocessed audio. Mode and stage changes reset filter state, not crossfade. Supported sample rates are 8–192 kHz; unsupported or changed OBS audio formats bypass until reconfigured. This is conventional deterministic DSP with no inference dependencies.

Native modulation supports sine, triangle, square and saw LFOs (0.05–20 Hz), and a linked pre-trim peak-envelope follower with independent attack/release. Four independently configurable routes can bind to cutoff (±4 octaves per route), Q (±2 octaves), EQ gain (±12 dB), wet/dry or output amplitude. Same-destination contributions sum before destination bounds. Envelope signals are positive-only, but signed route depths allow upward or downward modulation. Amount edits on the same route ramp over 10 ms; source or destination changes start the new amount from zero and disabling a route removes its contribution immediately. Source changes reset detector/oscillator state; waveform changes are not crossfaded. Route 1 retains the original settings keys; routes 2–4 default off. For auto-wah, select low-pass or band-pass, a low cutoff, input envelope as source, cutoff as destination and increase depth. Bass, mid and treble energy are additional positive-only sources, with fixed crossovers at 250 and 2500 Hz (24 dB/oct detector edges, not brick-wall bands). The detector takes the largest per-channel squared filtered sample, applies attack/release smoothing, then converts to amplitude and applies ±24 dB sensitivity, clamped to 0–1. Opposite-phase stereo does not cancel. The detector is pre-trim and does not filter the audible signal itself. All five existing destinations work with it, subject to the selected filter's applicable parameters. Filter effects can be chained using OBS's ordinary filter stack. Dedicated parallel chains, arbitrary routing, oversampling and a live frequency-response graph are not yet implemented.

The numerical suite measures all nine processing modes (plus channel isolation, bounded modulation and envelope response). The OBS suite measures actual low-pass/high-pass, notch, steep cascade, dry bypass, blend and envelope-driven cutoff. It does not qualify all possible settings or measure real-time CPU budgets.

## Native dynamics

Add **HyperDSP Dynamics** to an OBS audio source for compressor, downward expander, noise gate, sample-peak limiter or unprocessed bypass. The reusable DSP engine links channels through their maximum absolute input sample, with instantaneous detector rise and exponential decay, and applies a common smoothed gain to preserve stereo balance. Compressor and expander have adjustable ratio (1:1–20:1), threshold (−80–0 dBFS), soft knee (0–24 dB), attack and release. Expander and gate offer attenuation range; gate additionally has hold and hysteresis. Makeup, input/output trim and wet/dry mixing are included.

The limiter uses immediate attenuation and release smoothing, with a final wet-branch sample ceiling even with makeup applied. It has no lookahead or true-peak/intersample protection; dry mixing and output trim can exceed its ceiling, and the OBS adapter can bypass during a concurrent settings update or unsupported audio format. Do not use this as a guaranteed broadcast safety limiter. No separate sidechain or RMS detector yet.

All four shared modulation routes are available, including optional Sound and Sight signals. Destinations are threshold, ratio, makeup, attenuation range, mix and output amplitude, with signed amounts in destination units and per-destination bounds. Only parameters used by the current dynamics mode affect processing. A reusable OBS route-property helper supports future native effect adapters.

Numerical and real libobs tests cover static curves, stereo linkage, soft knee, gate hold/hysteresis, sample ceiling across 44.1/48/96 kHz, nonfinite sample handling, bypass and Sound-driven attenuation using the versioned contract fixture. See [dynamics verification](docs/dynamics.md).

## LADSPA filter

Add **HyperDSP LADSPA** to an audio source. Search the effect catalog by name, label or maker, narrow by category, and choose an effect from the native OBS properties. Switching catalog effects clears the previous effect's port values so the new effect starts at its own defaults. Manual trusted library path and exact label (for example, `/usr/lib/ladspa/amp.so` and `amp_stereo`) remain available. The filter generates numeric and toggle controls from port metadata. Mono effects use independent instances per OBS audio channel; stereo effects require a stereo OBS layout. Only equal-input/output mono or stereo effects declaring hard-real-time capability are accepted. Unsupported configurations, missing libraries and invalid labels bypass audio.

Use `analyseplugin /path/to/plugin.so` to see available labels. Only load trusted plugins: this backend executes native code in the OBS process, not a sandbox. The initial host does not provide preset import/export, latency compensation, sidechains, control-output meters or custom plugin interfaces. Categories are inferred from descriptor names/labels, not authoritative plugin metadata.

### Mix and gain staging

Each LADSPA filter also exposes input gain and output gain (−24 to +24 dB) and a linear wet/dry blend (0–1, default 1). Input gain precedes both the dry branch and the plugin, and output gain follows the blend. With a valid effect, zero wet still applies both trims; an invalid or unavailable effect instead passes the original audio unchanged. Nonfinite processed samples fall back to dry audio. The mixer is backend-independent code shared by LADSPA and the native filter. Input/output gain and wet/dry changes use a 10 ms linear sample ramp. Native LADSPA port changes remain block-boundary updates. Latency compensation and parallel alignment for delayed effects remain future work.

### Modulation

The LADSPA filter includes a sine LFO (0.05–20 Hz) with adjustable depth and destination: wet/dry or output amplitude (tremolo). Wet/dry modulation adds the bipolar LFO to the base value and clamps to 0–1; output amplitude is multiplied by 1 + depth × sine. Destination/depth changes ramp over 10 ms, and all channels share the same sample-clocked phase. Disabling modulation leaves the unmodulated effect settings intact. The phase freezes when both modulation depths are zero and resets on a new effect instance. This is a reusable deterministic modulation core, not yet a general-purpose routing matrix or a Sight/Sound consumer.

### Reactive parameter routing

The LADSPA filter additionally provides four independently configurable reactive routes, each with a source (sine, triangle, square or saw LFO, linked input envelope, or bass/mid/treble energy) and a destination: wet/dry, output amplitude, or an eligible continuous input control port of the selected effect. The signed amount is in the parameter's own units (up to ±10000), clamped to the host range; mix/output use ±1. Band sensitivity is ±24 dB. Detection is pre-trim. Native plugin ports are updated using the mean modulation value over sub-blocks of at most 64 samples; mixer destinations are sample-clocked. All channels receive the same modulation. Existing LFO settings remain independent and retain their previous behavior.

Port bindings include the library path, effect label and port index, so changing effects does not silently redirect a saved route. Incompatible destinations are inactive and shown as unavailable when inspecting a valid replacement effect. Integer/toggle controls are excluded. An amount edit on the same route ramps over 10 ms; disabling or replacing the route restores the base value immediately. Routes targeting the same destination are summed before clamping. Output amplitude contributions are summed before multiplying the base output gain; wet/dry contributions are additive. Each route has independent oscillator and detector state. The first route retains its original saved settings keys, and new routes default to off. Native filters and LADSPA now share the same fixed-capacity four-slot modulation matrix (source state, signed amount ramps, sparse destination contributions). Backend-specific bounds and LADSPA sub-block averaging remain in their respective adapters. Third-party effects may introduce their own parameter smoothing or latency.

### Optional Sound classification input

Both native filters and LADSPA routes can select **Sound category confidence** as a modulation source. Choose the attached Sound classifier by its parent/filter UUID (shown with its display name), enter the exact category label, choose a destination and set signed depth/amount. A negative depth can attenuate or close an effect when the selected category becomes active. Sound must already be producing category scores; HyperDSP neither starts inference nor loads a model. Without Sound, an unavailable category, a disabled/detached source or a stale snapshot, the external contribution returns to zero and the underlying effect stays active at its base settings.

The adapter reads the existing `sound_signal_read_v1` category stream via libobs on a single shared external-signal background polling thread (nominally every 100 ms). The audio path consumes a timestamped atomic score snapshot and ramps changes over 10 ms. Snapshots older than 500 ms on the consumer side are discarded, in addition to Sound's own availability/expiry checks. Polling is serial; a stalled producer can delay other subscribers, whose snapshots then also expire. No required Sound dependency, shared C++ ABI, model execution or producer call from the audio callback. The category entry is currently exact-label text, not a category browser. See [interop verification](docs/sound-interop.md).

### Optional Sight visual signals

Both backends accept Sight motion energy, hand X, hand Y and pinch closeness through the same four modulation routes. Motion is selected by the attached U2Net producer's parent/filter UUID and reads the source-local normalized luma-change energy, not physical velocity. Hand routes select a named hand publisher from Sight's tracking inventory. X/Y are the first reported hand's wrist coordinates in the producer's normalized video space; pinch is the producer's existing 0=open, 1=pinched signal. A first reported hand is not a persistent handedness identity. Motion and tracking can coexist with Sound category confidence and conventional audio modulation.

The adapter validates procedure version, buffer size, sequence, dimensions, kind, bounds and availability as appropriate to each contract. Duplicate, disabled, missing or unavailable tracking channels contribute zero. Sight snapshots are requested with a 500 ms maximum age; the shared worker also expires local snapshots after 500 ms and the matrix smooths changes over 10 ms. Named tracking channels follow Sight's own naming semantics (not UUIDs). No producer is rendered or started by HyperDSP. See [Sight interop verification](docs/sight-interop.md).

### Refresh the catalog

After building, run:

```sh
python3 tools/scan-ladspa.py --probe build/hyperdsp-ladspa-probe
```

The scanner checks standard system LADSPA directories (or explicitly supplied libraries/directories), starts a separate process per library with a three-second timeout, and atomically writes the user catalog to `~/.config/obs-studio/plugin_config/hyperdsp/ladspa-catalog.json` (respecting XDG_CONFIG_HOME). Reopen filter properties after scanning. Catalog reads do not execute plugin code; selecting a native plugin does. Process separation is crash isolation, not a security sandbox: scan only trusted libraries. A descriptor in the catalog is an eligibility result, not proof that its audio processing works.

The install target includes both scanner files under `tools/`; the installed Python script locates its sibling probe by default. The catalog is local user data, not a shipped list of absolute paths. The audio test uses the real LADSPA SDK amplifier when installed; without it the LADSPA integration test is not registered.

## Migration status

This development module retains the existing OBS source ID `mic_modeler_filter` and settings keys to preserve scene compatibility. **Do not install or load it alongside a Sound build registering the same ID.** The old module remains unchanged until a coordinated migration verifies both modules together. No changes to installed OBS plugins or scene collections are required for testing.

Run `tests/verify-coexist.sh /path/to/live-latent-lab-sound-public` to build a committed Sound snapshot with only YAMNet enabled, then verify both module load orders and real stereo processing with the original filter ID. This tests coexistence, not the entire expanded neural stack.

The production migration must remove Mic DSP from Sound's enabled effect sets, registration, verification receipts and installer checks together, requalify the expanded build, and install both modules while OBS is closed. Do not replace a running module or publish a second registration.

## Direction

A standalone OBS DSP host with programmable effects (JSFX/Faust candidates) and native audio plugin hosting (LADSPA implemented; CLAP/LV2 candidates). JSFX integration needs a license compatibility review before linking YSFX (Apache-2.0) to the inherited GPL-2.0 code. Faust, CLAP and LV2 are not implemented. See [the development roadmap](docs/roadmap.md) for the agreed broader direction and current implementation boundary.

## Provenance

Mic filter and DSP helpers extracted from myrqyry/live-latent-lab.sound at f2e18dc, including the original verification tests. GPL-2.0 license retained.
