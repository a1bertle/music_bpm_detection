#pragma once

#include <cstddef>
#include <vector>

#include "bpm/audio_buffer.h"

namespace bpm {

class Metronome {
 public:
  void overlay(AudioBuffer &audio,
               const std::vector<std::size_t> &beat_samples,
               float click_volume = 0.5f,
               float click_freq = 1000.0f) const;

  void overlay(AudioBuffer &audio,
               const std::vector<std::size_t> &beat_samples,
               const std::vector<std::size_t> &downbeat_samples,
               float click_volume,
               float click_freq,
               float downbeat_freq) const;

 private:
  std::vector<float> synth_click(int sample_rate,
                                 float click_volume,
                                 float click_freq,
                                 float duration_sec = 0.02f,
                                 float decay = 200.0f) const;
};

}  // namespace bpm
