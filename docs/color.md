# Native color and degradation

The reusable Color engine implements tanh, bounded cubic soft clipping, hard clipping and triangle folding, plus signed uniform bit quantization and integer sample hold. Drive is applied to the wet branch. Quantization is signed two's-complement-like with 2^bits possible levels, including -1 and excluding +1; no dither. Sample hold is intentional non-bandlimited degradation.

Oversampling is optional and uses one polyphase interpolation FIR and one decimation FIR, with Blackman-windowed sinc coefficients, cutoff 0.45 cycles/base-rate sample, 12 base-sample group delay each and normalized DC gain. At factors 2 and 4, the combined group delay is 24 base-rate samples (0.5 ms at 48 kHz); the dry branch is delayed equally. The 1× and crusher modes have zero added algorithmic delay. No scene-wide latency reporting/compensation or topology crossfade. Audio callback uses fixed-size storage; no per-block allocation. The host prepares format-dependent engine storage and subscriptions on the control path, and adopts latest settings at audio block boundaries without a processing mutex. Same-format continuous edits retain FIR history and dry alignment. Explicit topology changes still reset the filter. Oversampling is not alias-free and has not been CPU-profiled in live OBS.

Measured 18 kHz aliased component of a 10 kHz sine, hard clip, drive +18 dB, 48 kHz base rate:

| Factor | Alias amplitude |
|---|---:|
| 1× | 0.402369 |
| 2× | 0.00730733 |
| 4× | 0.0123309 |

This is one spectral bin under one specific stimulus, not broadband alias energy or a universal ranking of factors. Tests also check wet-path 24-sample delay and low-frequency gain, dry alignment, block partition independence, channel isolation at 44.1/48/96 kHz, exact signed 2-bit levels, sample hold, static transfer curves, finite output, bypass and external modulation. Real libobs capture checks hard clipping, 4× mode, dry alignment by RMS, crusher behavior, bypass and Sound category drive using its contract fixture. No live model or GUI smoke test.

All four modulation routes are available via the reusable OBS route helper. Destination amounts are signed, summed, bounded and interpreted in parameter units; bit depth and hold are quantized. Input/output trim and wet/dry gain retain existing semantics.
