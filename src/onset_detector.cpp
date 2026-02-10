#include "bpm/onset_detector.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

extern "C" {
#include "pocketfft.h"
}

namespace bpm {
namespace {

float hz_to_mel(float hz) {
  return 2595.0f * std::log10(1.0f + hz / 700.0f);
}

float mel_to_hz(float mel) {
  return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
}

}  // namespace

std::vector<float> OnsetDetector::hann_window() const {
  std::vector<float> window(static_cast<std::size_t>(fft_size_));
  constexpr float kPi = 3.14159265358979323846f;
  float denom = static_cast<float>(fft_size_ - 1);
  for (int i = 0; i < fft_size_; ++i) {
    window[static_cast<std::size_t>(i)] = 0.5f - 0.5f * std::cos(2.0f * kPi * i / denom);
  }
  return window;
}

std::vector<std::vector<float>> OnsetDetector::mel_filterbank(int sample_rate) const {
  float low_mel = hz_to_mel(30.0f);
  float high_mel = hz_to_mel(8000.0f);
  std::vector<float> mel_points(static_cast<std::size_t>(mel_bands_ + 2));
  for (int i = 0; i < mel_bands_ + 2; ++i) {
    float t = static_cast<float>(i) / static_cast<float>(mel_bands_ + 1);
    mel_points[static_cast<std::size_t>(i)] = low_mel + t * (high_mel - low_mel);
  }

  std::vector<int> bin_points(static_cast<std::size_t>(mel_bands_ + 2));
  for (int i = 0; i < mel_bands_ + 2; ++i) {
    float hz = mel_to_hz(mel_points[static_cast<std::size_t>(i)]);
    int bin = static_cast<int>(std::floor((fft_size_ + 1) * hz / static_cast<float>(sample_rate)));
    bin_points[static_cast<std::size_t>(i)] = std::min(std::max(bin, 0), fft_size_ / 2);
  }

  std::vector<std::vector<float>> filters(static_cast<std::size_t>(mel_bands_),
                                          std::vector<float>(static_cast<std::size_t>(fft_size_ / 2 + 1), 0.0f));
  for (int band = 0; band < mel_bands_; ++band) {
    int left = bin_points[static_cast<std::size_t>(band)];
    int center = bin_points[static_cast<std::size_t>(band + 1)];
    int right = bin_points[static_cast<std::size_t>(band + 2)];
    if (center == left) {
      center = left + 1;
    }
    if (right == center) {
      right = center + 1;
    }
    for (int bin = left; bin < center; ++bin) {
      if (bin >= 0 && bin <= fft_size_ / 2) {
        filters[static_cast<std::size_t>(band)][static_cast<std::size_t>(bin)] =
            (static_cast<float>(bin) - left) / (center - left);
      }
    }
    for (int bin = center; bin < right; ++bin) {
      if (bin >= 0 && bin <= fft_size_ / 2) {
        filters[static_cast<std::size_t>(band)][static_cast<std::size_t>(bin)] =
            (right - static_cast<float>(bin)) / (right - center);
      }
    }
  }

  return filters;
}

OnsetDetector::Result OnsetDetector::compute(const AudioBuffer &mono_audio) const {
  if (mono_audio.channels != 1) {
    throw std::runtime_error("OnsetDetector expects mono audio.");
  }
  if (mono_audio.sample_rate <= 0) {
    throw std::runtime_error("OnsetDetector invalid sample rate.");
  }
  if (mono_audio.samples.empty()) {
    return Result{};
  }

  std::vector<float> window = hann_window();
  auto mel_filters = mel_filterbank(mono_audio.sample_rate);

  std::size_t frames = 0;
  if (mono_audio.samples.size() >= static_cast<std::size_t>(fft_size_)) {
    frames = 1 + (mono_audio.samples.size() - static_cast<std::size_t>(fft_size_)) / static_cast<std::size_t>(hop_size_);
  }

  std::vector<float> onset_strength(frames, 0.0f);
  std::vector<float> prev_mel(static_cast<std::size_t>(mel_bands_), 0.0f);

  if (fft_size_ % 2 != 0) {
    throw std::runtime_error("OnsetDetector requires an even FFT size.");
  }

  rfft_plan plan = make_rfft_plan(static_cast<std::size_t>(fft_size_));
  if (!plan) {
    throw std::runtime_error("Failed to create FFT plan.");
  }

  std::vector<double> frame(fft_size_);
  std::vector<double> power_spectrum(static_cast<std::size_t>(fft_size_ / 2 + 1), 0.0);

  for (std::size_t frame_idx = 0; frame_idx < frames; ++frame_idx) {
    std::size_t offset = frame_idx * static_cast<std::size_t>(hop_size_);
    for (int i = 0; i < fft_size_; ++i) {
      float sample = mono_audio.samples[offset + static_cast<std::size_t>(i)];
      frame[static_cast<std::size_t>(i)] = static_cast<double>(sample * window[static_cast<std::size_t>(i)]);
    }

    if (rfft_forward(plan, frame.data(), 1.0) != 0) {
      destroy_rfft_plan(plan);
      throw std::runtime_error("FFT execution failed.");
    }

    power_spectrum[0] = frame[0] * frame[0];
    power_spectrum[static_cast<std::size_t>(fft_size_ / 2)] = frame[1] * frame[1];
    for (int bin = 1; bin < fft_size_ / 2; ++bin) {
      double re = frame[static_cast<std::size_t>(2 * bin)];
      double im = frame[static_cast<std::size_t>(2 * bin + 1)];
      power_spectrum[static_cast<std::size_t>(bin)] = re * re + im * im;
    }

    std::vector<float> mel_energy(static_cast<std::size_t>(mel_bands_), 0.0f);
    for (int band = 0; band < mel_bands_; ++band) {
      double sum = 0.0;
      const auto &filter = mel_filters[static_cast<std::size_t>(band)];
      for (int bin = 0; bin <= fft_size_ / 2; ++bin) {
        sum += power_spectrum[static_cast<std::size_t>(bin)] * filter[static_cast<std::size_t>(bin)];
      }
      mel_energy[static_cast<std::size_t>(band)] = static_cast<float>(std::log10(sum + 1e-10));
    }

    float flux = 0.0f;
    for (int band = 0; band < mel_bands_; ++band) {
      float diff = mel_energy[static_cast<std::size_t>(band)] - prev_mel[static_cast<std::size_t>(band)];
      if (diff > 0.0f) {
        flux += diff;
      }
    }
    onset_strength[frame_idx] = flux;
    prev_mel = std::move(mel_energy);
  }

  if (!onset_strength.empty()) {
    float mean = std::accumulate(onset_strength.begin(), onset_strength.end(), 0.0f) /
                 static_cast<float>(onset_strength.size());
    float variance = 0.0f;
    for (float value : onset_strength) {
      float diff = value - mean;
      variance += diff * diff;
    }
    variance /= static_cast<float>(onset_strength.size());
    float stddev = std::sqrt(variance);
    if (stddev > 1e-6f) {
      for (float &value : onset_strength) {
        value = (value - mean) / stddev;
      }
    }
  }

  destroy_rfft_plan(plan);

  Result result;
  result.onset_strength = std::move(onset_strength);
  result.hop_size = hop_size_;
  result.fft_size = fft_size_;
  return result;
}

}  // namespace bpm
