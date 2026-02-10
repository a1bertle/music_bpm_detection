#include "bpm/audio_buffer.h"

#include <algorithm>

namespace bpm {

AudioBuffer::AudioBuffer(std::vector<float> data, int rate, int ch)
    : samples(std::move(data)), sample_rate(rate), channels(ch) {}

std::size_t AudioBuffer::num_frames() const {
  if (channels <= 0) {
    return 0;
  }
  return samples.size() / static_cast<std::size_t>(channels);
}

double AudioBuffer::duration_sec() const {
  if (sample_rate <= 0) {
    return 0.0;
  }
  return static_cast<double>(num_frames()) / static_cast<double>(sample_rate);
}

AudioBuffer AudioBuffer::to_mono() const {
  if (channels <= 1) {
    return *this;
  }

  std::size_t frames = num_frames();
  std::vector<float> mono(frames, 0.0f);
  for (std::size_t frame = 0; frame < frames; ++frame) {
    double sum = 0.0;
    std::size_t base = frame * static_cast<std::size_t>(channels);
    for (int ch = 0; ch < channels; ++ch) {
      sum += samples[base + static_cast<std::size_t>(ch)];
    }
    mono[frame] = static_cast<float>(sum / static_cast<double>(channels));
  }

  return AudioBuffer(std::move(mono), sample_rate, 1);
}

}  // namespace bpm
