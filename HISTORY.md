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
