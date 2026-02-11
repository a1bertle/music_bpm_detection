# music_bpm_detection

A C++17 command-line tool that detects the tempo (BPM) of an MP3 file and outputs a WAV file with a metronome click track mixed in at the detected beat positions.

## Build

Requires CMake 3.16+ and a C++17 compiler.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Or use the included script:

```bash
./scripts/build.sh
```

The executable is produced at `build/bpm_detect`.

## Usage

```
bpm_detect [options] <input.mp3>
```

| Option | Description | Default |
|--------|-------------|---------|
| `-o, --output <path>` | Output WAV file path | `<input>_click.wav` |
| `-v, --verbose` | Print detailed processing info | off |
| `--min-bpm <float>` | Minimum BPM to detect | 50 |
| `--max-bpm <float>` | Maximum BPM to detect | 220 |
| `--click-volume <float>` | Click volume (0.0 - 1.0) | 0.5 |
| `--click-freq <float>` | Click tone frequency in Hz | 1000 |
| `-h, --help` | Show help | |

### Examples

Detect BPM and write a click track with default settings:

```bash
./build/bpm_detect song.mp3
```

Custom output path, narrowed BPM range, quieter click:

```bash
./build/bpm_detect -o output.wav --min-bpm 60 --max-bpm 180 --click-volume 0.3 song.mp3
```

## How It Works

The tool runs a multi-stage audio analysis pipeline:

```
MP3 file
  │
  ▼
Mp3Decoder ──► AudioBuffer (stereo float PCM)
  │                          │
  │ to_mono()                │ (preserved for final mix)
  ▼                          │
OnsetDetector                │
  │ spectral flux            │
  ▼                          │
TempoEstimator               │
  │ autocorrelation          │
  ▼                          │
BeatTracker                  │
  │ dynamic programming      │
  ▼                          ▼
Metronome ◄──────────────────┘
  │ overlay clicks on stereo
  ▼
WavWriter ──► output.wav
```

### 1. Onset Detection

Audio is framed with a Hann window (2048 samples, 512 hop) and transformed via real FFT. A 40-band mel filterbank (30-8000 Hz) is applied to each frame's power spectrum, followed by log compression. The spectral flux -- the half-wave rectified difference between consecutive mel frames -- produces an onset strength signal that peaks at note attacks and rhythmic transients.

### 2. Tempo Estimation

Autocorrelation of the onset strength signal reveals periodicities corresponding to candidate tempos. A log-Gaussian prior centered at 120 BPM biases selection toward common tempos. An octave error correction step prefers the faster tempo when the half-period peak is sufficiently strong.

### 3. Beat Tracking

A dynamic programming pass (Ellis 2007) finds the globally optimal sequence of beat positions by maximizing onset alignment while penalizing deviations from the estimated inter-beat interval. Beats are backtraced from the highest-scoring frames and converted to sample positions.

### 4. Metronome Overlay

A short (20 ms) exponentially-decaying sine burst is synthesized and mixed into the stereo signal at each beat position. The output is clamped to [-1, 1] to prevent clipping.

## Project Structure

```
CMakeLists.txt              Build configuration
include/bpm/
  audio_buffer.h            PCM audio container with mono conversion
  mp3_decoder.h             MP3 → float PCM decoding
  onset_detector.h          Mel-spectral-flux onset detection
  tempo_estimator.h         Autocorrelation tempo estimation
  beat_tracker.h            DP beat tracking
  metronome.h               Click synthesis and overlay
  wav_writer.h              16-bit PCM WAV output
  pipeline.h                End-to-end orchestration
src/
  main.cpp                  CLI entry point
  audio_buffer.cpp
  mp3_decoder.cpp
  onset_detector.cpp
  tempo_estimator.cpp
  beat_tracker.cpp
  metronome.cpp
  wav_writer.cpp
  pipeline.cpp
third_party/
  minimp3/                  MP3 decoder (CC0)
  pocketfft/                FFT library (BSD)
```

## Dependencies

Both dependencies are vendored under `third_party/` for offline builds -- no package manager or network access required.

| Library | Purpose | License |
|---------|---------|---------|
| [minimp3](https://github.com/lieff/minimp3) | Header-only MP3 decoder | CC0 (Public Domain) |
| [pocketfft](https://github.com/mreineck/pocketfft) | FFT computation | BSD |

## Limitations

- Estimates a single global BPM per track. Music with significant tempo changes (rubato, accelerando) will receive an averaged tempo.
- Classical music with soft onsets and no percussion is the hardest case for accurate beat placement.
- Output is WAV only (no MP3 re-encoding).

## License

MIT
