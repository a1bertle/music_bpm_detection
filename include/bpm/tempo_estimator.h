#pragma once

#include <vector>

namespace bpm {

class TempoEstimator {
 public:
  struct Result {
    float bpm = 0.0f;
    int period_frames = 0;
  };

  Result estimate(const std::vector<float> &onset_strength,
                  int sample_rate,
                  int hop_size,
                  float min_bpm = 50.0f,
                  float max_bpm = 220.0f,
                  bool verbose = false) const;
};

}  // namespace bpm
