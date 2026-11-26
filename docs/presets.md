# Bundled graph presets

Load from the Effect Builder's Presets tab, or import the plain graph JSON in examples/graphs. These are editable native effect chains, not external plugins. They use exposed normalized macros, including coordinated multi-parameter mappings. Audition first, adjust for the source, then select a target and Apply. The eight new presets end with a full-wet sample-peak limiter at -2 or -3 dBFS; this is not a true-peak limiter or a loudness normalization guarantee.

| Preset | Processing and intended use | Exposed controls |
| --- | --- | --- |
| Voice Foundation | High-pass, downward expander, presence EQ, compressor, limiter; starting point for speech, not ML noise removal | Room cleanup, Presence, Leveling |
| Parallel Punch | Compressed/saturated branch mixed with dry audio; latency-aligned graph summation and limiter | Punch, Output trim |
| Radio Transmission | Variable band limits, midrange presence, soft clipping and limiter | Bandwidth, Grit |
| Tape Echo | Saturation, high-shelf roll-off, ping-pong delay and limiter | Tape age, Echo, Delay time |
| Envelope Wah | Input envelope opens resonant band-pass, with light saturation and limiter | Wah position, Resonance |
| Rhythmic Pulse | 2 Hz triangle LFO shapes gain and high-shelf attenuation, followed by small room and limiter; free-running, not beat-synchronized | Pulse position, Room |
| Spectral Dream | Spectral magnitude memory and reverb mixed with latency-aligned dry branch, treble energy modulates space | Spectral memory, Space |
| Stereo Chorus | Two-tap chorus with adjustable phase spread, high-shelf roll-off and limiter; mono also works | Chorus width, Movement, Warmth |
| Parallel Color | Existing parallel saturation example | Color |
| Vowel Space | Existing vowel morph and reverb example | Vowel, Space |

Voice Foundation's expander requires appropriately set thresholds and release time; sustained background sound may not be attenuated. Spectral Dream is an artistic effect with 1024 samples of known algorithmic latency, not a voice-restoration tool. Tape Echo's time changes can move pitch during adjustment. The three reactive presets use local envelope, LFO or treble-band analysis, with no dependency on Sight/Sound or LADSPA.

## Verification

The graph-presets test enumerates every bundled JSON, checks the expected inventory, parses and prepares the production graph, renders deterministic pink noise through the isolated audition path, exercises default and all-min/all-max macro settings at 44.1, 48 and 96 kHz in mono and stereo, checks finite non-silent output and the new chains' sample-peak ceilings, and verifies reactive values move. Independent fresh renders of each macro endpoint at 48 kHz stereo verify an actual output difference. The production OBS graph apply transaction, generated float properties, and monitor readback from synthetic audio are checked for every preset. The widget test loads Envelope Wah from the bundled list, checks that the applied filter is untouched, and restores the previous draft. Synthetic qualification is not a listening or real-world CPU benchmark. These are starting points, not calibrated presets for a particular microphone or room.
