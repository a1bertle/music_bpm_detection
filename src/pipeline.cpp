#include "bpm/pipeline.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "bpm/beat_tracker.h"
#include "bpm/meter_detector.h"
#include "bpm/metronome.h"
#include "bpm/mp3_decoder.h"
#include "bpm/mp4_decoder.h"
#include "bpm/onset_detector.h"
#include "bpm/youtube_decoder.h"
#include "bpm/tempo_estimator.h"
#include "bpm/wav_writer.h"

namespace bpm {
namespace {

std::string get_extension(const std::string &path) {
  auto dot = path.rfind('.');
  if (dot == std::string::npos) {
    return "";
  }
  std::string ext = path.substr(dot);
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return ext;
}

std::string sanitize_filename(const std::string &name) {
  std::string result;
  result.reserve(name.size());
  for (char c : name) {
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
        c == '"' || c == '<' || c == '>' || c == '|' ||
        c == ' ' || c == '-') {
      result += '_';
    } else {
      result += c;
    }
  }
  return result;
}

}  // namespace

void Pipeline::run(const std::string &input_path,
                   const std::string &output_path,
                   const PipelineOptions &options) const {
  AudioBuffer stereo;
  if (input_path.find("://") != std::string::npos) {
    stereo = YoutubeDecoder::decode(input_path);
  } else {
    std::string ext = get_extension(input_path);
    if (ext == ".mp3") {
      stereo = Mp3Decoder::decode(input_path);
    } else if (ext == ".mp4" || ext == ".m4a") {
      stereo = Mp4Decoder::decode(input_path);
    } else {
      throw std::runtime_error("Unsupported file format: " + ext +
                               "\nSupported formats: .mp3, .mp4, .m4a, YouTube URL");
    }
  }
  if (options.verbose) {
    std::cout << "Decoded " << stereo.num_frames() << " frames @ " << stereo.sample_rate << " Hz.\n";
  }

  AudioBuffer mono = stereo.to_mono();
  OnsetDetector onset_detector;
  auto onset = onset_detector.compute(mono);
  if (options.verbose) {
    std::cout << "Computed onset strength with " << onset.onset_strength.size() << " frames.\n";
  }

  TempoEstimator tempo_estimator;
  auto tempo = tempo_estimator.estimate(onset.onset_strength,
                                        mono.sample_rate,
                                        onset.hop_size,
                                        options.min_bpm,
                                        options.max_bpm,
                                        options.verbose);
  // Evaluate multiple tempo candidates through the beat tracker and pick the
  // one with the highest DP score.  This resolves cases where autocorrelation
  // favours a sub-optimal period (e.g. syncopated tracks).
  BeatTracker beat_tracker;
  BeatTracker::Result beats;
  int best_period = tempo.period_frames;
  double best_beat_score = -std::numeric_limits<double>::infinity();
  double primary_norm_score = -std::numeric_limits<double>::infinity();
  constexpr double kPrimaryMargin = 1.05;

  float primary_bpm = tempo.bpm;
  for (int candidate : tempo.candidate_periods) {
    float candidate_bpm = (candidate > 0)
        ? 60.0f * static_cast<float>(mono.sample_rate) /
              static_cast<float>(onset.hop_size) / static_cast<float>(candidate)
        : 0.0f;

    // Only compare candidates within ±30% of the primary estimate to avoid
    // sub-harmonics (2/3, 3/2, half/double tempo) distorting the comparison.
    float ratio = candidate_bpm / primary_bpm;
    if (ratio < 0.7f || ratio > 1.3f) {
      if (options.verbose) {
        std::cout << "  Candidate period=" << candidate
                  << " (" << candidate_bpm << " BPM) — skipped (outside ±30%)\n";
      }
      continue;
    }

    auto candidate_beats = beat_tracker.track(onset.onset_strength,
                                              candidate,
                                              onset.hop_size);
    // Normalize by beat count so faster tempos (more beats) don't
    // accumulate an unfairly higher total score.
    double norm_score = candidate_beats.beat_samples.empty()
        ? 0.0
        : candidate_beats.score / static_cast<double>(candidate_beats.beat_samples.size());
    if (options.verbose) {
      std::cout << "  Candidate period=" << candidate
                << " (" << candidate_bpm << " BPM)"
                << " score=" << candidate_beats.score
                << " beats=" << candidate_beats.beat_samples.size()
                << " norm=" << norm_score << "\n";
    }
    if (candidate == tempo.period_frames) {
      primary_norm_score = norm_score;
    }
    // Require non-primary candidates to exceed the primary's score by a
    // margin — sub-harmonics can achieve slightly inflated per-beat scores
    // due to wider DP search windows.
    double threshold = best_beat_score;
    if (candidate != tempo.period_frames &&
        primary_norm_score > -std::numeric_limits<double>::infinity()) {
      threshold = std::max(threshold, primary_norm_score * kPrimaryMargin);
    }
    if (norm_score > threshold) {
      best_beat_score = norm_score;
      beats = std::move(candidate_beats);
      best_period = candidate;
    }
  }

  // Recompute BPM from the winning period.
  float frame_rate = static_cast<float>(mono.sample_rate) /
                     static_cast<float>(onset.hop_size);
  float final_bpm = (best_period > 0) ? 60.0f * frame_rate / static_cast<float>(best_period) : tempo.bpm;
  if (best_period != tempo.period_frames && options.verbose) {
    std::cout << "Beat-tracker re-estimated tempo: " << tempo.bpm
              << " BPM -> " << final_bpm << " BPM (period " << best_period << ")\n";
  }

  std::cout << "Detected BPM: " << final_bpm << "\n";
  std::cout << "Beat count: " << beats.beat_samples.size() << "\n";

  // Meter detection.
  MeterDetector::Result meter;
  if (options.detect_meter) {
    MeterDetector meter_detector;
    meter = meter_detector.detect(beats.beat_samples,
                                  onset.onset_strength,
                                  onset.hop_size,
                                  mono.sample_rate,
                                  final_bpm,
                                  options.verbose);
    std::cout << "Time signature: " << time_signature_string(meter.time_signature)
              << "\n";
  }

  // Build output paths.
  int bpm_int = static_cast<int>(std::round(final_bpm));
  std::string actual_output = output_path;
  std::string raw_output;

  if (actual_output.empty() && !stereo.title.empty()) {
    std::string base = sanitize_filename(stereo.title);
    actual_output = base + "_" + std::to_string(bpm_int) + "bpm.wav";
    raw_output = base + ".wav";
  } else if (actual_output.empty()) {
    actual_output = "output_click.wav";
  }

  // Save the raw audio (without click track) for YouTube downloads.
  if (!raw_output.empty()) {
    WavWriter::write(raw_output, stereo);
    std::cout << "Audio: " << raw_output << "\n";
  }

  Metronome metronome;
  if (options.detect_meter && !meter.downbeat_samples.empty()) {
    metronome.overlay(stereo, beats.beat_samples, meter.downbeat_samples,
                      options.click_volume, options.click_freq, options.downbeat_freq);
  } else {
    metronome.overlay(stereo, beats.beat_samples, options.click_volume, options.click_freq);
  }

  WavWriter::write(actual_output, stereo);
  std::cout << "Output: " << actual_output << "\n";
}

}  // namespace bpm
