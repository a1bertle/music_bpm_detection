# Implementation Notes

A comparison of the final implementation against the researched algorithms and theory documented in [ALGORITHMS.md](ALGORITHMS.md).

---

## Onset Detection

### Theory

The research identified several onset detection approaches, ordered by increasing sophistication:

1. Energy-based (simple but fails on soft onsets).
2. Basic spectral flux (magnitude differences between STFT frames).
3. Mel-frequency spectral flux (perceptually weighted, more robust).
4. Complex-domain spectral difference (uses phase for detecting pitched/soft onsets).
5. Multi-feature consensus (Essentia runs 5 detectors simultaneously).

The mel-frequency spectral flux was recommended as the best balance of accuracy and implementation complexity. The complex-domain variant was noted as a potential enhancement for classical music.

### Implementation

The `OnsetDetector` (`onset_detector.cpp`) implements mel-frequency spectral flux, matching the recommended approach:

| Aspect | Theory | Implementation | Match |
|--------|--------|----------------|-------|
| Window function | Hann, 2048 samples | Hann, `fft_size_=2048` | Exact |
| Hop size | 512 samples (~86 fps at 44.1 kHz) | `hop_size_=512` | Exact |
| FFT | Real-to-complex | pocketfft `rfft_forward()`, double precision | Exact |
| Power spectrum | \|X[k]\|^2 | `re*re + im*im` per bin | Exact |
| Mel filterbank | 40 triangular filters, 30--8000 Hz | 40 bands, `hz_to_mel`/`mel_to_hz` with standard formula `2595*log10(1+f/700)` | Exact |
| Log compression | `log10(energy + epsilon)` | `log10(sum + 1e-10)` | Exact |
| Spectral flux | Half-wave rectified difference, sum across bands | `max(0, mel[t][b] - mel[t-1][b])`, summed | Exact |
| Normalization | Zero mean, unit variance | Mean subtraction, stddev division | Exact |

**What's not implemented:**

- **Complex-domain variant.** The implementation uses magnitude-only spectral flux. Phase information is discarded after FFT. This is a known limitation for classical music with soft/pitched onsets where magnitude barely changes but phase shifts.
- **Superflux.** An enhancement (Boeck & Widmer, 2013) that compares each bin against a local maximum in a neighborhood of previous frames, reducing false onsets from vibrato. Not implemented.
- **Adaptive thresholding.** The research mentions subtracting a local average before half-wave rectification for noise robustness. The implementation normalizes globally (zero mean, unit variance) instead. Global normalization is simpler but less adaptive to varying onset density across a track.

---

## Tempo Estimation

### Theory

The research covered four tempo estimation approaches:

1. Autocorrelation (peaks at periodic lags reveal tempo).
2. Comb filter / resonator bank (Scheirer 1998).
3. Fourier tempogram (frequency-domain periodicity analysis).
4. Deep learning (TCN, LSTM).

Autocorrelation was recommended, with three critical requirements: a tempo prior to bias toward common tempos, octave error correction to disambiguate harmonically related tempos, and sufficient BPM resolution.

### Implementation

The `TempoEstimator` (`tempo_estimator.cpp`) implements autocorrelation with prior and octave correction:

| Aspect | Theory | Implementation | Match |
|--------|--------|----------------|-------|
| Autocorrelation | Direct computation over onset function | Direct sum, normalized by overlap count | Enhanced (normalization prevents bias toward longer lags) |
| BPM range | Configurable, default 50--220 | Lag range derived from `min_bpm`/`max_bpm`, default 50--220 | Exact |
| Tempo prior | Log-Gaussian centered at 120 BPM, sigma in octaves | `log2(bpm/120)`, sigma=1.0 octave, Gaussian weighting | Exact (after bug fix; original used `ln` with sigma=0.5) |
| Octave correction | Check half-lag and double-lag candidates | Iterative halving with windowed search (+/-2 lags) and median noise floor threshold | Enhanced (handles non-integer lag alignment, multiple octave jumps) |
| BPM resolution | ~2.7 BPM per lag step at 44.1kHz/512 hop | Parabolic interpolation around peak for sub-lag precision | Enhanced |

**What's not implemented:**

- **FFT-based autocorrelation.** The research notes autocorrelation can be computed in O(N log N) via FFT (forward FFT, multiply by conjugate, inverse FFT). The implementation uses direct computation at O(N * L) where L is the lag range. For typical onset vectors (~8000 frames) and lag ranges (~80 lags), this is fast enough, but FFT-based computation would be more efficient for longer tracks or wider BPM ranges.
- **Tempogram.** The research describes computing autocorrelation in a sliding window across time to produce a 2D tempogram for tracking local tempo changes. The implementation computes a single global autocorrelation. This is the primary limitation for classical music with rubato.
- **Multiple tempo candidates.** The implementation returns a single BPM. Libraries like Essentia return multiple candidates with confidence scores. A multi-candidate approach would allow the beat tracker to try several tempos and pick the best.

---

## Beat Tracking

### Theory

The research identified the Ellis 2007 dynamic programming approach as the recommended method, noting:

- Objective: maximize onset alignment + inter-beat regularity.
- Penalty: `alpha * (log(delta/tau))^2` for tempo deviations.
- Search window: `[t - 2*tau, t - tau/2]` around each frame.
- Backtrace from highest-scoring frame near the end.
- Default alpha around 680 (from Ellis).

### Implementation

The `BeatTracker` (`beat_tracker.cpp`) implements Ellis 2007 DP:

| Aspect | Theory | Implementation | Match |
|--------|--------|----------------|-------|
| DP formulation | Score = onset + best_predecessor - penalty | `dp[p] + onset[t] - alpha * (log(lag/period))^2` | Exact |
| Search window | `[t - 2*tau, t - tau/2]` | `start = t - max_lag` (2*period), `end = t - min_lag` (0.5*period) | Exact |
| Penalty function | `alpha * (log(delta/tau))^2` | `alpha * (log_ratio * log_ratio)` using `std::log` (natural log) | Exact (Ellis uses natural log for the penalty) |
| Default alpha | ~680 | 680.0f | Exact |
| Backtrace start | Last ~10% of signal | `total_frames * 0.9f` to end | Exact |
| Output | Beat frame indices -> sample positions | `frame * hop_size` | Exact |

**What's not implemented:**

- **Multiple tempo hypotheses.** Ellis suggests running the DP with several candidate tempos and selecting the sequence with the highest total score. The implementation uses only the single best tempo from the estimator.
- **Local tempo adaptation.** The DP assumes a fixed period across the entire track. An extension would allow the period to vary slowly, which would improve performance on tracks with gradual tempo changes.
- **Adjustable alpha per genre.** The research notes alpha could be reduced (e.g., 400) for classical music to allow more flexible beat placement. The default 680 is exposed as a parameter but there is no automatic genre-based adjustment.

---

## MP3 Decoding

### Theory

The research compared four C++ MP3 decoding libraries:

| Library | Recommendation |
|---------|---------------|
| minimp3 | Best overall: header-only, CC0, SIMD, float output |
| dr_mp3 | Alternative with nicer pull API, no LAME tags |
| libmpg123 | Fastest, LGPL, requires linking |
| FFmpeg | Overkill for MP3-only |

### Implementation

The `Mp3Decoder` (`mp3_decoder.cpp`) uses minimp3, matching the top recommendation:

- Uses the high-level `mp3dec_load()` API from `minimp3_ex.h` for whole-file decoding.
- `MINIMP3_FLOAT_OUTPUT` produces float samples directly (no int16-to-float conversion needed).
- Defines `MINIMP3_IMPLEMENTATION` and `MINIMP3_EX_IMPLEMENTATION` in a single translation unit, preventing multiple-definition errors.
- Copies decoded buffer into `std::vector<float>` and frees the minimp3 buffer.

This is a straightforward, correct integration.

---

## Metronome Overlay

### Theory

The plan specified:

- Click synthesis: exponentially-decaying sine burst, 1000 Hz, 20 ms, decay rate 200.
- Mix clicks into all channels at beat positions.
- Clamp to [-1, 1] to prevent clipping.

### Implementation

The `Metronome` (`metronome.cpp`) matches the specification exactly:

| Aspect | Theory | Implementation | Match |
|--------|--------|----------------|-------|
| Click waveform | `amp * sin(2*pi*freq*t) * exp(-decay*t)` | Same formula, default 1000 Hz, 0.02s, decay=200 | Exact |
| Channel handling | Add to all channels | Inner loop over `ch = 0..channels-1` | Exact |
| Clipping | Clamp to [-1, 1] | `std::max(-1.0f, std::min(1.0f, sample))` | Exact |
| Bounds checking | Don't write past buffer end | `if (frame >= frames) break` | Correct |

---

## WAV Output

### Theory

Standard 44-byte RIFF/WAVE header, 16-bit signed PCM, little-endian.

### Implementation

The `WavWriter` (`wav_writer.cpp`) writes the header field by field using `write_u16`/`write_u32` helpers. Float-to-int16 conversion uses `clamp(s, -1, 1) * 32767`. All fields match the PCM WAV specification. Correct and complete.

---

## Summary

### What aligns with the research

The implementation follows the recommended algorithm choices closely: mel-frequency spectral flux for onset detection, autocorrelation with tempo prior for tempo estimation, Ellis 2007 DP for beat tracking, minimp3 for decoding, and synthesized click overlay for metronome output. The core pipeline matches the theoretical descriptions in structure, parameters, and formulas.

### Where the implementation goes beyond

- **Autocorrelation normalization** by overlap count (not in the basic theory).
- **Iterative octave correction** with windowed half-lag search and median noise floor (more robust than the single-point check described in most references).
- **Parabolic interpolation** for sub-lag BPM resolution (not mentioned in Ellis 2007).

### Where the implementation falls short of the state of the art

| Gap | Impact | Potential improvement |
|-----|--------|---------------------|
| No complex-domain onset detection | Weaker detection of soft/pitched onsets in classical music | Add phase-based onset detection as an alternative mode |
| Single global tempo | Cannot track tempo changes (rubato, accelerando) | Implement windowed autocorrelation tempogram |
| Single tempo hypothesis for beat tracker | May lock onto wrong tempo if estimator errs | Run DP with top-N tempo candidates, pick highest-scoring sequence |
| No superflux | Vibrato can produce false onsets | Compare each bin against local max of previous frames |
| No deep learning | Lower accuracy ceiling than RNN/TCN approaches | Would require pre-trained models and significant complexity |
| WAV output only | Large files compared to MP3 | Integrate LAME for optional MP3 encoding |
