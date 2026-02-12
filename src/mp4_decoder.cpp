#include "bpm/mp4_decoder.h"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "bpm/wav_reader.h"

namespace bpm {

AudioBuffer Mp4Decoder::decode(const std::string &filepath) {
  std::string temp_path = filepath + ".tmp.wav";

  std::string command = "ffmpeg -y -i \"" + filepath +
                        "\" -vn -acodec pcm_s16le -ar 44100 -ac 2 \"" +
                        temp_path + "\" 2>/dev/null";

  int ret = std::system(command.c_str());
  if (ret != 0) {
    std::remove(temp_path.c_str());
    throw std::runtime_error(
        "ffmpeg failed to extract audio from: " + filepath +
        "\nEnsure ffmpeg is installed and the file contains an audio track.");
  }

  AudioBuffer audio = WavReader::read(temp_path);
  std::remove(temp_path.c_str());
  return audio;
}

}  // namespace bpm
