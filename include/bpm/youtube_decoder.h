#pragma once

#include <string>

#include "bpm/audio_buffer.h"

namespace bpm {

class YoutubeDecoder {
 public:
  static AudioBuffer decode(const std::string &url);
};

}  // namespace bpm
