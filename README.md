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

## LADSPA filter

Add **HyperDSP LADSPA** to an audio source. Search the effect catalog by name, label or maker, narrow by category, and choose an effect from the native OBS properties. Switching catalog effects clears the previous effect's port values so the new effect starts at its own defaults. Manual trusted library path and exact label (for example, `/usr/lib/ladspa/amp.so` and `amp_stereo`) remain available. The filter generates numeric and toggle controls from port metadata. Mono effects use independent instances per OBS audio channel; stereo effects require a stereo OBS layout. Only equal-input/output mono or stereo effects declaring hard-real-time capability are accepted. Unsupported configurations, missing libraries and invalid labels bypass audio.

Use `analyseplugin /path/to/plugin.so` to see available labels. Only load trusted plugins: this backend executes native code in the OBS process, not a sandbox. The initial host does not provide preset import/export, latency compensation, sidechains, control-output meters or custom plugin interfaces. Categories are inferred from descriptor names/labels, not authoritative plugin metadata.

### Mix and gain staging

Each LADSPA filter also exposes input gain and output gain (−24 to +24 dB) and a linear wet/dry blend (0–1, default 1). Input gain precedes both the dry branch and the plugin, and output gain follows the blend. With a valid effect, zero wet still applies both trims; an invalid or unavailable effect instead passes the original audio unchanged. Nonfinite processed samples fall back to dry audio. The mixer is backend-independent code; only LADSPA uses it so far. Live settings changes are block-boundary updates; sample-ramped controls, latency compensation and parallel alignment for delayed effects remain future work.

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

A standalone OBS DSP host with programmable effects (JSFX/Faust candidates) and native audio plugin hosting (LADSPA implemented; CLAP/LV2 candidates). JSFX integration needs a license compatibility review before linking YSFX (Apache-2.0) to the inherited GPL-2.0 code. Faust, CLAP and LV2 are not implemented.

## Provenance

Mic filter and DSP helpers extracted from myrqyry/live-latent-lab.sound at f2e18dc, including the original verification tests. GPL-2.0 license retained.
