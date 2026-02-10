#include "bpm/wav_writer.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace bpm {
namespace {

void write_u16(std::ofstream &out, std::uint16_t value) {
  out.put(static_cast<char>(value & 0xFF));
  out.put(static_cast<char>((value >> 8) & 0xFF));
}

void write_u32(std::ofstream &out, std::uint32_t value) {
  out.put(static_cast<char>(value & 0xFF));
  out.put(static_cast<char>((value >> 8) & 0xFF));
  out.put(static_cast<char>((value >> 16) & 0xFF));
  out.put(static_cast<char>((value >> 24) & 0xFF));
}

}  // namespace

void WavWriter::write(const std::string &filepath, const AudioBuffer &audio) {
  if (audio.sample_rate <= 0 || audio.channels <= 0) {
    throw std::runtime_error("Invalid audio buffer for WAV output.");
  }

  std::ofstream out(filepath, std::ios::binary);
  if (!out) {
    throw std::runtime_error("Failed to open output WAV: " + filepath);
  }

  std::uint32_t num_samples = static_cast<std::uint32_t>(audio.samples.size());
  std::uint32_t data_bytes = num_samples * sizeof(std::int16_t);
  std::uint16_t channels = static_cast<std::uint16_t>(audio.channels);
  std::uint32_t sample_rate = static_cast<std::uint32_t>(audio.sample_rate);
  std::uint16_t bits_per_sample = 16;
  std::uint16_t block_align = channels * (bits_per_sample / 8);
  std::uint32_t byte_rate = sample_rate * block_align;

  out.write("RIFF", 4);
  write_u32(out, 36 + data_bytes);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  write_u32(out, 16);
  write_u16(out, 1);
  write_u16(out, channels);
  write_u32(out, sample_rate);
  write_u32(out, byte_rate);
  write_u16(out, block_align);
  write_u16(out, bits_per_sample);
  out.write("data", 4);
  write_u32(out, data_bytes);

  for (float sample : audio.samples) {
    float clamped = std::max(-1.0f, std::min(1.0f, sample));
    std::int16_t value = static_cast<std::int16_t>(clamped * 32767.0f);
    write_u16(out, static_cast<std::uint16_t>(value));
  }

  if (!out) {
    throw std::runtime_error("Failed while writing WAV: " + filepath);
  }
}

}  // namespace bpm
