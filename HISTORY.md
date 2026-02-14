# History

## Claude Code Research and Planning

This project began with research and planning carried out by Claude Code. The focus was to design a practical, greenfield C++ pipeline for BPM detection on MP3 audio and to output a WAV file with a metronome overlay. The result of that work is captured in the implementation plan:

- `claude_code_implementation_plan.md`

That plan lays out the architecture, data flow, dependency choices, module specifications, and a recommended implementation order. It also documents algorithm choices for onset detection, tempo estimation, and beat tracking, plus CLI requirements and verification steps.

## Implementation with GPT-5.2-Codex

Implementation was executed using GPT-5.2-Codex as a coding agent. The work followed the plan end-to-end, resulting in a full CMake-based C++ project with:

- A complete audio pipeline from MP3 decode through onset detection, tempo estimation, beat tracking, metronome overlay, and WAV output.
- Header and source files for each module specified in the plan.
- A CLI tool (`bpm_detect`) matching the plan’s argument requirements.
- Vendored third-party dependencies placed under `third_party/` to ensure offline builds.
- A `scripts/build.sh` helper script to build the project.

Where needed, build issues were resolved pragmatically, including adapting the FFT integration to the vendored `pocketfft` C API and updating CMake to compile the C source directly. The implementation reflects the structure and sequencing defined in the Claude Code plan, with minor adjustments to improve reliability in offline environments.

## Tempo Estimator Bug Fixes (Claude Code)

Testing against three tracks revealed that the tempo estimator was returning 120.185 BPM for a track with a known tempo of 116 BPM. Investigation by Claude Code identified three bugs in `src/tempo_estimator.cpp`:

1. **Prior bias toward 120 BPM.** The log-Gaussian tempo prior used `std::log` (natural log) with `sigma=0.5`, making it far too narrow. At this width the prior dominated over the actual autocorrelation signal, pulling results toward 120 BPM regardless of the true tempo. Fixed by switching to `std::log2` with `sigma=1.0` so the prior width is properly 1 octave.

2. **Sub-harmonic octave error.** Autocorrelation naturally produces strong peaks at integer multiples of the true period (2T, 3T, ...). The original octave correction only compared a single half-lag point against raw (not prior-weighted) autocorrelation, which failed to escape sub-harmonic traps. Replaced with iterative half-lag search: at each step, a windowed scan around `best_lag/2` finds the strongest candidate, and accepts it if the prior-weighted score exceeds the median noise floor. This correctly promotes the fastest plausible tempo.

3. **Coarse BPM resolution.** With `hop_size=512` at 44.1 kHz, each integer lag step spans ~2.7 BPM, limiting precision. Added parabolic interpolation around the autocorrelation peak to recover sub-lag accuracy.

Additionally, the raw autocorrelation was normalized by overlap count to prevent longer lags from having inflated sums.

### Test results after fixes

| Track | Detected BPM | Actual BPM | Error |
|-------|-------------|------------|-------|
| Track 1 | 128.2 | 128 | +0.2% |
| Track 2 | 150.5 | 150 | +0.3% |
| Track 3 (self-recording, tempo fluctuations) | 119.7 | ~116 | +3.2% |

Track 3 is a self-recording with strings and a drum loop where the tempo fluctuates, explaining the larger error from the single-global-BPM estimator.

## Pipeline Explainer Documents

Detailed plain-text explainer documents were created for each stage of the BPM detection pipeline, with ASCII diagrams, worked numeric examples, and step-by-step walkthroughs. These live under `docs/`:

- `ONSET_DETECTOR_EXPLAINED.txt` — Hann windowing, FFT, mel filterbank, spectral flux
- `TEMPO_ESTIMATOR_EXPLAINED.txt` — autocorrelation, tempo prior, octave correction, parabolic interpolation
- `BEAT_TRACKER_EXPLAINED.txt` — dynamic programming forward pass, backtrace, frame-to-sample conversion
- `MP3_DECODER_EXPLAINED.txt` — MP3 compression/decompression, minimp3 integration
- `METRONOME_EXPLAINED.txt` — click synthesis, stereo mixing, clipping prevention
- `WAV_WRITER_EXPLAINED.txt` — RIFF/WAVE header layout, float-to-int16 conversion
- `PIPELINE_EXPLAINED.txt` — full pipeline overview, data flow, and data size breakdown

Originally created as markdown, the files were converted to plain text with pure ASCII diagrams after discovering that unicode box-drawing characters render with inconsistent widths in monospace fonts.

## MP4/M4A Input Support

Added support for MP4 and M4A audio files as input. The approach mirrors the existing MP3 pipeline but uses `ffmpeg` as an external subprocess to extract audio:

1. `ffmpeg` converts the MP4/M4A to a temporary 16-bit PCM WAV file (44100 Hz, stereo).
2. A new `WavReader` module parses the WAV file into an `AudioBuffer`.
3. The temporary file is deleted after reading.

New files:
- `include/bpm/wav_reader.h` / `src/wav_reader.cpp` — WAV file parser (reused by YouTube decoder)
- `include/bpm/mp4_decoder.h` / `src/mp4_decoder.cpp` — ffmpeg subprocess + WavReader

The pipeline auto-selects the decoder based on file extension (`.mp4`, `.m4a`).

### Test results

| Track | Format | Detected BPM | Actual BPM |
|-------|--------|-------------|------------|
| Sunrise | MP3 | 128.2 | 128 |
| Sunrise | MP4 | 126.0 | 128 |

Both formats produce consistent results for the same track.

## YouTube URL Support

Added support for YouTube URLs as input, enabling BPM detection directly from a video link without manual downloading. Uses `yt-dlp` and `ffmpeg` as external subprocesses:

1. `yt-dlp` downloads the best available audio stream (with `--no-playlist` to prevent downloading entire playlists).
2. `ffmpeg` converts the downloaded audio to a temporary 16-bit PCM WAV file.
3. `WavReader` loads the WAV into an `AudioBuffer`.
4. Both temporary files are cleaned up after reading.

New files:
- `include/bpm/youtube_decoder.h` / `src/youtube_decoder.cpp`

The pipeline detects URLs by checking for `://` in the input path. Default output for URLs is `output_click.wav` instead of appending to the URL string.

### Test results

| Track | Source | Detected BPM | Actual BPM |
|-------|--------|-------------|------------|
| Art Exhibit — Young the Giant | YouTube URL | 150.6 | 150 |

Prerequisites: `yt-dlp` and `ffmpeg` must be installed. The README was updated with installation instructions for macOS (Homebrew) and Ubuntu/Debian.

## Sub-Harmonic Override Fix in Multi-Candidate Evaluation (Claude Code)

Testing with Bad Bunny — NUEVAYoL (actual: 118 BPM) revealed that the pipeline returned 78.3 BPM despite the tempo estimator correctly identifying 117.5 BPM as the top candidate.

### Root cause

The multi-candidate beat tracker evaluation was overriding the tempo estimator's primary pick with a 2/3 sub-harmonic (78.3 BPM ≈ 117.5 × 2/3). This happened because:

1. **The ±40% BPM filter didn't catch it.** The filter blocks candidates outside 0.6x–1.4x of the primary BPM. The ratio 78.3/118 = 0.66 squeaks past the 0.6 lower bound, unlike a true half-tempo (0.5x) which would be blocked.

2. **Slower tempos get inflated per-beat DP scores.** The beat tracker's DP search window is [0.5×period, 2.0×period]. For the slower period=66 candidate, this window spans 33–132 frames — wide enough to always find a strong onset nearby. Combined with fewer total beats (240 vs 354), the normalized score (score/beat_count) was 4.6% higher for the incorrect slower tempo (1.979 vs 1.893).

### Fix

Added a primary-candidate margin (`kPrimaryMargin = 1.05`) in `src/pipeline.cpp`. Non-primary candidates must now exceed the tempo estimator's primary pick by at least 5% in normalized beat-tracker score to override it. This prevents marginal sub-harmonic overrides while still allowing genuinely better candidates (with >5% advantage) to win.

The corresponding Python implementation in `visualizer/bpm_visualizer.ipynb` (cell: "Multi-candidate evaluation") was updated to match.

### Test results

| Track | Before | After | Actual BPM |
|-------|--------|-------|------------|
| Bad Bunny — NUEVAYoL | 78.3 BPM | 117.5 BPM | 118 |
| Foals — My Number | 129.2 BPM | 129.2 BPM | 128 |

## Tighten BPM Candidate Filter to ±30% (Claude Code)

Testing Bad Bunny — NUEVAYoL with a different YouTube upload revealed that the 5% primary margin fix was fragile. The 2/3 sub-harmonic (78.3 BPM) achieved a 5.5% higher normalized score on one upload but only 4.6% on another — landing on opposite sides of the 5% threshold depending on minor encoding differences.

### Root cause

The ±40% BPM filter (0.6–1.4) was too permissive. The 2/3 sub-harmonic ratio (78.3/117.5 = 0.667) passed the 0.6 lower bound, allowing it into the candidate comparison where its inflated per-beat DP score could marginally beat the 5% margin on some audio encodes.

### Fix

Tightened the BPM candidate filter from ±40% (0.6–1.4) to ±30% (0.7–1.3) in `src/pipeline.cpp`. This directly excludes both 2/3 sub-harmonics (ratio 0.667 < 0.7) and 3/2 super-harmonics (ratio 1.5 > 1.3), while still allowing legitimate nearby candidates within ±30% of the primary estimate.

Updated the notebook's multi-candidate evaluation cell and `CLAUDE.md` parameter reference to match.

### Test results

| Track | Before (±40%) | After (±30%) | Actual BPM |
|-------|---------------|--------------|------------|
| Bad Bunny — NUEVAYoL (upload 1) | 117.5 BPM | 117.5 BPM | 117 |
| Bad Bunny — NUEVAYoL (upload 2) | 78.3 BPM | 117.5 BPM | 117 |
| Foals — My Number | 129.2 BPM | 129.2 BPM | 128 |
