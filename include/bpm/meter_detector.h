#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace bpm {

enum class TimeSignature {
  TWO_FOUR,    // 2/4
  THREE_FOUR,  // 3/4
  FOUR_FOUR,   // 4/4
  SIX_EIGHT    // 6/8
};

std::string time_signature_string(TimeSignature ts);

class MeterDetector {
 public:
  struct Result {
    TimeSignature time_signature = TimeSignature::FOUR_FOUR;
    int beats_per_measure = 4;
    int downbeat_phase = 0;
    float confidence = 0.0f;
    std::vector<std::size_t> downbeat_samples;
  };

  Result detect(const std::vector<std::size_t> &beat_samples,
                const std::vector<float> &onset_strength,
                int hop_size,
                int sample_rate,
                float bpm,
                bool verbose = false) const;

 private:
  float accent_score(const std::vector<float> &onset_at_beat,
                     int grouping, int phase) const;

  float beat_autocorrelation(const std::vector<float> &onset_at_beat,
                             int lag) const;

  bool check_compound_subdivision(
      const std::vector<std::size_t> &beat_samples,
      const std::vector<float> &onset_strength,
      int hop_size,
      bool verbose = false) const;

  std::vector<std::size_t> extract_downbeats(
      const std::vector<std::size_t> &beat_samples,
      int grouping, int phase) const;
};

}  // namespace bpm
