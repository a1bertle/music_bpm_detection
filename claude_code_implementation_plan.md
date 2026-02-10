# BPM Detection with Metronome Overlay — Implementation Plan

## Context

This is a greenfield C++ project that detects BPM from MP3 files and outputs a WAV file with a metronome click track overlaid at the detected beat positions. The project targets classical, pop, and rock music. The repo currently has only a README, LICENSE (MIT), and empty `src/` directory.

## Architecture Overview

A multi-stage pipeline: **MP3 decode → onset detection → tempo estimation → beat tracking → metronome overlay → WAV output**.

```
MP3 → [Mp3Decoder] → AudioBuffer(stereo) ─────────────────────┐
                          │ .to_mono()                         │
                          ▼                                    │
                   AudioBuffer(mono)                           │
                          │                                    │
                          ▼                                    │
                   [OnsetDetector] → onset strength vector     │
                          │                                    │
                   ┌──────┴──────┐                             │
                   ▼              ▼                             │
            [TempoEstimator]  (onset data)                     │
                   │              │                             │
                   ▼              ▼                             │
              {bpm, period} → [BeatTracker] → beat positions   │
                                                   │           │
                                                   ▼           ▼
                                              [Metronome] (mix clicks into stereo)
                                                   │
                                                   ▼
                                              [WavWriter] → output.wav
```

## Dependencies

| Library | Purpose | Type | License |
|---------|---------|------|---------|
| **minimp3** | MP3 decoding | Header-only (FetchContent) | CC0 Public Domain |
| **pocketfft** | FFT for spectral analysis | Header-only (FetchContent) | BSD |

No system-level dependencies beyond a C++17 compiler and CMake 3.16+.

## File Structure

```
CMakeLists.txt
include/bpm/
    audio_buffer.h
    mp3_decoder.h
    onset_detector.h
    tempo_estimator.h
    beat_tracker.h
    metronome.h
    wav_writer.h
    pipeline.h
src/
    main.cpp
    audio_buffer.cpp
    mp3_decoder.cpp
    onset_detector.cpp
    tempo_estimator.cpp
    beat_tracker.cpp
    metronome.cpp
    wav_writer.cpp
    pipeline.cpp
```

## Module Specifications

### 1. AudioBuffer (`audio_buffer.h/.cpp`)
- Struct holding `std::vector<float> samples`, `int sample_rate`, `int channels`
- `to_mono()` method averages L+R channels
- `num_frames()` and `duration_sec()` helpers

### 2. Mp3Decoder (`mp3_decoder.h/.cpp`)
- Static `decode(filepath)` → `AudioBuffer`
- Uses minimp3 high-level API (`mp3dec_load()`) with `MINIMP3_FLOAT_OUTPUT`
- Only file that includes minimp3 implementation defines

### 3. OnsetDetector (`onset_detector.h/.cpp`) — core algorithm
- **Spectral flux on mel-scaled spectrogram**:
  1. Frame audio with Hann window (2048 samples, 512 hop)
  2. Real-to-complex FFT via pocketfft → power spectrum
  3. Apply 40-band mel filterbank (30–8000 Hz)
  4. Log compression: `log10(energy + 1e-10)`
  5. Half-wave rectified difference between consecutive frames
  6. Sum across bands → one onset strength value per frame
  7. Normalize (subtract mean, divide by stddev)
- Params: `fft_size=2048`, `hop_size=512`, `n_mel_bands=40`

### 4. TempoEstimator (`tempo_estimator.h/.cpp`)
- **Autocorrelation of onset function**:
  1. Convert BPM range (50–220) to lag range in onset frames
  2. Compute autocorrelation at each candidate lag
  3. Apply log-Gaussian tempo prior centered at 120 BPM
  4. Octave error correction: if half-lag peak > 0.8× best peak, prefer faster tempo
- Returns `{bpm, period_frames}`

### 5. BeatTracker (`beat_tracker.h/.cpp`)
- **Dynamic programming (Ellis 2007)**:
  1. Score each frame: `onset_strength[t]`
  2. Forward pass: for each frame, search window `[t-2τ, t-τ/2]` for best predecessor with log-Gaussian penalty: `-α·(log(Δ/τ))²`
  3. Backtrace from highest-scoring frame near end
  4. Convert beat frame indices to sample positions (`frame × hop_size`)
- Param: `alpha=680` (tempo regularity strength)

### 6. Metronome (`metronome.h/.cpp`)
- **Click synthesis**: exponentially-decaying sine burst
  - `click[n] = amplitude × sin(2π × freq × t) × exp(-decay × t)`
  - Default: 1000 Hz, 20ms duration, decay=200
- **Mixing**: add click samples at each beat position across all channels
- **Clipping prevention**: clamp all samples to [-1.0, 1.0]

### 7. WavWriter (`wav_writer.h/.cpp`)
- Writes standard 44-byte PCM WAV header + 16-bit signed LE samples
- Float-to-int16: `clamp(sample, -1, 1) × 32767`

### 8. Pipeline (`pipeline.h/.cpp`)
- Orchestrates full chain: decode → mono → onset → tempo → beats → metronome overlay on stereo → WAV write
- Prints detected BPM and beat count

### 9. main.cpp — CLI
```
Usage: bpm_detect [options] <input.mp3>
  -o, --output <path>     Output WAV path (default: <input>_click.wav)
  -v, --verbose           Print detailed info
  --min-bpm <float>       Min BPM (default: 50)
  --max-bpm <float>       Max BPM (default: 220)
  --click-volume <float>  Click volume 0.0-1.0 (default: 0.5)
  --click-freq <float>    Click frequency Hz (default: 1000)
  -h, --help              Show help
```
Simple argc/argv loop parser, no external library.

## Implementation Order

1. **CMakeLists.txt** — build system with FetchContent for minimp3 + pocketfft
2. **AudioBuffer** — common data type
3. **Mp3Decoder** — decode MP3 to float PCM
4. **WavWriter** — write WAV files (enables decode→write verification by ear)
5. **OnsetDetector** — mel filterbank + spectral flux (most complex module)
6. **TempoEstimator** — autocorrelation + tempo prior + octave correction
7. **BeatTracker** — DP forward pass + backtrace
8. **Metronome** — click synthesis + mixing
9. **Pipeline** — wire everything together
10. **main.cpp** — CLI argument parsing
11. **Testing/tuning** — verify on pop, rock, and classical tracks

## Genre Handling

- **Pop**: Default params work well — steady tempo, strong percussive onsets
- **Rock**: Same defaults — strong beat, slight drift absorbed by DP tracker window
- **Classical**: Hardest case — single global BPM is a limitation for rubato. The DP tracker's flexible beat placement helps with small tempo fluctuations. A future enhancement could add windowed local tempo estimation

## Verification

1. **Build**: `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build`
2. **Decode sanity check**: decode an MP3 → write WAV immediately → play to confirm audio integrity
3. **BPM accuracy**: test on tracks with known BPM (metronome recordings, well-known pop songs)
4. **Listen test**: play output WAV files — metronome clicks should align with the beat
5. **Genre coverage**: test at least one pop, one rock, and one classical track
