#pragma once

#include <string>

#include "bpm/audio_buffer.h"

namespace bpm {

class Mp4Decoder {
 public:
  static AudioBuffer decode(const std::string &filepath);
};

}  // namespace bpm
