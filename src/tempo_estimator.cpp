#include "bpm/tempo_estimator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace bpm {
namespace {

float bpm_from_lag(int lag, float frame_rate) {
  if (lag <= 0 || frame_rate <= 0.0f) {
    return 0.0f;
  }
  return 60.0f * frame_rate / static_cast<float>(lag);
}

float bpm_from_lag_f(double lag, float frame_rate) {
  if (lag <= 0.0 || frame_rate <= 0.0f) {
    return 0.0f;
  }
  return static_cast<float>(60.0 * static_cast<double>(frame_rate) / lag);
}

// Parabolic interpolation around a peak at index `peak` in `data`.
// Returns the fractional offset from `peak` where the true maximum lies.
double parabolic_interpolate(const std::vector<double> &data, int peak, int lo, int hi) {
  if (peak <= lo || peak >= hi) {
    return static_cast<double>(peak);
  }
  double a = data[static_cast<std::size_t>(peak - 1)];
  double b = data[static_cast<std::size_t>(peak)];
  double c = data[static_cast<std::size_t>(peak + 1)];
  double denom = a - 2.0 * b + c;
  if (std::abs(denom) < 1e-12) {
    return static_cast<double>(peak);
  }
  double delta = 0.5 * (a - c) / denom;
  return static_cast<double>(peak) + delta;
}

}  // namespace

TempoEstimator::Result TempoEstimator::estimate(const std::vector<float> &onset_strength,
                                                int sample_rate,
                                                int hop_size,
                                                float min_bpm,
                                                float max_bpm) const {
  if (sample_rate <= 0 || hop_size <= 0) {
    throw std::runtime_error("TempoEstimator invalid sample rate or hop size.");
  }
  if (onset_strength.size() < 2) {
    return Result{};
  }

  float frame_rate = static_cast<float>(sample_rate) / static_cast<float>(hop_size);
  min_bpm = std::max(min_bpm, 1.0f);
  max_bpm = std::max(max_bpm, min_bpm + 1.0f);

  int max_lag = static_cast<int>(std::floor(60.0f * frame_rate / min_bpm));
  int min_lag = static_cast<int>(std::ceil(60.0f * frame_rate / max_bpm));
  min_lag = std::max(min_lag, 1);
  max_lag = std::min(max_lag, static_cast<int>(onset_strength.size() - 1));

  if (max_lag <= min_lag) {
    return Result{};
  }

  // Compute normalized autocorrelation for each candidate lag.
  std::vector<double> autocorr(static_cast<std::size_t>(max_lag + 1), 0.0);
  for (int lag = min_lag; lag <= max_lag; ++lag) {
    double sum = 0.0;
    std::size_t count = onset_strength.size() - static_cast<std::size_t>(lag);
    for (std::size_t i = static_cast<std::size_t>(lag); i < onset_strength.size(); ++i) {
      sum += static_cast<double>(onset_strength[i]) * onset_strength[i - static_cast<std::size_t>(lag)];
    }
    autocorr[static_cast<std::size_t>(lag)] = (count > 0) ? sum / static_cast<double>(count) : 0.0;
  }

  // Apply log-Gaussian tempo prior and find best lag.
  int best_lag = min_lag;
  double best_score = -std::numeric_limits<double>::infinity();
  constexpr double kPriorCenter = 120.0;
  constexpr double kSigma = 1.0;

  std::vector<double> weighted(autocorr.size(), 0.0);
  for (int lag = min_lag; lag <= max_lag; ++lag) {
    float bpm = bpm_from_lag(lag, frame_rate);
    if (bpm <= 0.0f) {
      continue;
    }
    double log_ratio = std::log2(static_cast<double>(bpm) / kPriorCenter);
    double prior = std::exp(-0.5 * (log_ratio * log_ratio) / (kSigma * kSigma));
    weighted[static_cast<std::size_t>(lag)] = autocorr[static_cast<std::size_t>(lag)] * prior;
    if (weighted[static_cast<std::size_t>(lag)] > best_score) {
      best_score = weighted[static_cast<std::size_t>(lag)];
      best_lag = lag;
    }
  }

  // Compute median weighted score as a noise floor estimate.
  double median_weighted = 0.0;
  {
    std::vector<double> sorted_scores;
    sorted_scores.reserve(static_cast<std::size_t>(max_lag - min_lag + 1));
    for (int lag = min_lag; lag <= max_lag; ++lag) {
      sorted_scores.push_back(weighted[static_cast<std::size_t>(lag)]);
    }
    std::sort(sorted_scores.begin(), sorted_scores.end());
    if (!sorted_scores.empty()) {
      median_weighted = sorted_scores[sorted_scores.size() / 2];
    }
  }

  // Octave correction: iteratively halve the lag to prefer the fastest tempo
  // whose autocorrelation peak is genuine (above the median noise floor).
  // Sub-harmonics (2T, 3T, ...) always produce strong autocorrelation peaks.
  for (;;) {
    int half_center = best_lag / 2;
    int search_lo = std::max(min_lag, half_center - 2);
    int search_hi = std::min(max_lag, half_center + 2);

    int best_half = -1;
    double best_half_score = -std::numeric_limits<double>::infinity();
    for (int lag = search_lo; lag <= search_hi; ++lag) {
      if (weighted[static_cast<std::size_t>(lag)] > best_half_score) {
        best_half_score = weighted[static_cast<std::size_t>(lag)];
        best_half = lag;
      }
    }

    if (best_half < min_lag) {
      break;
    }
    if (best_half_score > median_weighted) {
      best_lag = best_half;
    } else {
      break;
    }
  }

  // Parabolic interpolation for sub-lag BPM precision.
  double refined_lag = parabolic_interpolate(autocorr, best_lag, min_lag, max_lag);

  Result result;
  result.period_frames = best_lag;
  result.bpm = bpm_from_lag_f(refined_lag, frame_rate);
  return result;
}

}  // namespace bpm
