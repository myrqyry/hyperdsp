# Native formants and resonator bank

The reusable Formant engine in src/framework/dsp/formant.hpp uses three parallel, center-normalized topology-preserving state-variable band-pass sections (existing SvfCoefficients/SvfState). Each channel has its own state. The wet output is sum(gain_j * band_j)/3, where gain_j = 10^(dB_j/20). The nominal Q is center/bandwidth, bounded to [0.25,80] after frequency is clamped to [40,0.45*sample_rate]. At high frequency the bilinear transform means the requested analog bandwidth is not an exact digital -3 dB width. There is no fixed buffered latency, but filters have frequency-dependent phase/group delay. No heap allocation or host call inside DSP processing. Static coefficient calculations are cached; continuous modulation may require per-sample coefficient design.

Vowel presets are deliberately illustrative synthesized targets, not a measured speaker corpus or a universal mapping of vowel pronunciation.

| Target | Center frequencies (Hz) | Nominal bandwidths (Hz) |
|---|---|---|
| A | 800,1150,2900 | 90,120,180 |
| E | 500,1900,2600 | 80,130,180 |
| I | 270,2290,3010 | 60,120,180 |
| O | 450,800,2830 | 80,100,180 |
| U | 300,870,2240 | 60,100,160 |

Preset gains are 0,-6,-12 dB. Interpolation is linear in log2 frequency/bandwidth and gain dB. Custom mode uses the same bank with independent controls; the morph parameter has no effect there. Global semitone shift multiplies both frequency and bandwidth by 2^(shift/12); width multiplies bandwidth. Resonance offsets (Hz) and gain (dB) can be modulated individually in either mode. Four shared modulation routes use signed amounts and bounded sums. The mode switch resets integrator state; continuous edits, including selecting vowel endpoints, ramp over 10 ms. Filtered output reshapes the input spectrum; it neither detects formants nor independently transposes input frequencies, and it does not perform spectral-envelope preservation during pitch shifting.

Core verification uses coherent sine response measurements at 44.1/48/96 kHz. A 500 Hz custom peak with other gains -36 dB measures approximately 0.333337 amplitude ratio, with 100 Hz rejection 0.0139036 at 48 kHz; +12 semitone resonance shift moves the peak to 1000 Hz. At 600 Hz, width 1 vs 4 gives 0.15932 vs 0.302697; reducing the peak gain by 6 dB gives 0.167066. A-to-I endpoints and geometric midpoint are verified, as are individual offset and gain routes, stereo proportionality and isolation, block partition invariance, finite extreme/invalid input handling and bypass. Actual libobs capture verifies custom peak/rejection/shift, vowel endpoints and manual midpoint, matching Sound confidence fixture values 0.5 and 1, fallback, stereo proportionality and bypass. At 270 Hz input the A, midpoint and I output RMS values are 0.00539058, 0.0136643 and 0.0942843 respectively. These are controlled measurements, not a perceptual vowel intelligibility or live model qualification. Installed GUI, listening quality and live CPU budget remain unverified.
