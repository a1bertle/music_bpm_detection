#include <iostream>
#include <string>

#include "bpm/pipeline.h"

namespace {

void print_help() {
  std::cout << "Usage: bpm_detect [options] <input>\n"
            << "\nSupported inputs: MP3, MP4, M4A, YouTube URL\n"
            << "  MP4/M4A require ffmpeg. YouTube requires yt-dlp and ffmpeg.\n\n"
            << "  -o, --output <path>     Output WAV path (default: <input>_click.wav)\n"
            << "  -v, --verbose           Print detailed info\n"
            << "  --min-bpm <float>       Min BPM (default: 50)\n"
            << "  --max-bpm <float>       Max BPM (default: 220)\n"
            << "  --click-volume <float>  Click volume 0.0-1.0 (default: 0.5)\n"
            << "  --click-freq <float>    Click frequency Hz (default: 1000)\n"
            << "  --downbeat-freq <float> Downbeat click frequency Hz (default: 1500)\n"
            << "  --accent-downbeats      Use higher-pitched click on downbeats\n"
            << "  --no-key                Disable key signature detection\n"
            << "  -h, --help              Show help\n";
}

bool parse_arg(int argc, char **argv, int &index, std::string &value) {
  if (index + 1 >= argc) {
    return false;
  }
  value = argv[index + 1];
  index += 1;
  return true;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    print_help();
    return 1;
  }

  bpm::PipelineOptions options;
  std::string input_path;
  std::string output_path;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      print_help();
      return 0;
    }
    if (arg == "-v" || arg == "--verbose") {
      options.verbose = true;
      continue;
    }
    if (arg == "-o" || arg == "--output") {
      if (!parse_arg(argc, argv, i, output_path)) {
        std::cerr << "Missing value for output path.\n";
        return 1;
      }
      continue;
    }
    if (arg == "--min-bpm") {
      std::string value;
      if (!parse_arg(argc, argv, i, value)) {
        std::cerr << "Missing value for min BPM.\n";
        return 1;
      }
      options.min_bpm = std::stof(value);
      continue;
    }
    if (arg == "--max-bpm") {
      std::string value;
      if (!parse_arg(argc, argv, i, value)) {
        std::cerr << "Missing value for max BPM.\n";
        return 1;
      }
      options.max_bpm = std::stof(value);
      continue;
    }
    if (arg == "--click-volume") {
      std::string value;
      if (!parse_arg(argc, argv, i, value)) {
        std::cerr << "Missing value for click volume.\n";
        return 1;
      }
      options.click_volume = std::stof(value);
      continue;
    }
    if (arg == "--click-freq") {
      std::string value;
      if (!parse_arg(argc, argv, i, value)) {
        std::cerr << "Missing value for click frequency.\n";
        return 1;
      }
      options.click_freq = std::stof(value);
      continue;
    }
    if (arg == "--downbeat-freq") {
      std::string value;
      if (!parse_arg(argc, argv, i, value)) {
        std::cerr << "Missing value for downbeat frequency.\n";
        return 1;
      }
      options.downbeat_freq = std::stof(value);
      continue;
    }
    if (arg == "--accent-downbeats") {
      options.accent_downbeats = true;
      continue;
    }
    if (arg == "--no-key") {
      options.detect_key = false;
      continue;
    }

    if (!arg.empty() && arg[0] == '-') {
      std::cerr << "Unknown option: " << arg << "\n";
      return 1;
    }

    input_path = arg;
  }

  if (input_path.empty()) {
    std::cerr << "No input file provided.\n";
    print_help();
    return 1;
  }

  if (output_path.empty() && input_path.find("://") == std::string::npos) {
    output_path = input_path + "_click.wav";
  }

  try {
    bpm::Pipeline pipeline;
    pipeline.run(input_path, output_path, options);
  } catch (const std::exception &ex) {
    std::cerr << "Error: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}
