# BPM Detection Algorithm Visualizer

A Jupyter notebook that re-implements and visualizes each stage of the C++ BPM detection pipeline in Python.

## Setup

```bash
cd visualizer
pip install -r requirements.txt
```

## Usage

### Local (Jupyter or VS Code)

Open `bpm_visualizer.ipynb` in Jupyter or VS Code. Edit the config cell at the top to point to your audio file:

```python
AUDIO_PATH = '../test_samples/Foals - My Number (Official Audio).mp3'
KNOWN_BPM = 128  # Set to None if unknown
```

Run all cells. The notebook walks through every pipeline stage with plots and explanations.

YouTube URLs are also supported — set `AUDIO_PATH` to a URL and the notebook will download via yt-dlp + ffmpeg:

```python
AUDIO_PATH = 'https://www.youtube.com/watch?v=...'
KNOWN_BPM = None
```

### Google Colab

You can run the notebook in the browser without any local setup:

1. Open in Colab: [Launch on Google Colab](https://colab.research.google.com/github/a1bertle/music_bpm_detection/blob/main/visualizer/bpm_visualizer.ipynb)
2. Colab has numpy, scipy, and matplotlib pre-installed. Install the remaining dependencies by adding a cell at the top:
   ```python
   !pip install librosa
   ```
3. Upload an audio file via the Colab file browser (folder icon in the left sidebar), then set `AUDIO_PATH` to its path (e.g. `'/content/song.mp3'`)
4. Alternatively, use a YouTube URL as `AUDIO_PATH` — yt-dlp and ffmpeg are available on Colab by default
5. Run all cells

## Notebook Sections

### 1. Audio Loading
Loads audio via librosa, converts to mono, plots stereo and mono waveforms.

### 2. Onset Detection (Mel-Frequency Spectral Flux)
- Builds a 40-band mel filterbank (30–8000 Hz)
- Computes STFT with Hann window (2048 FFT, 512 hop)
- Log-compressed mel energy → half-wave rectified spectral flux → z-score normalization
- **Plots**: mel filterbank, mel spectrogram heatmap, onset strength overlaid on waveform

### 3. Tempo Estimation (Autocorrelation + Prior)
- Normalized autocorrelation over lag range (50–220 BPM)
- Log-Gaussian tempo prior centered at 120 BPM (σ = 1.0 octave)
- Iterative octave correction with median noise floor threshold
- Parabolic interpolation for sub-lag precision
- Top 5 candidate periods collected for beat tracking
- **Plots**: raw autocorrelation, prior curve, weighted autocorrelation, octave correction steps, candidate markers

### 4. Beat Tracking (Dynamic Programming — Ellis 2007)
- Forward DP pass with log-ratio penalty (α = 680)
- Backtrace from last 10% of signal
- Multi-candidate evaluation with ±30% BPM filter and normalized scoring
- **Plots**: penalty function shape, candidate comparison bar chart, DP score over time, beats on onset strength (full + zoomed)

### 5. Metronome Click
- 20ms exponentially-decaying sine at 1000 Hz (decay = 200)
- **Plots**: click waveform, waveform with beat markers (full + zoomed)

### 6. Full Pipeline Summary
- 4-panel overview: waveform → onset strength → autocorrelation → beats
- Text summary with detected BPM, beat count, and error vs known BPM

## Parameters

All values match the C++ implementation exactly:

| Parameter | Value | C++ Source |
|-----------|-------|------------|
| FFT size | 2048 | `onset_detector.h` |
| Hop size | 512 | `onset_detector.h` |
| Mel bands | 40 (30–8000 Hz) | `onset_detector.cpp` |
| Prior center | 120 BPM, σ=1.0 | `tempo_estimator.cpp` |
| DP alpha | 680.0 | `beat_tracker.h` |
| Click | 1000 Hz, 20ms, decay=200 | `metronome.h` |

## Dependencies

- **numpy**, **scipy** — signal processing
- **matplotlib** — plotting
- **librosa** — audio file loading only (all algorithms implemented from scratch)
- **ipykernel** — Jupyter kernel
