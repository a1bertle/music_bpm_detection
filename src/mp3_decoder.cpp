#define MINIMP3_IMPLEMENTATION
#define MINIMP3_EX_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT

#include "minimp3_ex.h"

#include "bpm/mp3_decoder.h"

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace bpm {

AudioBuffer Mp3Decoder::decode(const std::string &filepath) {
  mp3dec_t dec;
  mp3dec_file_info_t info;
  std::memset(&info, 0, sizeof(info));

  if (mp3dec_load(&dec, filepath.c_str(), &info, nullptr, nullptr) != 0) {
    throw std::runtime_error("Failed to decode MP3: " + filepath);
  }

  if (info.samples <= 0 || info.hz <= 0 || info.channels <= 0 || !info.buffer) {
    if (info.buffer) {
      std::free(info.buffer);
    }
    throw std::runtime_error("Decoded MP3 contained no samples: " + filepath);
  }

  std::vector<float> samples(info.buffer, info.buffer + info.samples);
  std::free(info.buffer);

  return AudioBuffer(std::move(samples), info.hz, info.channels);
}

}  // namespace bpm
