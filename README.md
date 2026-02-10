# music_bpm_detection

A C++17 BPM detection tool for MP3 files. It detects a global tempo, tracks beats, and writes a WAV file with an overlaid metronome click track.

## Architecture

The project implements a multi-stage audio analysis pipeline:

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

Key components:

- `Mp3Decoder`: Decodes MP3 to float PCM.
- `AudioBuffer`: Common container for audio data, supports mono conversion.
- `OnsetDetector`: Computes spectral flux on a mel-scaled spectrogram.
- `TempoEstimator`: Autocorrelation-based tempo estimation with a log-Gaussian prior and octave correction.
- `BeatTracker`: Dynamic-programming beat tracking (Ellis 2007 style).
- `Metronome`: Synthesizes an exponential-decay click and mixes at beat positions.
- `WavWriter`: Writes 16-bit PCM WAV output.

## Directory and File Structure

```
CMakeLists.txt
HISTORY.md
README.md
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
scripts/
    build.sh
third_party/
    minimp3/
    pocketfft/
```

## Build

```
./scripts/build.sh
```

The build produces `bpm_detect` under `build/`.

## Usage

```
./build/bpm_detect [options] <input.mp3>
  -o, --output <path>     Output WAV path (default: <input>_click.wav)
  -v, --verbose           Print detailed info
  --min-bpm <float>       Min BPM (default: 50)
  --max-bpm <float>       Max BPM (default: 220)
  --click-volume <float>  Click volume 0.0-1.0 (default: 0.5)
  --click-freq <float>    Click frequency Hz (default: 1000)
  -h, --help              Show help
```

Example:

```
./build/bpm_detect -v --min-bpm 60 --max-bpm 180 --click-volume 0.4 song.mp3
```

## Theoretical Background

### 1. Onset Detection (Spectral Flux)

- Audio is framed with a Hann window (`fft_size=2048`, `hop_size=512`).
- A real FFT is computed per frame.
- A 40-band mel filterbank (30–8000 Hz) is applied to the power spectrum.
- Log compression is applied to mel-band energy.
- Spectral flux is computed as the half-wave rectified difference between consecutive frames.
- The onset strength signal is normalized to zero mean and unit variance.

This yields a single onset strength value per frame that emphasizes note attacks and rhythmic energy.

### 2. Tempo Estimation (Autocorrelation + Prior)

- Autocorrelation is computed over the onset strength signal for lags corresponding to a BPM range (default 50–220).
- A log-Gaussian prior centered at 120 BPM biases the selection toward common tempos.
- Octave error correction prefers faster tempos if the half-period peak is strong enough.

The output is a global BPM estimate and a period in onset frames.

### 3. Beat Tracking (Dynamic Programming)

- For each frame, a score is built from the onset strength.
- A predecessor search window is evaluated with a log-Gaussian penalty on deviations from the expected period.
- The best-scoring path is backtraced from the end to yield beat positions.

The result is a sequence of beat times (frame indices) which are mapped into sample positions.

### 4. Metronome Overlay

- Each beat triggers a short (20 ms) decaying sine burst.
- The click is mixed into all channels and the final signal is clamped to [-1, 1].

## Dependencies

- `minimp3` (CC0) for MP3 decoding
- `pocketfft` (BSD) for FFTs

Both are vendored under `third_party/` to enable offline builds.

## Notes

- This implementation assumes a single global tempo per track.
- Classical or rubato material may require more advanced, time-varying tempo modeling.
