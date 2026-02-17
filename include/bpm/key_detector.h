#pragma once

#include <array>
#include <string>

#include "bpm/audio_buffer.h"

namespace bpm {

class KeyDetector {
 public:
  struct Result {
    std::string key_name;      // "C", "F#", "Bb"
    std::string mode;          // "major" or "minor"
    std::string label;         // "C major", "F# minor"
    std::string short_label;   // "Cmaj", "Fsharpmin" (filename-safe)
    float confidence = 0.0f;   // best_corr - second_best_corr
    float correlation = 0.0f;  // Pearson r of winning key
  };

  Result detect(const AudioBuffer &mono_audio, bool verbose = false) const;

 private:
  static constexpr int kChromaBins = 12;
  static constexpr int kFFTSize = 4096;
  static constexpr int kHopSize = 4096;
  static constexpr float kMinFreqHz = 65.4f;    // C2
  static constexpr float kMaxFreqHz = 2093.0f;  // C7

  std::array<float, kChromaBins> compute_chromagram(
      const AudioBuffer &mono_audio) const;

  static float pearson_correlation(
      const std::array<float, kChromaBins> &x,
      const std::array<float, kChromaBins> &y);
};

}  // namespace bpm
