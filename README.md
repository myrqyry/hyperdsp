# HyperDSP

Conventional audio processing for OBS Studio, separate from the neural audio sources and filters in Live Latent Lab.Sound.

The first verified target is the microphone coloration filter: dynamic, condenser, ribbon, telephone, vintage radio, tape and dry bypass, with spectral sweep and resonance controls. These are procedural tones, not measured microphone reproductions.

## Build and test (Linux)

Requires a C++20 compiler, CMake, OBS development headers and libobs. Run `tests/verify.sh` for the DSP unit test and real OBS post-filter stereo capture test. No model, download, audio device or neural dependency is needed.

## Migration status

This development module retains the existing OBS source ID `mic_modeler_filter` and settings keys to preserve scene compatibility. **Do not install or load it alongside a Sound build registering the same ID.** The old module remains unchanged until a coordinated migration verifies both modules together. No changes to installed OBS plugins or scene collections are required for testing.

Run `tests/verify-coexist.sh /path/to/live-latent-lab-sound-public` to build a committed Sound snapshot with only YAMNet enabled, then verify both module load orders and real stereo processing with the original filter ID. This tests coexistence, not the entire expanded neural stack.

The production migration must remove Mic DSP from Sound's enabled effect sets, registration, verification receipts and installer checks together, requalify the expanded build, and install both modules while OBS is closed. Do not replace a running module or publish a second registration.

## Direction

A standalone OBS DSP host with programmable effects (JSFX/Faust candidates) and native audio plugin hosting (CLAP/LV2 candidates). These are future capabilities, not currently implemented.

## Provenance

Mic filter and DSP helpers extracted from myrqyry/live-latent-lab.sound at f2e18dc, including the original verification tests. GPL-2.0 license retained.
