# BPM Detection Algorithms

Research notes on beat tracking and tempo estimation algorithms, with a focus on reliable detection across classical, pop, and rock music from MP3 files, targeting C++ implementation.

## Table of Contents

- [Pipeline Overview](#pipeline-overview)
- [Algorithm Families](#algorithm-families)
  - [Energy-Based Onset Detection](#1-energy-based-onset-detection)
  - [Spectral Flux / Spectral Difference](#2-spectral-flux--spectral-difference)
  - [Autocorrelation-Based Tempo Estimation](#3-autocorrelation-based-tempo-estimation)
  - [Comb Filter / Resonator Bank](#4-comb-filter--resonator-bank)
  - [Dynamic Programming Beat Tracking](#5-dynamic-programming-beat-tracking)
  - [Multi-Feature Consensus](#6-multi-feature-consensus)
  - [Deep Learning](#7-deep-learning)
- [Genre-Specific Challenges](#genre-specific-challenges)
- [Comparison](#comparison)
- [Open-Source Libraries](#open-source-libraries)
- [MP3 Decoding Libraries for C++](#mp3-decoding-libraries-for-c)
- [C++ Beat Detection Libraries](#c-beat-detection-libraries)
- [References](#references)

---

## Pipeline Overview

Nearly all BPM detection systems follow a three-stage pipeline:

1. **Front-end analysis** -- transform raw audio into a reduced representation (onset detection function / novelty function).
2. **Periodicity estimation** -- determine the dominant tempo (BPM) from the onset detection function.
3. **Beat location tracking** -- place individual beat positions in time, aligned with the estimated tempo and local onset peaks.

---

## Algorithm Families

### 1. Energy-Based Onset Detection

Computes short-time energy per audio frame (sum of squared samples) and flags spikes that exceed a running average by a threshold.

- **Complexity**: O(N) -- extremely cheap.
- **Strengths**: Simple, real-time capable, works well for music with strong percussive attacks (drums, electronic kicks).
- **Weaknesses**: Fails on music with soft onsets (legato strings, bowed instruments, voice). Does not distinguish frequency content -- a bass note and a cymbal hit look identical. Up to 82% worse than spectral methods on non-percussive music.
- **Genre fit**: Pop/Rock with prominent kick/snare (good). Classical, jazz with brushes, ambient (poor).

### 2. Spectral Flux / Spectral Difference

Computes the Short-Time Fourier Transform (STFT), then measures the half-wave rectified magnitude difference between consecutive frames. Only spectral *increases* (note onsets) contribute; decreases (note offsets) are discarded.

**Steps:**

1. Frame audio with a Hann window.
2. Compute real-to-complex FFT per frame.
3. Compute power spectrum: |X[k]|^2 = re^2 + im^2.
4. Apply a mel filterbank (triangular filters spaced on the mel scale). Mel conversion: `mel = 2595 * log10(1 + f/700)`.
5. Log compression: `log10(energy + epsilon)`.
6. Half-wave rectified difference: `flux[b] = max(0, log_mel[t][b] - log_mel[t-1][b])`.
7. Sum across all bands to produce one onset strength value per frame.
8. Normalize to zero mean, unit variance.

**Variants:**

- **Basic spectral flux**: magnitude differences only.
- **Complex domain**: uses both magnitude and phase; detects onsets where phase deviates from expected trajectory. Better for pitched/soft onsets.
- **Mel-frequency spectral flux**: flux computed on mel-scaled filterbanks. Perceptually motivated, more robust.
- **High Frequency Content (HFC)**: weights bins proportionally to frequency, emphasizing transients.

- **Complexity**: O(N * K) where K is the number of frequency bins.
- **Strengths**: Handles polyphonic content well. Complex-domain variant detects soft/pitched onsets.
- **Weaknesses**: Struggles with vibrato (continuous pitch modulation creates false onsets). Requires tuning of hop size, window size, frequency resolution.
- **Genre fit**: Pop/Rock (good). Classical with complex-domain variant (moderate).

### 3. Autocorrelation-Based Tempo Estimation

Computes the autocorrelation of the onset detection function. Peaks appear at lags where the signal repeats, revealing periodicities corresponding to candidate tempos.

**Steps:**

1. Convert BPM range to lag range in onset frames: `lag = frame_rate * 60 / bpm`.
2. For each candidate lag tau: `R(tau) = sum(onset[t] * onset[t + tau])`.
3. Apply a log-Gaussian tempo prior centered at a reference tempo (e.g., 120 BPM) to bias toward common tempos: `weight = exp(-0.5 * (log2(bpm/ref))^2 / sigma^2)`.
4. Select the lag with highest weighted autocorrelation.
5. Octave error correction: check half-lag and double-lag candidates, since a signal at period T also has strong autocorrelation at 2T, 3T, etc.

Can be computed in a sliding window across time to produce a **tempogram** (2D: lag vs. time) for tracking local tempo changes.

- **Complexity**: O(N * L) for direct computation, O(N * log N) via FFT.
- **Strengths**: Well-understood, handles multiple metrical levels, produces confidence measure (peak height).
- **Weaknesses**: **Octave error problem** -- 120 BPM signal also resonates at 60 and 240 BPM. Assumes local stationarity within analysis window.
- **Genre fit**: Pop/Rock (good). Classical with rubato (poor).

### 4. Comb Filter / Resonator Bank

Splits audio into frequency sub-bands, extracts amplitude envelopes, and feeds each into a bank of parallel comb filters tuned to different tempos. A comb filter with delay `d` sums energy at periodic intervals; when input periodicity matches `d`, the filter resonates.

**Steps (Scheirer 1998):**

1. Split audio into ~6 frequency sub-bands via bandpass filters.
2. Full-wave rectification + low-pass filtering to extract envelopes.
3. Feed each envelope into parallel comb filters at candidate tempos.
4. The filter with strongest output indicates the dominant tempo.
5. Phase-locking analysis determines exact beat positions.

- **Complexity**: O(N * B * T) where B = bands, T = tempo candidates. Each operation is trivially cheap.
- **Strengths**: Intuitive physical model, real-time capable, multi-band robustness.
- **Weaknesses**: Tempo resolution limited by discrete comb filter spacing. Octave problem persists.
- **Genre fit**: Pop/Rock/Dance (good). Classical (poor -- assumes steady tempo).

### 5. Dynamic Programming Beat Tracking

Finds the globally optimal sequence of beat positions by maximizing a score that balances onset alignment with inter-beat regularity.

**Objective function (Ellis 2007):**

```
Score = sum(onset_strength[beat_i]) + alpha * sum(F(beat_{i+1} - beat_i, tau))
```

where `F(delta, tau) = -(log(delta/tau))^2` penalizes deviations from expected period tau.

**Steps:**

1. Initialize `score[t] = onset_strength[t]` for all frames.
2. Forward pass: for each frame t, search predecessors in window `[t - 2*tau, t - tau/2]`:
   - `candidate = score[p] + onset_strength[t] - alpha * (log((t-p)/tau))^2`
   - Keep best predecessor.
3. Backtrace from highest-scoring frame near the end.
4. Convert beat frame indices to sample positions: `frame * hop_size`.

- **Complexity**: O(N * tau) per frame, O(N * W) total.
- **Strengths**: Globally optimal sequence, clean tightness parameter, handles syncopation.
- **Weaknesses**: Offline only (needs full signal). Assumes single global tempo. ~60% accuracy on MIREX 2006 as standalone.
- **Genre fit**: Pop/Rock (good). Classical (poor -- global tempo assumption).
- **Used by**: librosa `beat_track()` with default `tightness=100`.

### 6. Multi-Feature Consensus

Runs multiple onset detection functions simultaneously and selects beat candidates by agreement across methods.

**Essentia's BeatTrackerMultiFeature** uses 5 onset functions:

1. Complex spectral difference (2048/1024 frame/hop).
2. Energy flux (RMS-based).
3. Mel-frequency spectral flux.
4. Beat emphasis function (2048/512).
5. Modified information gain (2048/512).

`TempoTapDegara` generates beat candidates from each; `TempoTapMaxAgreement` selects by consensus. The `RhythmExtractor2013` wrapper outputs BPM, beat positions, and confidence (0--5.32 scale; >3.5 indicates ~80% accuracy).

- **Strengths**: Robust through redundancy.
- **Genre fit**: Broadly good across genres.

### 7. Deep Learning

Neural networks trained on annotated beat datasets produce per-frame beat activation functions, post-processed with Dynamic Bayesian Networks (DBN) or Hidden Markov Models for temporal regularity.

**Architectures:**

- **Bidirectional LSTM** (Boeck et al., 2016): multiple recurrent layers learn temporal beat patterns. Used in madmom's `RNNBeatProcessor`. Captures long-range dependencies but sequential (not parallelizable).

- **Temporal Convolutional Networks / TCN** (Davies & Boeck, 2019): 11 stacked residual blocks with exponentially increasing dilation (2^0 to 2^10). ~81.5 second receptive field. Fully parallelizable. Current state-of-the-art on benchmarks. Post-processed with DBN over 55--215 BPM range.

- **BeatNet CRNN** (Heydari et al., 2021): convolutional + recurrent layers with particle filtering for causal (real-time, <50ms latency) or DBN for offline inference. Jointly tracks beat, downbeat, tempo, and meter.

- **Strengths**: Highest accuracy across all genres including classical. Handles syncopation, polyrhythm, genre variation.
- **Weaknesses**: Requires large annotated training datasets. GPU-beneficial. Less interpretable. Performance degrades on underrepresented genres.

---

## Genre-Specific Challenges

### Classical

The hardest genre for all beat tracking algorithms:

- **Rubato**: performers freely speed up and slow down.
- **Soft onsets**: bowed strings, voice, and wind instruments lack sharp transients.
- **Complex meter**: frequent time signature changes.
- **No percussion**: the most reliable beat cue is absent.
- **Ritardando/accelerando**: gradual tempo changes within phrases.

No algorithm reliably tracks beats in orchestral music with expressive timing. Neural approaches (madmom, BeatNet) perform best but accuracy remains significantly lower than on pop/rock.

### Pop

Generally the easiest genre:

- Steady tempo, strong percussive beat.
- Clear onset patterns from drums and synthesizers.
- Occasional syncopation.

### Rock

- Strong beat from drums, but possible tempo drift in live recordings.
- Distortion can obscure transients in guitar-heavy passages.
- Varying dynamics between quiet verses and loud choruses.

---

## Comparison

| Algorithm | Accuracy | Real-time | Classical | Pop/Rock | Complexity |
|-----------|----------|-----------|-----------|----------|------------|
| Energy-based | Low-moderate | Yes | Poor | Good | Very low |
| Spectral flux | Moderate | Yes | Moderate | Good | Low |
| Autocorrelation | Moderate | Yes | Poor | Good | Low-moderate |
| Comb filter bank | Moderate | Yes | Poor | Good | Low-moderate |
| DP beat tracking | Moderate (~60%) | No | Poor | Good | Moderate |
| Multi-feature consensus | Good (~80%) | Partial | Moderate | Good | Moderate |
| RNN/TCN (madmom) | Highest | No (offline) | Good | Excellent | High |
| BeatNet CRNN+PF | Highest | Yes (<50ms) | Good | Excellent | Moderate-high |

---

## Open-Source Libraries

### librosa (Python)

- `beat_track()`: Ellis 2007 dynamic programming.
- `plp()`: Fourier tempogram for predominant local pulse.
- `tempo()`: autocorrelation of onset strength envelope with Gaussian prior.
- `onset_strength()`: spectral flux on mel spectrogram.
- Good for prototyping, not state-of-the-art accuracy.

### aubio (C)

- Multiple onset detection methods: `energy`, `hfc`, `complex`, `phase`, `specdiff`, `kl`, `mkl`, `specflux`.
- Tempo tracking via autocorrelation + comb filtering.
- Designed for real-time, low-latency use.
- GPL-3.0 license.

### Essentia (C++)

- `BeatTrackerMultiFeature`: 5 simultaneous onset detection functions with consensus selection.
- `RhythmExtractor2013`: full pipeline with confidence scoring.
- `TempoCNN`: ML-based tempo estimation.
- AGPL v3 (commercial license available).

### madmom (Python)

- `RNNBeatProcessor`: pre-trained bidirectional LSTM models.
- `DBNBeatTrackingProcessor`: Dynamic Bayesian Network post-processing.
- Joint beat, downbeat, tempo, and meter tracking.
- Consistently highest accuracy across genres in evaluations.
- Handles classical and complex rhythms well.

### BeatNet (Python)

- CRNN + particle filtering (online) or DBN (offline).
- Streaming, real-time (<50ms), online, and offline modes.
- Jointly tracks beat, downbeat, meter, and tempo.

---

## MP3 Decoding Libraries for C++

| Library | Type | License | Notes |
|---------|------|---------|-------|
| **minimp3** | Header-only | CC0 (Public Domain) | SSE/NEON optimized, LAME tag support, float output via `MINIMP3_FLOAT_OUTPUT`. Best choice for minimal dependencies. |
| **dr_mp3** | Header-only | Public Domain / MIT-0 | Based on minimp3 with pull-style API. No LAME tag support. |
| **libmpg123** | Shared/static lib | LGPL 2.1 | Fastest decoder, best standards compliance. Requires linking. |
| **FFmpeg/libavcodec** | Shared libs | LGPL/GPL | Universal format support. Overkill for MP3-only. Complex multi-library API. |

---

## C++ Beat Detection Libraries

| Library | License | Description |
|---------|---------|-------------|
| **Essentia** | AGPL v3 | Full C++ MIR library. `RhythmExtractor2013` for BPM. Complex build. |
| **aubio** | GPL 3.0 | C library for real-time beat tracking. Simple `aubio_tempo_t` API. |
| **MiniBPM** | GPL | Self-contained C++ module for quick fixed-BPM estimates. No dependencies. |
| **BTrack** | GPL | C++ real-time beat tracker by Adam Stark. |
| **MusicBeatDetector** | MIT | Small C++ library for real-time beat detection. |

---

## References

### Foundational Papers

1. Scheirer, E. D. (1998). "Tempo and Beat Analysis of Acoustic Musical Signals." *Journal of the Acoustical Society of America*. [[PDF](https://cagnazzo.wp.imt.fr/files/2013/05/Scheirer98.pdf)]

2. Bello, J. P., Daudet, L., Abdallah, S., Duxbury, C., Davies, M., & Sandler, M. B. (2005). "A Tutorial on Onset Detection in Music Signals." *IEEE Transactions on Audio, Speech, and Language Processing*.

3. Dixon, S. (2006). "Onset Detection Revisited." *Proceedings of the International Conference on Digital Audio Effects (DAFx)*.

4. Klapuri, A., Eronen, A., & Astola, J. (2006). "Analysis of the Meter of Acoustic Musical Signals." *IEEE Transactions on Audio, Speech, and Language Processing*.

5. Ellis, D. P. W. (2007). "Beat Tracking by Dynamic Programming." *Journal of New Music Research*, 36(1). [[PDF](https://www.ee.columbia.edu/~dpwe/pubs/Ellis07-beattrack.pdf)]

6. Grosche, P. & Muller, M. (2011). "Extracting Predominant Local Pulse Information from Music Recordings." *IEEE Transactions on Audio, Speech, and Language Processing*.

### Modern / Deep Learning

7. McFee, B. & Ellis, D. P. W. (2014). "Better Beat Tracking Through Robust Onset Aggregation." *ICASSP*. [[PDF](https://www.ee.columbia.edu/~dpwe/pubs/McFeeE14-beats.pdf)]

8. Tzanetakis, G. & Cook, P. (2014). "Streamlined Tempo Estimation Based on Autocorrelation and Cross-correlation with Pulses." *IEEE Transactions on Audio, Speech, and Language Processing*. [[PDF](https://webhome.csc.uvic.ca/~gtzan/output/taslp2014-tempo-gtzan.pdf)]

9. Boeck, S., Krebs, F., & Widmer, G. (2016). "Joint Beat and Downbeat Tracking with Recurrent Neural Networks." *ACM Multimedia*. [[PDF](https://www.cp.jku.at/research/papers/Boeck_etal_ACMMM_2016.pdf)]

10. Davies, M. & Boeck, S. (2019). "Temporal Convolutional Networks for Musical Audio Beat Tracking." *EUSIPCO*. [[PDF](https://www.eurasip.org/Proceedings/Eusipco/eusipco2019/Proceedings/papers/1570533824.pdf)]

11. Schreiber, H. & Muller, M. (2020). "Music Tempo Estimation: Are We Done Yet?" *Transactions of the International Society for Music Information Retrieval*. [[Link](https://transactions.ismir.net/articles/10.5334/tismir.43)]

12. Heydari, M., Cwitkowitz, F., & Duan, Z. (2021). "BeatNet: CRNN and Particle Filtering for Online Joint Beat Downbeat and Meter Tracking." *ISMIR*. [[arXiv](https://arxiv.org/abs/2108.03576)]

### Supplementary

13. Stark, A. & Plumbley, M. (2011). "Real-Time Visual Beat Tracking Using a Comb Filter Matrix." *ICMC*. [[PDF](https://www.adamstark.co.uk/pdf/papers/comb-filter-matrix-ICMC-2011.pdf)]

14. Alonso, M., Richard, G., & David, B. (2004). "Tempo and Beat Estimation of Musical Signals." *ISMIR*. [[PDF](https://archives.ismir.net/ismir2004/paper/000191.pdf)]

15. Boeck, S. (2016). "Event Detection in Musical Audio." *PhD Dissertation, JKU Linz*. [[PDF](https://www.cp.jku.at/research/papers/Boeck_dissertation.pdf)]

### Library Documentation

- [librosa onset_strength](https://librosa.org/doc/main/generated/librosa.onset.onset_strength.html)
- [librosa beat tracking (DeepWiki)](https://deepwiki.com/librosa/librosa/5.2-beat-tracking-and-tempo-estimation)
- [Essentia BeatTrackerMultiFeature](https://essentia.upf.edu/reference/std_BeatTrackerMultiFeature.html)
- [Essentia rhythm tutorial](https://essentia.upf.edu/tutorial_rhythm_beatdetection.html)
- [aubio documentation](https://aubio.org/manual/latest/)
- [madmom beats documentation](https://madmom.readthedocs.io/en/v0.16/modules/features/beats.html)
- [minimp3 (GitHub)](https://github.com/lieff/minimp3)
- [pocketfft (GitHub)](https://github.com/mreineck/pocketfft)
- [BeatNet (GitHub)](https://github.com/mjhydri/BeatNet)
- [TCN Beat Tracker (GitHub)](https://github.com/ben-hayes/beat-tracking-tcn)
- [FMP Notebooks: Spectral Novelty](https://www.audiolabs-erlangen.de/resources/MIR/FMP/C6/C6S1_NoveltySpectral.html)
- [FMP Notebooks: Autocorrelation Tempogram](https://www.audiolabs-erlangen.de/resources/MIR/FMP/C6/C6S2_TempogramAutocorrelation.html)
- [MiniBPM](https://breakfastquay.com/minibpm/)
- [MusicBeatDetector (GitHub)](https://github.com/introlab/MusicBeatDetector)
