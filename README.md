# music_bpm_detection

A C++17 command-line tool that detects the tempo (BPM) of an audio file or YouTube video and outputs a WAV file with a metronome click track mixed in at the detected beat positions.

Supported inputs: MP3, MP4/M4A, and YouTube URLs.

## Prerequisites

- CMake 3.16+ and a C++17 compiler
- **ffmpeg** — required for MP4/M4A and YouTube inputs ([install](https://ffmpeg.org/download.html))
- **yt-dlp** — required for YouTube URL inputs ([install](https://github.com/yt-dlp/yt-dlp#installation))

```bash
# macOS (Homebrew)
brew install ffmpeg yt-dlp

# Ubuntu/Debian
sudo apt install ffmpeg
pip install yt-dlp
```

## Build

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
bpm_detect [options] <input>
```

`<input>` can be an MP3 file, MP4/M4A file, or a YouTube URL.

| Option | Description | Default |
|--------|-------------|---------|
| `-o, --output <path>` | Output WAV file path | `<input>_click.wav` |
| `-v, --verbose` | Print detailed processing info | off |
| `--min-bpm <float>` | Minimum BPM to detect | 50 |
| `--max-bpm <float>` | Maximum BPM to detect | 220 |
| `--click-volume <float>` | Click volume (0.0 - 1.0) | 0.5 |
| `--click-freq <float>` | Click tone frequency in Hz | 1000 |
| `--accent-downbeats` | Higher-pitched click on downbeats | off |
| `--downbeat-freq <float>` | Downbeat click frequency in Hz | 1500 |
| `-h, --help` | Show help | |

### Examples

Detect BPM from an MP3 file:

```bash
./build/bpm_detect song.mp3
```

Detect BPM from an MP4 or M4A file:

```bash
./build/bpm_detect video.mp4
```

Detect BPM from a YouTube URL:

```bash
./build/bpm_detect "https://www.youtube.com/watch?v=dQw4w9WgXcQ"
```

Custom output path, narrowed BPM range, quieter click:

```bash
./build/bpm_detect -o output.wav --min-bpm 60 --max-bpm 180 --click-volume 0.3 song.mp3
```

## How It Works

The tool runs a multi-stage audio analysis pipeline:

```
Input (MP3 / MP4 / YouTube URL)
  │
  ▼
Decoder ──────► AudioBuffer (stereo float PCM)
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
  ▼                          │
MeterDetector                │
  │ accent pattern analysis  │
  ▼                          ▼
Metronome ◄──────────────────┘
  │ overlay clicks on stereo
  ▼
WavWriter ──► output.wav
```

The decoder is selected automatically: `Mp3Decoder` for `.mp3` files, `Mp4Decoder` for `.mp4`/`.m4a` (via ffmpeg), or `YoutubeDecoder` for URLs (via yt-dlp + ffmpeg).

### 1. Onset Detection

Audio is framed with a Hann window (2048 samples, 512 hop) and transformed via real FFT. A 40-band mel filterbank (30-8000 Hz) is applied to each frame's power spectrum, followed by log compression. The spectral flux -- the half-wave rectified difference between consecutive mel frames -- produces an onset strength signal that peaks at note attacks and rhythmic transients.

### 2. Tempo Estimation

Autocorrelation of the onset strength signal reveals periodicities corresponding to candidate tempos. A log-Gaussian prior centered at 120 BPM biases selection toward common tempos. An octave error correction step prefers the faster tempo when the half-period peak is sufficiently strong.

### 3. Beat Tracking

A dynamic programming pass (Ellis 2007) finds the globally optimal sequence of beat positions by maximizing onset alignment while penalizing deviations from the estimated inter-beat interval. Beats are backtraced from the highest-scoring frames and converted to sample positions.

### 4. Meter Detection

The detected beats are analyzed for accent patterns to identify the time signature (2/4, 3/4, 4/4, or 6/8). For each candidate grouping (2, 3, or 4 beats per measure) at every phase offset, the algorithm computes an accent contrast score (how much the proposed downbeat stands out) and a beat-level autocorrelation at that lag. A compound subdivision check distinguishes 6/8 from 2/4 or 3/4 by comparing onset strength at ternary (1/3, 2/3) vs binary (1/2) inter-beat positions.

### 5. Metronome Overlay

A short (20 ms) exponentially-decaying sine burst is synthesized and mixed into the stereo signal at each beat position. The output is clamped to [-1, 1] to prevent clipping. With `--accent-downbeats`, downbeats receive a higher-pitched click (1500 Hz by default) to distinguish them from regular beats.

## Project Structure

```
CMakeLists.txt              Build configuration
include/bpm/
  audio_buffer.h            PCM audio container with mono conversion
  mp3_decoder.h             MP3 → float PCM decoding
  mp4_decoder.h             MP4/M4A → float PCM (via ffmpeg)
  youtube_decoder.h         YouTube URL → float PCM (via yt-dlp + ffmpeg)
  wav_reader.h              WAV file reader (used by MP4/YouTube decoders)
  onset_detector.h          Mel-spectral-flux onset detection
  tempo_estimator.h         Autocorrelation tempo estimation
  beat_tracker.h            DP beat tracking
  meter_detector.h          Time signature detection
  metronome.h               Click synthesis and overlay
  wav_writer.h              16-bit PCM WAV output
  pipeline.h                End-to-end orchestration
src/
  main.cpp                  CLI entry point
  audio_buffer.cpp
  mp3_decoder.cpp
  mp4_decoder.cpp
  youtube_decoder.cpp
  wav_reader.cpp
  onset_detector.cpp
  tempo_estimator.cpp
  beat_tracker.cpp
  meter_detector.cpp
  metronome.cpp
  wav_writer.cpp
  pipeline.cpp
docs/
  ONSET_DETECTOR_EXPLAINED.txt
  TEMPO_ESTIMATOR_EXPLAINED.txt
  BEAT_TRACKER_EXPLAINED.txt
  MP3_DECODER_EXPLAINED.txt
  METRONOME_EXPLAINED.txt
  WAV_WRITER_EXPLAINED.txt
  PIPELINE_EXPLAINED.txt
scripts/
  build.sh                  Build helper script
  test.py                   Automated test runner
third_party/
  minimp3/                  MP3 decoder (CC0)
  pocketfft/                FFT library (BSD)
```

## Dependencies

### Vendored (no install needed)

| Library | Purpose | License |
|---------|---------|---------|
| [minimp3](https://github.com/lieff/minimp3) | Header-only MP3 decoder | CC0 (Public Domain) |
| [pocketfft](https://github.com/mreineck/pocketfft) | FFT computation | BSD |

### External (optional, install separately)

| Tool | Required for | Install |
|------|-------------|---------|
| [ffmpeg](https://ffmpeg.org/) | MP4/M4A and YouTube inputs | `brew install ffmpeg` |
| [yt-dlp](https://github.com/yt-dlp/yt-dlp) | YouTube URL inputs | `brew install yt-dlp` |

MP3 input requires no external tools. MP4/M4A and YouTube features are only available when the corresponding tools are installed.

## Visualizer

An interactive Jupyter notebook that re-implements the entire pipeline in Python with step-by-step plots and explanations. See [visualizer/README.md](visualizer/README.md) for setup and usage.

## Limitations

- Estimates a single global BPM per track. Music with significant tempo changes (rubato, accelerando) will receive an averaged tempo.
- Classical music with soft onsets and no percussion is the hardest case for accurate beat placement.
- Output is WAV only (no MP3 re-encoding).
- YouTube downloads require a working internet connection and are subject to yt-dlp compatibility with YouTube.

## License

GNU GPLv3. See `LICENSE`.
