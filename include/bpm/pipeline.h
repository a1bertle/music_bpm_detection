#pragma once

#include <string>

namespace bpm {

struct PipelineOptions {
  float min_bpm = 50.0f;
  float max_bpm = 220.0f;
  float click_volume = 0.5f;
  float click_freq = 1000.0f;
  float downbeat_freq = 1500.0f;
  bool verbose = false;
  bool detect_meter = true;
};

class Pipeline {
 public:
  void run(const std::string &input_path,
           const std::string &output_path,
           const PipelineOptions &options = PipelineOptions()) const;
};

}  // namespace bpm
