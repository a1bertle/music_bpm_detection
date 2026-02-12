#pragma once

#include <string>

#include "bpm/audio_buffer.h"

namespace bpm {

class WavReader {
 public:
  static AudioBuffer read(const std::string &filepath);
};

}  // namespace bpm
