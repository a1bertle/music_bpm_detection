#include "bpm/wav_reader.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace bpm {
namespace {

std::uint16_t read_u16(std::ifstream &in) {
  std::uint8_t buf[2];
  in.read(reinterpret_cast<char *>(buf), 2);
  return static_cast<std::uint16_t>(buf[0]) |
         (static_cast<std::uint16_t>(buf[1]) << 8);
}

std::uint32_t read_u32(std::ifstream &in) {
  std::uint8_t buf[4];
  in.read(reinterpret_cast<char *>(buf), 4);
  return static_cast<std::uint32_t>(buf[0]) |
         (static_cast<std::uint32_t>(buf[1]) << 8) |
         (static_cast<std::uint32_t>(buf[2]) << 16) |
         (static_cast<std::uint32_t>(buf[3]) << 24);
}

void expect_tag(std::ifstream &in, const char *expected) {
  char tag[4];
  in.read(tag, 4);
  if (std::memcmp(tag, expected, 4) != 0) {
    throw std::runtime_error(
        std::string("WAV parse error: expected '") + expected + "' tag.");
  }
}

}  // namespace

AudioBuffer WavReader::read(const std::string &filepath) {
  std::ifstream in(filepath, std::ios::binary);
  if (!in) {
    throw std::runtime_error("Failed to open WAV file: " + filepath);
  }

  // RIFF header.
  expect_tag(in, "RIFF");
  read_u32(in);  // chunk size (ignored)
  expect_tag(in, "WAVE");

  // fmt sub-chunk.
  expect_tag(in, "fmt ");
  std::uint32_t fmt_size = read_u32(in);
  std::uint16_t audio_format = read_u16(in);
  std::uint16_t channels = read_u16(in);
  std::uint32_t sample_rate = read_u32(in);
  read_u32(in);  // byte rate (ignored)
  read_u16(in);  // block align (ignored)
  std::uint16_t bits_per_sample = read_u16(in);

  // Skip any extra fmt bytes.
  if (fmt_size > 16) {
    in.seekg(static_cast<std::streamoff>(fmt_size - 16), std::ios::cur);
  }

  if (audio_format != 1) {
    throw std::runtime_error("WAV file is not PCM format.");
  }
  if (bits_per_sample != 16) {
    throw std::runtime_error("WAV file is not 16-bit.");
  }

  // Find the data sub-chunk (skip any non-data chunks like LIST/INFO).
  char chunk_id[4];
  std::uint32_t data_size = 0;
  while (in.read(chunk_id, 4)) {
    data_size = read_u32(in);
    if (std::memcmp(chunk_id, "data", 4) == 0) {
      break;
    }
    // Skip unknown chunk.
    in.seekg(static_cast<std::streamoff>(data_size), std::ios::cur);
  }

  if (std::memcmp(chunk_id, "data", 4) != 0) {
    throw std::runtime_error("WAV file has no data chunk.");
  }

  std::uint32_t num_samples = data_size / sizeof(std::int16_t);
  std::vector<std::int16_t> raw(num_samples);
  in.read(reinterpret_cast<char *>(raw.data()),
          static_cast<std::streamsize>(data_size));

  if (!in) {
    throw std::runtime_error("Failed reading WAV sample data: " + filepath);
  }

  std::vector<float> samples(num_samples);
  for (std::uint32_t i = 0; i < num_samples; ++i) {
    samples[i] = static_cast<float>(raw[i]) / 32768.0f;
  }

  return AudioBuffer(std::move(samples), static_cast<int>(sample_rate),
                     static_cast<int>(channels));
}

}  // namespace bpm
