#include "bpm/key_detector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

extern "C" {
#include "pocketfft.h"
}

namespace bpm {
namespace {

// Krumhansl-Kessler key profiles (Krumhansl 1990).
// Index 0 = tonic, 1 = minor 2nd, ..., 11 = major 7th.
constexpr std::array<float, 12> kMajorProfile = {
    6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f,
    2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f};

constexpr std::array<float, 12> kMinorProfile = {
    6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f,
    2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f};

// Display names for the 12 pitch classes.
constexpr const char *kKeyNames[12] = {
    "C", "C#", "D", "Eb", "E", "F",
    "F#", "G", "Ab", "A", "Bb", "B"};

// Filename-safe names (no '#' character).
constexpr const char *kKeyFileNames[12] = {
    "C", "Csharp", "D", "Eb", "E", "F",
    "Fsharp", "G", "Ab", "A", "Bb", "B"};

// Reference frequency for pitch class mapping: C0 in Hz.
constexpr float kC0Hz = 16.3516f;

}  // namespace

std::array<float, KeyDetector::kChromaBins>
KeyDetector::compute_chromagram(const AudioBuffer &mono_audio) const {
  std::array<float, kChromaBins> chroma = {};

  if (mono_audio.samples.size() < static_cast<std::size_t>(kFFTSize)) {
    return chroma;
  }

  // Hann window.
  constexpr float kPi = 3.14159265358979323846f;
  std::vector<float> window(static_cast<std::size_t>(kFFTSize));
  float denom = static_cast<float>(kFFTSize - 1);
  for (int i = 0; i < kFFTSize; ++i) {
    window[static_cast<std::size_t>(i)] =
        0.5f - 0.5f * std::cos(2.0f * kPi * static_cast<float>(i) / denom);
  }

  // Pre-compute interpolated bin-to-chroma mapping with octave index.
  // Each bin distributes energy between the two nearest pitch classes
  // proportionally to distance, avoiding systematic bias at low frequencies
  // where FFT bin spacing exceeds semitone spacing.
  int num_bins = kFFTSize / 2 + 1;
  float sr = static_cast<float>(mono_audio.sample_rate);

  // Determine octave range.
  float min_pitch = 12.0f * std::log2(kMinFreqHz / kC0Hz);
  int min_octave = static_cast<int>(std::floor(min_pitch / 12.0f));
  float max_pitch = 12.0f * std::log2(kMaxFreqHz / kC0Hz);
  int max_octave = static_cast<int>(std::floor(max_pitch / 12.0f));
  int n_octaves = max_octave - min_octave + 1;

  struct BinMapping {
    int chroma_lo = -1;
    int chroma_hi = -1;
    float weight_hi = 0.0f;
    int octave = -1;  // 0-based index into per-octave chroma
  };
  std::vector<BinMapping> bin_map(static_cast<std::size_t>(num_bins));

  for (int k = 1; k < num_bins; ++k) {
    float freq = static_cast<float>(k) * sr / static_cast<float>(kFFTSize);
    if (freq < kMinFreqHz || freq > kMaxFreqHz) {
      continue;
    }
    float pitch = 12.0f * std::log2(freq / kC0Hz);
    float pitch_floor = std::floor(pitch);
    float frac = pitch - pitch_floor;
    int pc_lo = static_cast<int>(pitch_floor) % 12;
    if (pc_lo < 0) {
      pc_lo += 12;
    }
    int pc_hi = (pc_lo + 1) % 12;
    int octave = static_cast<int>(std::floor(pitch / 12.0f)) - min_octave;
    octave = std::max(0, std::min(octave, n_octaves - 1));

    auto &m = bin_map[static_cast<std::size_t>(k)];
    m.chroma_lo = pc_lo;
    m.chroma_hi = pc_hi;
    m.weight_hi = frac;
    m.octave = octave;
  }

  // Per-octave chroma accumulators.
  std::vector<std::array<float, kChromaBins>> octave_chroma(
      static_cast<std::size_t>(n_octaves));
  for (auto &oc : octave_chroma) {
    oc = {};
  }

  // Create FFT plan.
  rfft_plan plan = make_rfft_plan(static_cast<std::size_t>(kFFTSize));
  if (!plan) {
    throw std::runtime_error("Failed to create FFT plan for key detection.");
  }

  std::size_t num_frames =
      1 + (mono_audio.samples.size() - static_cast<std::size_t>(kFFTSize)) /
              static_cast<std::size_t>(kHopSize);

  std::vector<double> frame(static_cast<std::size_t>(kFFTSize));

  for (std::size_t fi = 0; fi < num_frames; ++fi) {
    std::size_t offset = fi * static_cast<std::size_t>(kHopSize);

    // Apply Hann window.
    for (int i = 0; i < kFFTSize; ++i) {
      frame[static_cast<std::size_t>(i)] = static_cast<double>(
          mono_audio.samples[offset + static_cast<std::size_t>(i)] *
          window[static_cast<std::size_t>(i)]);
    }

    if (rfft_forward(plan, frame.data(), 1.0) != 0) {
      destroy_rfft_plan(plan);
      throw std::runtime_error("FFT execution failed in key detection.");
    }

    // pocketfft halfcomplex format:
    //   frame[0] = DC (real), frame[1] = Nyquist (real)
    //   bin k (1..N/2-1): real = frame[2k], imag = frame[2k+1]

    // Interior bins: magnitude = sqrt(re^2 + im^2), interpolated across
    // the two nearest pitch classes, accumulated per octave.
    for (int k = 1; k < kFFTSize / 2; ++k) {
      const auto &m = bin_map[static_cast<std::size_t>(k)];
      if (m.chroma_lo < 0) {
        continue;
      }
      double re = frame[static_cast<std::size_t>(2 * k)];
      double im = frame[static_cast<std::size_t>(2 * k + 1)];
      float power = static_cast<float>(re * re + im * im);
      auto &oc = octave_chroma[static_cast<std::size_t>(m.octave)];
      oc[static_cast<std::size_t>(m.chroma_lo)] += power * (1.0f - m.weight_hi);
      oc[static_cast<std::size_t>(m.chroma_hi)] += power * m.weight_hi;
    }
  }

  destroy_rfft_plan(plan);

  // Normalize each octave independently, then average.
  // This prevents harmonics in upper octaves from dominating the chroma.
  int contributing_octaves = 0;
  for (int oct = 0; oct < n_octaves; ++oct) {
    auto &oc = octave_chroma[static_cast<std::size_t>(oct)];
    float total = 0.0f;
    for (float v : oc) {
      total += v;
    }
    if (total < 1e-12f) {
      continue;
    }
    for (float &v : oc) {
      v /= total;
    }
    for (int i = 0; i < kChromaBins; ++i) {
      chroma[static_cast<std::size_t>(i)] +=
          oc[static_cast<std::size_t>(i)];
    }
    ++contributing_octaves;
  }

  // Average across octaves.
  if (contributing_octaves > 0) {
    float scale = 1.0f / static_cast<float>(contributing_octaves);
    for (float &v : chroma) {
      v *= scale;
    }
  }

  return chroma;
}

float KeyDetector::pearson_correlation(
    const std::array<float, kChromaBins> &x,
    const std::array<float, kChromaBins> &y) {
  float mean_x = 0.0f, mean_y = 0.0f;
  for (int i = 0; i < kChromaBins; ++i) {
    mean_x += x[static_cast<std::size_t>(i)];
    mean_y += y[static_cast<std::size_t>(i)];
  }
  mean_x /= static_cast<float>(kChromaBins);
  mean_y /= static_cast<float>(kChromaBins);

  float num = 0.0f, den_x = 0.0f, den_y = 0.0f;
  for (int i = 0; i < kChromaBins; ++i) {
    float dx = x[static_cast<std::size_t>(i)] - mean_x;
    float dy = y[static_cast<std::size_t>(i)] - mean_y;
    num += dx * dy;
    den_x += dx * dx;
    den_y += dy * dy;
  }

  float den = std::sqrt(den_x * den_y);
  if (den < 1e-12f) {
    return 0.0f;
  }
  return num / den;
}

KeyDetector::Result KeyDetector::detect(const AudioBuffer &mono_audio,
                                        bool verbose) const {
  if (mono_audio.channels != 1) {
    throw std::runtime_error("KeyDetector expects mono audio.");
  }
  if (mono_audio.sample_rate <= 0) {
    throw std::runtime_error("KeyDetector invalid sample rate.");
  }

  auto chroma = compute_chromagram(mono_audio);

  if (verbose) {
    std::cout << "Chroma distribution:";
    for (int i = 0; i < kChromaBins; ++i) {
      std::cout << " " << kKeyNames[i] << "="
                << chroma[static_cast<std::size_t>(i)];
    }
    std::cout << "\n";
  }

  float best_corr = -2.0f;
  float second_best_corr = -2.0f;
  int best_root = 0;
  bool best_is_major = true;

  for (int root = 0; root < 12; ++root) {
    // Rotate profile so the tonic aligns with pitch class `root`.
    std::array<float, 12> rotated_major;
    std::array<float, 12> rotated_minor;
    for (int i = 0; i < 12; ++i) {
      rotated_major[static_cast<std::size_t>(i)] =
          kMajorProfile[static_cast<std::size_t>((i - root + 12) % 12)];
      rotated_minor[static_cast<std::size_t>(i)] =
          kMinorProfile[static_cast<std::size_t>((i - root + 12) % 12)];
    }

    float corr_major = pearson_correlation(chroma, rotated_major);
    float corr_minor = pearson_correlation(chroma, rotated_minor);

    if (verbose) {
      std::cout << "  " << kKeyNames[root] << " major: r=" << corr_major
                << "  " << kKeyNames[root] << " minor: r=" << corr_minor
                << "\n";
    }

    if (corr_major > best_corr) {
      second_best_corr = best_corr;
      best_corr = corr_major;
      best_root = root;
      best_is_major = true;
    } else if (corr_major > second_best_corr) {
      second_best_corr = corr_major;
    }

    if (corr_minor > best_corr) {
      second_best_corr = best_corr;
      best_corr = corr_minor;
      best_root = root;
      best_is_major = false;
    } else if (corr_minor > second_best_corr) {
      second_best_corr = corr_minor;
    }
  }

  Result result;
  result.key_name = kKeyNames[best_root];
  result.mode = best_is_major ? "major" : "minor";
  result.label = std::string(kKeyNames[best_root]) + " " + result.mode;
  result.short_label = std::string(kKeyFileNames[best_root]) +
                       (best_is_major ? "maj" : "min");
  result.correlation = best_corr;
  result.confidence = best_corr - second_best_corr;

  if (verbose) {
    std::cout << "Key detection: " << result.label
              << " (r=" << result.correlation
              << ", confidence=" << result.confidence << ")\n";
  }

  return result;
}

}  // namespace bpm
