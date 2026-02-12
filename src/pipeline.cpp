#include "bpm/pipeline.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

#include "bpm/beat_tracker.h"
#include "bpm/metronome.h"
#include "bpm/mp3_decoder.h"
#include "bpm/mp4_decoder.h"
#include "bpm/onset_detector.h"
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

}  // namespace

void Pipeline::run(const std::string &input_path,
                   const std::string &output_path,
                   const PipelineOptions &options) const {
  std::string ext = get_extension(input_path);
  AudioBuffer stereo;
  if (ext == ".mp3") {
    stereo = Mp3Decoder::decode(input_path);
  } else if (ext == ".mp4" || ext == ".m4a") {
    stereo = Mp4Decoder::decode(input_path);
  } else {
    throw std::runtime_error("Unsupported file format: " + ext +
                             "\nSupported formats: .mp3, .mp4, .m4a");
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
                                        options.max_bpm);
  std::cout << "Detected BPM: " << tempo.bpm << "\n";

  BeatTracker beat_tracker;
  auto beats = beat_tracker.track(onset.onset_strength,
                                  tempo.period_frames,
                                  onset.hop_size);
  std::cout << "Beat count: " << beats.beat_samples.size() << "\n";

  Metronome metronome;
  metronome.overlay(stereo, beats.beat_samples, options.click_volume, options.click_freq);

  WavWriter::write(output_path, stereo);
  if (options.verbose) {
    std::cout << "Wrote output WAV: " << output_path << "\n";
  }
}

}  // namespace bpm
