#pragma once

#include <vector>

#include "bpm/audio_buffer.h"

namespace bpm {

class OnsetDetector {
 public:
  struct Result {
    std::vector<float> onset_strength;
    int hop_size = 0;
    int fft_size = 0;
  };

  Result compute(const AudioBuffer &mono_audio) const;

 private:
  int fft_size_ = 2048;
  int hop_size_ = 512;
  int mel_bands_ = 40;

  std::vector<float> hann_window() const;
  std::vector<std::vector<float>> mel_filterbank(int sample_rate) const;
};

}  // namespace bpm
