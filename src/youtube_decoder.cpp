#include "bpm/youtube_decoder.h"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include "bpm/wav_reader.h"

namespace bpm {

namespace {

std::string get_video_title(const std::string &url) {
  std::string command = "yt-dlp --get-title --no-playlist \"" + url + "\" 2>/dev/null";
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe) {
    return "";
  }
  char buffer[512];
  std::string title;
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    title += buffer;
  }
  pclose(pipe);
  // Trim trailing newline.
  while (!title.empty() && (title.back() == '\n' || title.back() == '\r')) {
    title.pop_back();
  }
  return title;
}

}  // namespace

AudioBuffer YoutubeDecoder::decode(const std::string &url) {
  std::string temp_dl = "/tmp/bpm_yt_download";
  std::string temp_wav = "/tmp/bpm_yt_audio.wav";

  // Fetch video title for output naming.
  std::string video_title = get_video_title(url);

  // Download best audio stream with yt-dlp.
  std::string dl_command = "yt-dlp -f \"bestaudio\" --no-playlist -o \"" +
                           temp_dl + "\" \"" + url + "\" 2>/dev/null";

  int ret = std::system(dl_command.c_str());
  if (ret != 0) {
    std::remove(temp_dl.c_str());
    throw std::runtime_error(
        "yt-dlp failed to download audio from: " + url +
        "\nEnsure yt-dlp is installed and the URL is valid.");
  }

  // Convert downloaded audio to WAV via ffmpeg.
  std::string ffmpeg_command = "ffmpeg -y -i \"" + temp_dl +
                               "\" -vn -acodec pcm_s16le -ar 44100 -ac 2 \"" +
                               temp_wav + "\" 2>/dev/null";

  ret = std::system(ffmpeg_command.c_str());
  std::remove(temp_dl.c_str());
  if (ret != 0) {
    std::remove(temp_wav.c_str());
    throw std::runtime_error(
        "ffmpeg failed to convert downloaded audio.\n"
        "Ensure ffmpeg is installed.");
  }

  AudioBuffer audio = WavReader::read(temp_wav);
  std::remove(temp_wav.c_str());
  audio.title = video_title;
  return audio;
}

}  // namespace bpm
