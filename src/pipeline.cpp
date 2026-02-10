#include "bpm/pipeline.h"

#include <iostream>

#include "bpm/beat_tracker.h"
#include "bpm/metronome.h"
#include "bpm/mp3_decoder.h"
#include "bpm/onset_detector.h"
#include "bpm/tempo_estimator.h"
#include "bpm/wav_writer.h"

namespace bpm {

void Pipeline::run(const std::string &input_path,
                   const std::string &output_path,
                   const PipelineOptions &options) const {
  AudioBuffer stereo = Mp3Decoder::decode(input_path);
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
