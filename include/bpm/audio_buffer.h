#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace bpm {

struct AudioBuffer {
  std::vector<float> samples;
  int sample_rate = 0;
  int channels = 0;
  std::string title;

  AudioBuffer() = default;
  AudioBuffer(std::vector<float> data, int rate, int ch);

  std::size_t num_frames() const;
  double duration_sec() const;
  AudioBuffer to_mono() const;
};

}  // namespace bpm
