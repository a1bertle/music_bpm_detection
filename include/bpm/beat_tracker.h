#pragma once

#include <cstddef>
#include <vector>

namespace bpm {

class BeatTracker {
 public:
  struct Result {
    std::vector<std::size_t> beat_samples;
  };

  Result track(const std::vector<float> &onset_strength,
               int period_frames,
               int hop_size,
               float alpha = 680.0f) const;
};

}  // namespace bpm
