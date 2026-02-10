#include "bpm/metronome.h"

#include <algorithm>
#include <cmath>

namespace bpm {

std::vector<float> Metronome::synth_click(int sample_rate,
                                          float click_volume,
                                          float click_freq,
                                          float duration_sec,
                                          float decay) const {
  if (sample_rate <= 0 || duration_sec <= 0.0f) {
    return {};
  }

  int length = std::max(1, static_cast<int>(std::round(duration_sec * sample_rate)));
  std::vector<float> click(static_cast<std::size_t>(length), 0.0f);
  constexpr float kPi = 3.14159265358979323846f;

  for (int i = 0; i < length; ++i) {
    float t = static_cast<float>(i) / static_cast<float>(sample_rate);
    float env = std::exp(-decay * t);
    click[static_cast<std::size_t>(i)] = click_volume * std::sin(2.0f * kPi * click_freq * t) * env;
  }

  return click;
}

void Metronome::overlay(AudioBuffer &audio,
                        const std::vector<std::size_t> &beat_samples,
                        float click_volume,
                        float click_freq) const {
  if (audio.sample_rate <= 0 || audio.channels <= 0 || audio.samples.empty()) {
    return;
  }
  if (beat_samples.empty()) {
    return;
  }

  std::vector<float> click = synth_click(audio.sample_rate, click_volume, click_freq);
  if (click.empty()) {
    return;
  }

  std::size_t frames = audio.num_frames();
  std::size_t channels = static_cast<std::size_t>(audio.channels);

  for (std::size_t beat : beat_samples) {
    if (beat >= frames) {
      continue;
    }
    for (std::size_t i = 0; i < click.size(); ++i) {
      std::size_t frame = beat + i;
      if (frame >= frames) {
        break;
      }
      for (std::size_t ch = 0; ch < channels; ++ch) {
        std::size_t index = frame * channels + ch;
        audio.samples[index] += click[i];
      }
    }
  }

  for (float &sample : audio.samples) {
    sample = std::max(-1.0f, std::min(1.0f, sample));
  }
}

}  // namespace bpm
