#pragma once

#include <string>

#include "bpm/audio_buffer.h"

namespace bpm {

class WavWriter {
 public:
  static void write(const std::string &filepath, const AudioBuffer &audio);
};

}  // namespace bpm
