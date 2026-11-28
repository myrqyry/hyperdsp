# HyperDSP

Standalone deterministic audio effects and DSP hosting for OBS Studio, the audio counterpart to HyperShader. It is not part of Live Latent Lab.

## Plugin family

| Plugin | Responsibility |
| --- | --- |
| HyperDSP | Deterministic audio effects |
| HyperShader | Deterministic video effects |
| Live Latent Lab.Sound | Audio AI/ML and derived signals |
| Live Latent Lab.Sight | Video AI/ML and derived signals |

Both deterministic effect plugins may optionally consume data published by either Lab plugin without owning inference or requiring its installation. HyperDSP must keep working independently. Future signal consumers should reuse the existing versioned OBS interfaces, read outside the real-time audio callback, and safely handle missing or stale inputs. HyperDSP already consumes optional Sound classification and Sight motion/hand signals via these versioned interfaces.

The first verified target is the microphone coloration filter: dynamic, condenser, ribbon, telephone, vintage radio, tape and dry bypass, with spectral sweep and resonance controls. These are procedural tones, not measured microphone reproductions. Profile stages use fixed-capacity storage and settings are adopted at block boundaries without a processing mutex.

Native Linux x86-64, Windows x64 and macOS (Intel/Apple Silicon) passed the full shared CI matrix against OBS 32.2.0 and 33.0.3 at the final PR #2 head, including the 59-case regression suite and package checks. Post-merge Apple Silicon test repeatability remains under investigation (see [CI qualification](docs/ci.md)). The native build retains
LADSPA hosting, Clock Sync and the Qt Effect Builder; Windows/macOS native device playback
for the isolated audition panel remains outstanding. See [CI qualification](docs/ci.md).

## Build and test (Linux)

Requires a C++20 compiler, CMake, OBS development headers, libobs, Jansson development headers/library, and LADSPA SDK headers (on Mint: libjansson-dev and ladspa-sdk; Clock Sync also uses libsamplerate0-dev). Run `tests/verify.sh` for a fresh Debug build and the configured CTest suite, including numerical DSP checks and real libobs audio capture. Qt widget tests require the Effect Builder dependencies (Qt 6 Widgets), not a separate Qt Test module; LADSPA integration and CLI fixture tests require the SDK `amp.so` test plugin and Python 3. No model, download, physical audio device or neural dependency is needed.

On a headless Linux host with Xvfb available, run `xvfb-run -a tests/verify.sh` to provide a display for libobs.

### Shared CI packages

[Shared OBS plugin CI](.github/workflows/ci.yml) targets Linux x86-64, Windows x64, macOS Intel and Apple Silicon
with pinned OBS 32.2.0 and 33.0.3 SDKs. All 59 configured tests, including
Qt Effect Builder, Clock Sync, real libobs audio and LADSPA discovery,
are required. The versioned packages contain the plugin, all 11 graph
presets, licensing and the installed effect-library/scanner tools.
The CI tests the extracted package as well as the build tree.
OBS32 uses `bin/64bit/hyperdsp.so`; OBS33 uses `hyperdsp.so` at the
plugin root. For local CMake installs, choose
`-DHYPERDSP_PACKAGE_LAYOUT=legacy` (default) or `modern` for the
matching target SDK. Do not install two versions together.
Windows packages use the corresponding native DLL layout; macOS packages use a signed-for-testing plugin bundle. Use the archive matching the installed OBS version and architecture. See [CI qualification](docs/ci.md) for platform evidence and playback limitations.

## Real-time processing

All 15 OBS filter adapters now adopt prepared settings or engines at block boundaries without a HyperDSP processing mutex or contention-induced dry fallback. Ordinary edits preserve DSP history; explicit structural changes can reset state and are not crossfaded. Control-side serialization, OBS host locks and third-party plugin behavior are separate. Graph/standalone telemetry publishes bounded snapshots without waiting on readers. Linux [callback profiling](docs/realtime-profiling.md) covers all adapters, allocation auditing, multiple sample rates/layouts, concurrent updates and real OBS mixer capture. The optimized FFT reduced measured spectral and graph callback CPU substantially; this is not a universal deadline guarantee.

## Clock Sync

**HyperDSP Clock Sync** is a standalone adaptive sample-rate filter for live hardware/network capture. It compensates small clock-rate differences before other DSP and neural filters; it is not a graph node because it changes packet length and timestamps. Choose source timestamps (default) or OBS arrival time, fast/medium/best sinc quality and a correction limit of 50–2000 ppm. Refresh diagnostics in the native OBS properties to read lock state, estimated correction, applied ratio, timing error and relocks.

Clock Sync is built by default with libsamplerate; use -DHYPERDSP_BUILD_CLOCK_SYNC=OFF to omit it. Apply only to appropriate live sources, not local media files or sources whose clocks are already corrected. Independent monitoring-device clocks are not synchronized. See [Clock Sync design and verification](docs/clock-sync.md).

## Effect Builder dock

Build with Qt 6 Widgets and the OBS frontend development library available (the default CMake option is HYPERDSP_BUILD_BUILDER=ON). The DSP-only module remains available with -DHYPERDSP_BUILD_BUILDER=OFF or when those dependencies are absent. No browser engine is required.

The dock follows the active OBS theme, including palette-derived canvas colors and live theme updates. Open **Docks → HyperDSP Effect Builder** in OBS. First add a **HyperDSP Graph** filter to the desired audio source, then refresh the dock targets and choose that filter. Load copies its saved draft into the editor. Browse native and scanned LADSPA effects, add nodes, select them in the graph, edit parameters and weighted inputs in Inspector, and choose the output. Validate prepares the candidate without changing live audio. Apply explicitly replaces the selected filter's graph only after successful preparation. Import/export operate on the versioned graph JSON; changes remain local drafts until Apply. External plugins execute trusted native code during preparation, including validation. Use the installed scan/reload actions to discover more local LADSPA effects.

Drag node bodies to arrange the canvas, and drag an output port to an input port to create a connection or reroute the final output. Select a cable and press Delete, or right-click and choose Disconnect. Connect another input before removing a node’s last cable. Ctrl+wheel zooms; Fit and Layout restore the view and default arrangement. The inspector still edits connection weights. Node positions are saved in a separate editor document alongside the draft; ordinary DSP graph JSON remains unchanged. In Controls, add a named macro, set its initial value, bind numeric parameters from any node, and set the endpoints at 0 and 1 (including reversed ranges). Apply generates native OBS filter sliders. In Modulation, add up to four routes from LFO, input envelope, bass/mid/treble energy or optional Sound/Sight signals to named controls. Set signed depth and source-specific parameters; external channels are discovered through the existing OBS signal inventory. Refresh inputs to rediscover producers. Apply activates the routes. The Monitor tab shows the selected applied graph’s real input/output waveforms, log-frequency spectrum, peak levels and manual/effective macro values. Waveforms and spectral difference use channel 1; the stereo vectorscope overlays paired input/output mid/side samples and reports output correlation and mono-fold loss. Node activity shows the applied graph’s actual compressor/gate/limiter detector and smoothed gain, multiband detector/gain meters, and the frozen wet Dynamic EQ response. All views clear stale data. The spectral difference is observed magnitude change, not an EQ transfer estimate. The Preview tab renders a synthetic test tone through a fresh native draft graph and measures the finite-impulse frequency response for stationary gain/filter graphs. Preview is silent, uses draft defaults, and does not touch the live filter; external LADSPA nodes are excluded. Audition renders dry and processed copies of a sine, pink noise, logarithmic sweep, or local PCM16 WAV, then plays either on an explicitly selected PulseAudio/PipeWire output using pactl and paplay (optional Linux runtime tools; native Windows/macOS audible playback is not implemented). It bypasses the OBS mixer, but desktop capture can still record that output; choose an uncaptured device. Both versions use the same listening attenuation and digital peak ceiling. The Presets tab offers eleven bundled native graph presets (voice, de-essing, parallel compression, radio, tape echo, envelope wah, rhythmic pulse, spectral textures, chorus and the two original examples), plus named local graph/layout presets. See the [preset guide](docs/presets.md) for controls and signal paths. Drafts and layouts autosave locally, recover when reopening the dock, and retain one previous draft on explicit replacement. Restored sessions require explicit target selection. Import/export of ordinary graph JSON remains compatible. There is no community download integration or external latency compensation. Last-good filter recovery continues to work through OBS scene saves. See [Effect Builder](docs/effect-builder.md).

## Effect graph runtime

**HyperDSP Graph** is an initial executable serial/parallel effect graph filter. Its versioned JSON definition is editable in OBS filter properties; the graphical Effect Builder is available through the optional Qt dock. The graph supports all 43 native processing modes across twelve families, and a gain/weighted-sum utility. Scanned compatible LADSPA effects can also be used as graph nodes or standalone filters. External native code is trusted code, not sandboxed.

The graph has a single audio input (source index -1), up to 16 nodes and 32 weighted edges, and a selected output. It rejects cycles, disconnected nodes, unsupported effects, non-finite/out-of-range parameters and invalid connections. Parallel paths are aligned to the greatest fixed algorithmic latency at each merge, including Color oversampling and Spectral FFT buffering. Valid graphs are prepared off the audio callback and swapped in; an invalid edit retains the previous active graph; OBS saves its last accepted definition and base control values for recovery on reload. The callback processes bounded chunks with preallocated buffers. A valid replacement resets graph state; same-graph control edits preserve state without a processing mutex; there is no crossfade or scene-wide timestamp alignment.

Example: parallel saturated and dry branches. Paste the JSON into the graph filter's properties:

```json
{
  "schema": 1,
  "nodes": [
    {"effect": "native:hyperdsp_color_filter:1", "params": {"drive": 12, "oversample": 2},
     "inputs": [{"source": -1, "weight": 1}]},
    {"effect": "gain", "params": {"gain_db": 0},
     "inputs": [{"source": 0, "weight": 0.5}, {"source": -1, "weight": 0.5}]}
  ],
  "output": 1
}
```

### Exposed graph controls

An effect author can expose up to eight named sliders (0–1), each mapping to one or more continuous internal parameters. Add a top-level controls array to the graph JSON, for example:

```json
"controls": [
  {"id": "color", "name": "Color", "default": 0.25,
   "bindings": [
     {"node": 0, "parameter": "drive", "low": 0, "high": 24},
     {"node": 1, "parameter": "gain_db", "low": 0, "high": -6}
   ]}
]
```

HyperDSP generates native OBS sliders from the accepted graph. Moving a slider updates the existing engines without resetting the graph, preserves delay/reverb history, and does not change fixed branch alignment. Endpoints may be reversed. Control values persist with OBS settings under graph_control_<id>; use stable IDs for the same semantic control. Native continuous settings use their existing smoothing where applicable, gain uses a 10 ms ramp, and external continuous ports update at block boundaries. Up to four graph modulation routes can drive macros from LFO, input envelope, band energy, Sound confidence or Sight motion/hand signals. Detectors advance per sample; control mappings update on a fixed 64-sample clock. Load a [bundled preset](docs/presets.md) in the Effect Builder's Presets tab, or copy its JSON from [examples/graphs](examples/graphs/) into the graph filter. The definitions are installed under `data/graphs/`.

Shared native parameter definitions drive OBS controls, graph validation and the effect-library JSON metadata. LADSPA nodes use the same host engine as the standalone filter and validate live port hints before activation. Use the catalog ID from the JSON inventory, with numeric port_N parameters and optional mix/input_db/output_db. Only locally scanned IDs are accepted. LADSPA latency is unknown and not compensated; native fixed-latency alignment remains active. See [graph contract and verification](docs/effect-graph.md) for limitations and schema.

## Native filter

Add **HyperDSP Filter** to an OBS audio source. The native topology-preserving state-variable engine provides low-pass, high-pass, band-pass, notch, peaking EQ, low/high shelves and all-pass, plus a fractional-delay feedforward comb and bypass. It has independent state per channel and shares parameter changes across channels. Low/high-pass cascades offer 12, 24, 36 and 48 dB/octave asymptotic slopes with two response alignments. Resonant cascade (existing default) uses adjustable Q per identical stage. Butterworth uses fixed stage-specific Q and a maximally flat passband with a −3.01 dB cutoff at every slope. Q controls and Q modulation do not affect Butterworth; shelves use fixed slope. Changing alignment resets filter state. Comb frequency sets notch spacing, depth controls cancellation.

Frequency (20–20,000 Hz, capped at 45% of sample rate), Q (0.25–12), EQ gain (±24 dB) and comb depth ramp over 10 ms. Input/output gain and wet/dry use the shared mixer; bypass is unprocessed audio. Mode and stage changes reset filter state, not crossfade. Supported sample rates are 8–192 kHz; unsupported or changed OBS audio formats bypass until reconfigured. This is conventional deterministic DSP with no inference dependencies.

Native modulation supports sine, triangle, square and saw LFOs (0.05–20 Hz), and a linked pre-trim peak-envelope follower with independent attack/release. Four independently configurable routes can bind to cutoff (±4 octaves per route), Q (±2 octaves), EQ gain (±12 dB), wet/dry or output amplitude. Same-destination contributions sum before destination bounds. Envelope signals are positive-only, but signed route depths allow upward or downward modulation. Amount edits on the same route ramp over 10 ms; source or destination changes start the new amount from zero and disabling a route removes its contribution immediately. Source changes reset detector/oscillator state; waveform changes are not crossfaded. Route 1 retains the original settings keys; routes 2–4 default off. For auto-wah, select low-pass or band-pass, a low cutoff, input envelope as source, cutoff as destination and increase depth. Bass, mid and treble energy are additional positive-only sources, with fixed crossovers at 250 and 2500 Hz (24 dB/oct detector edges, not brick-wall bands). The detector takes the largest per-channel squared filtered sample, applies attack/release smoothing, then converts to amplitude and applies ±24 dB sensitivity, clamped to 0–1. Opposite-phase stereo does not cancel. The detector is pre-trim and does not filter the audible signal itself. All five existing destinations work with it, subject to the selected filter's applicable parameters. Filter effects can be chained using OBS's ordinary filter stack. The standalone native filter does not expose arbitrary internal routing or oversampling. HyperDSP Graph provides parallel chains; Color offers oversampling, and the Builder Preview measures stationary native filter responses.

The numerical suite measures all nine processing modes (plus channel isolation, bounded modulation and envelope response). The OBS suite measures actual low-pass/high-pass, notch, steep cascade, dry bypass, blend and envelope-driven cutoff. It does not qualify all possible settings or measure real-time CPU budgets.

## Native dynamics

Add **HyperDSP Dynamics** to an OBS audio source for compressor, downward expander, noise gate, sample-peak limiter or unprocessed bypass. The reusable DSP engine links channels through their maximum absolute input sample, with instantaneous detector rise and exponential decay, and applies a common smoothed gain to preserve stereo balance. Compressor and expander have adjustable ratio (1:1–20:1), threshold (−80–0 dBFS), soft knee (0–24 dB), attack and release. Expander and gate offer attenuation range; gate additionally has hold and hysteresis. Makeup, input/output trim and wet/dry mixing are included.

The limiter uses immediate attenuation and release smoothing, with a final wet-branch sample ceiling even with makeup applied. It has no lookahead or true-peak/intersample protection; dry mixing and output trim can exceed its ceiling, and the OBS adapter can bypass during a concurrent settings update or unsupported audio format. Do not use this as a guaranteed broadcast safety limiter. No separate sidechain or RMS detector yet.

All four shared modulation routes are available, including optional Sound and Sight signals. Destinations are threshold, ratio, makeup, attenuation range, mix and output amplitude, with signed amounts in destination units and per-destination bounds. Only parameters used by the current dynamics mode affect processing. A reusable OBS route-property helper supports future native effect adapters.

Numerical and real libobs tests cover static curves, stereo linkage, soft knee, gate hold/hysteresis, sample ceiling across 44.1/48/96 kHz, nonfinite sample handling, bypass and Sound-driven attenuation using the versioned contract fixture. See [dynamics verification](docs/dynamics.md).

## Standalone live analysis

The native **HyperDSP Dynamics**, **Multiband**, **Transient**, and **Dynamic EQ** filter properties include **Open live analysis** when the optional Qt frontend is built. The modeless window follows that exact filter and uses the same plots as the Effect Builder. Dynamic EQ additionally supports a draggable frequency/gain-limit handle, scroll-to-adjust Q, and numeric parameter editing directly in the window. Multiband adds a draggable crossover map and per-band threshold, ratio and makeup editing. Transient shares that map in its three-band mode and adds signed contour/gain meters, independent attack/sustain gains and shared timing controls in both modes. Visualizations include: actual detector peaks and smoothed gain reduction, independent band meters, or a frozen wet Dynamic EQ curve. It stops polling when hidden and closes when its source is destroyed. DSP-only builds retain the telemetry procedure without a GUI dependency. See [standalone analysis](docs/filter-analysis.md).

## Dynamic EQ / de-essing

**HyperDSP Dynamic EQ** uses a frequency-selective peak detector to attenuate or boost loud components around an adjustable center frequency. Set the Q, threshold, ratio, signed dynamic gain limit, knee and attack/release; input/output trim and wet/dry are shared with the other native effects. The same engine is available as a graph node with eleven macro-bindable parameters. The bundled Vocal De Esser preset starts at 6.5 kHz, with a −9 dB dynamic cut limit and adjustable depth, sensitivity and frequency. No lookahead, external sidechain, fixed buffering latency or internal peak limiter. See [dynamic EQ design and tests](docs/dynamic-eq.md).

## Transient shaping

**HyperDSP Transient** changes attack and decay contours independently with signed gain controls (−12 to +12 dB), adjustable contour times, input/output trim and wet/dry. Its linked-channel peak-envelope contrast detector preserves channel balance, responds immediately to onsets, and tends toward neutral on stationary input. Broadband mode retains its seven macro-bindable parameters. Three-band mode adds independent low, mid and high attack/sustain gains and adjustable crossovers, using the same phase-matched processing bank as Multiband compression. No lookahead, fixed buffering latency or peak limiter. See [transient design and tests](docs/transient.md).

## Stereo imaging

**HyperDSP Stereo** provides width (0–2×), mid and side gains, balance, wet/dry and trim. Width zero folds stereo to its mono sum; widening changes the side component without changing the mid component unless other controls are adjusted. The native filter and graph node use the same ramped, zero-buffer-latency engine. Stereo-only: standalone bypasses other layouts; graphs reject them. Refresh diagnostics in native OBS properties to inspect output correlation, mono-fold power loss and side energy. These measurements are not a guarantee of mono compatibility or a limiter. See [stereo design and tests](docs/stereo.md).

## Multiband compression

**HyperDSP Multiband** provides independent low/mid/high compression with adjustable crossovers, per-band threshold, ratio and makeup, common attack, release and knee, plus wet/dry and input/output trim. The same engine is available as a graph node; its dynamics parameters can be bound to reactive macros. The crossover is a phase-matched fourth-order Linkwitz–Riley network, with no fixed sample-buffer latency. The internal dry blend uses the recombined reference to avoid phase cancellation. It does not remove the crossover's frequency-dependent phase shift; use bypass for untouched audio. Crossovers are fixed during graph modulation and changing them in standalone properties resets state. See [multiband design and tests](docs/multiband.md).

## Native color and digital degradation

Add **HyperDSP Color** for tanh saturation, cubic soft clipping, hard clipping, triangle wavefolding, bitcrushing/sample hold or unprocessed bypass. Drive spans −24 to +36 dB; the signed quantizer provides 2–16 bits and sample hold repeats each quantized value for 1–64 samples. No dithering is applied. Each channel has independent state.

Nonlinear modes offer optional 2× or 4× filtered oversampling using windowed-sinc FIR interpolation and decimation. Both modes delay the signal by 24 base-rate samples; the parallel dry path is aligned internally. This is not OBS-wide latency compensation. Bitcrushing intentionally preserves aliasing and does not oversample. Changing effect type or oversampling resets state rather than crossfading.

Four shared modulation routes control drive, bit depth, sample hold, wet/dry and output amplitude in destination units, including optional Sight and Sound inputs. As with other native effects, the underlying engine is independent of OBS and can be reused in future effect graphs. See [color verification](docs/color.md).

## Native time effects

Add **HyperDSP Time** for delay/echo, stereo ping-pong, two-tap chorus, flanger, vibrato or unprocessed bypass. Echo supports up to two seconds of fractional delay per channel, signed feedback (−0.95 to +0.95) and one-pole feedback damping. Ping-pong crosses adjacent channel pairs, with ordinary delay for unpaired channels. Chorus averages two phase-offset moving taps without feedback; flanger uses a short moving tap with signed feedback; vibrato uses a moving tap without feedback (set wet/dry to 100% for pure vibrato). Chorus/vibrato center and excursion are bounded to 40 ms, flanger to 10 ms. Stereo phase spread offsets odd-channel oscillators by up to half a cycle. The mode change picker substitutes an appropriate short-delay center when the existing delay exceeds that mode's range.

Four shared modulation routes target delay, feedback, depth, rate, wet/dry and output amplitude, including optional Sound and Sight. Continuous settings ramp over 10 ms; changing mode clears delay history and oscillator phase. Moving or abruptly retargeting a delay can produce pitch shifts; there is no tempo synchronization or crossfaded delay retiming yet. The reusable fractional-delay primitive allocates storage for the actual configured channel count and sample rate on reset, not inside audio processing. See [time effects verification](docs/time-effects.md).

## Native spatial effects

Add **HyperDSP Space** for a swept 2–12-stage all-pass phaser or stereo algorithmic reverb. The phaser has adjustable center, octave sweep depth, rate, signed feedback and stereo phase spread. Use a parallel wet/dry blend for the characteristic notches. Reverb uses independent input diffusion and a normalized four-line Hadamard feedback delay network for each channel pair, with pre-delay, nominal decay (0.1–10 s), damping, room size and stereo width. The nominal decay is a per-line attenuation target before damping, not a calibrated acoustic measurement. Width zero gives a common wet signal for each pair; mono uses the common projection.

Both modes support the four shared reactive routes, input/output gain and wet/dry mixing. Modulation destinations include phaser center/depth/rate/feedback and reverb decay/damping/pre-delay/size, plus width, mix and output amplitude. Only destinations applicable to the selected mode affect processing. Changing mode or stage count clears state; room-size changes retime integer taps and can produce audible discontinuities. Continuous base controls ramp over 10 ms. No convolution, measured room presets, tail-preserving bypass or live CPU qualification yet. See [space effects verification](docs/space-effects.md).

## Native spectral effects

Add **HyperDSP Spectral** for frequency-bin gating, phase-advancing spectral freeze, temporal spectral smearing, pitch shifting and frequency shifting. The shared FFT engine uses 1024-sample periodic Hann analysis/synthesis windows with 75% overlap and normalized overlap-add. Active modes have 1024 samples of latency (about 21.33 ms at 48 kHz), including the internally aligned dry branch. Unprocessed bypass is undelayed; OBS-wide compensation is not implemented.

The gate attenuates bins below a peak-amplitude threshold calibrated for bin-centered sine waves, with adjustable attenuation and opening/closing time constants. It is not a learned noise suppressor or adaptive noise profile. Freeze captures the next analysis frame when hold crosses 0.5, preserving magnitudes and measured inter-frame phase advances until release. Smear averages spectral magnitudes over time, using live phase while a bin is significant and continuing its last estimated phase advance when quiet. It creates spectral trails, not an ordinary echo. Stereo channels have independent spectral state.

Pitch shift (±24 semitones) multiplies partial frequencies; frequency shift (±5000 Hz) adds a fixed offset. Both use peak-region phase locking and estimated inter-frame frequency rather than simple FFT-bin relabeling. Output duration and analysis hop remain unchanged, and components whose shifted peak is outside the positive-frequency range are discarded. At zero shift the original spectrum is reconstructed. Nonzero shifts can change partial amplitudes and introduce artifacts on transients and complex signals; no formant preservation or adaptive analysis resolution yet.

Four reactive routes target threshold, attenuation, freeze hold, spectral memory time, mix, output amplitude, pitch and frequency shift. Spectral parameters are sampled at analysis hops; the shared mixer runs per sample. Optional Sound and Sight inputs remain supported. Mode changes reset spectral history; no crossfade or tail preservation. See [spectral verification](docs/spectral-effects.md).

## Native formants and resonators

**HyperDSP Formant** provides vowel morphing between five illustrative A/E/I/O/U resonance profiles or a custom three-band resonator bank. Each custom band has an independent center frequency (40–16000 Hz), nominal bandwidth (20–4000 Hz) and gain (−36 to +6 dB). The vowel morph interpolates centers and bandwidths logarithmically and gains in dB. Resonance shift (±24 semitones) scales the entire bank without transposing input frequencies; bandwidth scale (0.25–4×) changes resonance sharpness. Actual center/Q are bounded at the current sample rate.

The bank uses three parallel normalized TPT band-pass sections, with independent channel state and shared coefficients, and adds no fixed sample-buffer delay. The wet output is a weighted sum divided by three. Four reactive routes can drive morph, resonance shift, width, each band's frequency offset and gain, wet/dry and output amplitude. Optional Sight/Sound signals use the existing adapter. Continuous controls ramp over 10 ms; switching between bypass, vowel and custom modes resets filter state. This is creative resonance shaping, not a measured vocal-tract model or automatic formant-preserving pitch correction. See [formant verification](docs/formant-effects.md).

## LADSPA filter

Add **HyperDSP LADSPA** to an audio source. Search the effect catalog using any combination of name, label, maker, category, library filename, or mono/stereo (word order does not matter), narrow by category, and choose an effect from the native OBS properties. Switching catalog effects clears the previous effect's port values so the new effect starts at its own defaults. Manual trusted library path and exact label (for example, `/usr/lib/ladspa/amp.so` and `amp_stereo`) remain available. The filter generates numeric and toggle controls from port metadata. Mono effects use independent instances per OBS audio channel; stereo effects require a stereo OBS layout. Only equal-input/output mono or stereo effects declaring hard-real-time capability are accepted. Unsupported configurations, missing libraries and invalid labels bypass audio.

Use `analyseplugin /path/to/plugin.so` to see available labels. Only load trusted plugins: this backend executes native code in the OBS process, not a sandbox. The initial host does not provide preset import/export, latency compensation, sidechains, control-output meters or custom plugin interfaces. Categories are inferred from descriptor names/labels, not authoritative plugin metadata.

### Mix and gain staging

Each LADSPA filter also exposes input gain and output gain (−24 to +24 dB) and a linear wet/dry blend (0–1, default 1). Input gain precedes both the dry branch and the plugin, and output gain follows the blend. With a valid effect, zero wet still applies both trims; an invalid or unavailable effect instead passes the original audio unchanged. Nonfinite processed samples fall back to dry audio. The mixer is backend-independent code shared by LADSPA and the native filter. Input/output gain and wet/dry changes use a 10 ms linear sample ramp. Native LADSPA port changes remain block-boundary updates. Latency compensation and parallel alignment for delayed effects remain future work.

### Modulation

The LADSPA filter includes a sine LFO (0.05–20 Hz) with adjustable depth and destination: wet/dry or output amplitude (tremolo). Wet/dry modulation adds the bipolar LFO to the base value and clamps to 0–1; output amplitude is multiplied by 1 + depth × sine. Destination/depth changes ramp over 10 ms, and all channels share the same sample-clocked phase. Disabling modulation leaves the unmodulated effect settings intact. The phase freezes when both modulation depths are zero and resets on a new effect instance. This dedicated LFO is separate from the reactive routes, which can also consume Sight and Sound signals.

### Reactive parameter routing

The LADSPA filter additionally provides four independently configurable reactive routes, each with a source (sine, triangle, square or saw LFO, linked input envelope, or bass/mid/treble energy) and a destination: wet/dry, output amplitude, or an eligible continuous input control port of the selected effect. The signed amount is in the parameter's own units (up to ±10000), clamped to the host range; mix/output use ±1. Band sensitivity is ±24 dB. Detection is pre-trim. Native plugin ports are updated using the mean modulation value over sub-blocks of at most 64 samples; mixer destinations are sample-clocked. All channels receive the same modulation. Existing LFO settings remain independent and retain their previous behavior.

Port bindings include the library path, effect label and port index, so changing effects does not silently redirect a saved route. Incompatible destinations are inactive and shown as unavailable when inspecting a valid replacement effect. Integer/toggle controls are excluded. An amount edit on the same route ramps over 10 ms; disabling or replacing the route restores the base value immediately. Routes targeting the same destination are summed before clamping. Output amplitude contributions are summed before multiplying the base output gain; wet/dry contributions are additive. Each route has independent oscillator and detector state. The first route retains its original saved settings keys, and new routes default to off. Native filters and LADSPA now share the same fixed-capacity four-slot modulation matrix (source state, signed amount ramps, sparse destination contributions). Backend-specific bounds and LADSPA sub-block averaging remain in their respective adapters. Third-party effects may introduce their own parameter smoothing or latency.

### Optional Sound classification input

Both native filters and LADSPA routes can select **Sound category confidence** as a modulation source. Choose the attached Sound classifier by its parent/filter UUID (shown with its display name), enter the exact category label, choose a destination and set signed depth/amount. A negative depth can attenuate or close an effect when the selected category becomes active. Sound must already be producing category scores; HyperDSP neither starts inference nor loads a model. Without Sound, an unavailable category, a disabled/detached source or a stale snapshot, the external contribution returns to zero and the underlying effect stays active at its base settings.

The adapter reads the existing `sound_signal_read_v1` category stream via libobs on a single shared external-signal background polling thread (nominally every 100 ms). The audio path consumes a timestamped atomic score snapshot and ramps changes over 10 ms. Snapshots older than 500 ms on the consumer side are discarded, in addition to Sound's own availability/expiry checks. Polling is serial; a stalled producer can delay other subscribers, whose snapshots then also expire. No required Sound dependency, shared C++ ABI, model execution or producer call from the audio callback. The category entry is currently exact-label text, not a category browser. See [interop verification](docs/sound-interop.md).

### Optional Sight visual signals

Both backends accept Sight motion energy, hand X, hand Y and pinch closeness through the same four modulation routes. Motion is selected by the attached U2Net producer's parent/filter UUID and reads the source-local normalized luma-change energy, not physical velocity. Hand routes select a named hand publisher from Sight's tracking inventory. X/Y are the first reported hand's wrist coordinates in the producer's normalized video space; pinch is the producer's existing 0=open, 1=pinched signal. A first reported hand is not a persistent handedness identity. Motion and tracking can coexist with Sound category confidence and conventional audio modulation.

The adapter validates procedure version, buffer size, sequence, dimensions, kind, bounds and availability as appropriate to each contract. Duplicate, disabled, missing or unavailable tracking channels contribute zero. Sight snapshots are requested with a 500 ms maximum age; the shared worker also expires local snapshots after 500 ms and the matrix smooths changes over 10 ms. Named tracking channels follow Sight's own naming semantics (not UUIDs). No producer is rendered or started by HyperDSP. See [Sight interop verification](docs/sight-interop.md).

### Unified effect inventory

The shared native mode registry drives all twelve native OBS filter selectors and lists 43 processing modes (excluding bypass). The `hyperdsp-effects` tool combines this registry with the local scanned LADSPA catalog, without opening any plugin or starting OBS:

```sh
build/hyperdsp-effects --search "pitch"
build/hyperdsp-effects --format ladspa --channels 1 --json
```

Search accepts multiple words in any order, and the versioned JSON inventory includes native source/mode keys or local LADSPA path/label, category, description, format, maker and layout/path eligibility. A missing external catalog still leaves native effects discoverable. The tool is installed under `tools/`. It reports metadata only: eligibility is not audio qualification, and a local library path is not a portable effect identifier. The Effect Builder dock uses the same unified graphical library. The graph runtime supports native modes, gain and scanned LADSPA effects. JSON includes graph support flags, parameter metadata, and external latency/metadata completeness flags. Use --rate to resolve sample-rate-dependent port hints (default 48000). Rescan older catalogs to populate port metadata. See [effect library contract](docs/effect-library.md).

### Find and scan external effects

In **HyperDSP LADSPA** properties, choose **Scan installed effects**, then **Reload catalog** when the scan finishes. Search and category filters are retained; scanning does not change the active effect. A separate Python 3 scanner runs the native probe in a child process for each library, with a three-second timeout per probe. The scan is explicit because native libraries execute code during discovery and again when selected. This is process isolation, not a security sandbox.

For more effects, the properties include package suggestions and links to external collections. On Mint/Ubuntu/Debian, search your normal package manager for these collections:

| Collection | Package |
|---|---|
| [Linux Studio Plugins](https://lsp-plug.in/) | `lsp-plugins-ladspa` |
| Steve Harris plugins | `swh-plugins` |
| C* Audio Plugin Suite | `caps` |
| [Tom's Audio Processing](https://tap-plugins.sourceforge.io/) | `tap-plugins` |
| Invada Studio | `invada-studio-plugins-ladspa` |
| [DISTRHO](https://github.com/DISTRHO/DPF-Plugins) | `dpf-plugins-ladspa` |

Install the desired packages yourself and scan/reload. Not every plugin in a package qualifies for this host; the catalog lists eligible LADSPA descriptors, not CLAP/LV2/VST variants or guarantees of audio quality. Availability varies by distribution. No package install or download is performed by HyperDSP.

For custom installs, set the platform-separated `LADSPA_PATH` (colon on POSIX, semicolon on Windows) before launching OBS, or use the CLI below with explicit trusted paths. When LADSPA_PATH is set, it replaces default search locations. Otherwise the scanner checks platform-specific user and system LADSPA directories (see [discovery paths](docs/external-effects.md)). Symlinked libraries are deduplicated by resolved path.

```sh
python3 tools/scan-ladspa.py --probe build/hyperdsp-ladspa-probe
```

The scanner atomically writes `ladspa-catalog.json` in the OBS plugin configuration directory; the CLI follows the platform defaults described in [external effect discovery](docs/external-effects.md). The installed script and probe are under the plugin's tools directory. The build tree copies the script next to the module for development. Python 3 is needed for discovery, not for audio processing. See [external effect discovery](docs/external-effects.md).

## Migration status

HyperDSP registers its own standalone microphone source ID `hyperdsp_mic_filter`, retaining the settings keys and existing graph effect IDs. Sound can keep its historical `mic_modeler_filter` while existing scenes remain unchanged. This avoids a registration collision irrespective of load order.

Run `tests/verify-coexist.sh /path/to/live-latent-lab-sound-public` to build a committed Sound snapshot with only YAMNet enabled, then verify both module load orders and real stereo processing with the original filter ID. This tests coexistence, not the entire expanded neural stack.

Removing the old Mic DSP implementation from Sound still requires coordinated scene migration and Sound verification. Do not replace running modules.

## Direction

A standalone OBS DSP host with programmable effects (JSFX/Faust candidates) and native audio plugin hosting (LADSPA implemented; CLAP/LV2 candidates). JSFX integration needs a license compatibility review before linking YSFX (Apache-2.0) to the inherited GPL-2.0 code. Faust, CLAP and LV2 are not implemented. See [the development roadmap](docs/roadmap.md) for the agreed broader direction and current implementation boundary.

## Provenance

Mic filter and DSP helpers extracted from myrqyry/live-latent-lab.sound at f2e18dc, including the original verification tests. GPL-2.0 license retained.
