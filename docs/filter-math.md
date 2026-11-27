# Native filter alignment verification

The resonant cascade retains its original identical-Q stages and saved-settings default. The optional Butterworth alignment uses N=2m poles for m second-order stages, with ascending Q_k=1/(2 cos(pi*(k+1/2)/N)), k=0,...,m-1. TPT SVFs use tan(pi*fc/fs) prewarping. Stage Q values are precomputed when topology changes; coefficient updates remain shared across channels. Fixed alignment intentionally ignores Q modulation.

Independent Wolfram Language evaluation (2026-11-24):

```wl
N[Table[{n, Table[1/(2 Cos[Pi (k-1/2)/n]),{k,1,n/2}],
 -10 Log10[2],
 -10 Log10[1+(Tan[Pi 8000/48000]/Tan[Pi 1000/48000])^(2 n)]},
 {n,{2,4,6,8}}],12]
```

| Poles | Stage Q (rounded) | Cutoff dB | 8 kHz dB, fc=1 kHz, fs=48 kHz |
|---|---|---:|---:|
| 2 | 0.707107 | -3.0103 | -37.7971 |
| 4 | 0.541196, 1.306563 | -3.0103 | -75.5928 |
| 6 | 0.517638, 0.707107, 1.931852 | -3.0103 | -113.3892 |
| 8 | 0.509796, 0.601345, 0.899976, 2.562915 | -3.0103 | -151.1856 |

The C++ numerical regression tests measure actual samples against these independent reference values and test high-pass cutoff, passband, fixed-Q invariance and the existing resonant response. Real OBS stereo capture confirms switching between the 8-pole alignments. At cutoff, resonant Q=1/sqrt(2) gives 0.25 amplitude ratio, while Butterworth gives 1/sqrt(2). These checks are not exhaustive CPU, stability, or UI qualification.

Reference: https://reference.wolfram.com/language/ref/ButterworthFilterModel.html

The standalone adapter prepares legacy-key external modulation subscriptions on the control path and adopts settings at block boundaries without a processing mutex. Continuous settings preserve filter history. Explicit topology changes reset state; comb history uses a constant-time logical reset instead of clearing the 16,384-sample ring per channel.
