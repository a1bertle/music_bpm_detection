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

  std::vector<double> autocorr(static_cast<std::size_t>(max_lag + 1), 0.0);
  for (int lag = min_lag; lag <= max_lag; ++lag) {
    double sum = 0.0;
    for (std::size_t i = static_cast<std::size_t>(lag); i < onset_strength.size(); ++i) {
      sum += static_cast<double>(onset_strength[i]) * onset_strength[i - static_cast<std::size_t>(lag)];
    }
    autocorr[static_cast<std::size_t>(lag)] = sum;
  }

  int best_lag = min_lag;
  double best_score = -std::numeric_limits<double>::infinity();
  constexpr double kPriorCenter = 120.0;
  constexpr double kSigma = 0.5;

  for (int lag = min_lag; lag <= max_lag; ++lag) {
    float bpm = bpm_from_lag(lag, frame_rate);
    if (bpm <= 0.0f) {
      continue;
    }
    double log_ratio = std::log(bpm / static_cast<float>(kPriorCenter));
    double prior = std::exp(-0.5 * (log_ratio * log_ratio) / (kSigma * kSigma));
    double score = autocorr[static_cast<std::size_t>(lag)] * prior;
    if (score > best_score) {
      best_score = score;
      best_lag = lag;
    }
  }

  int half_lag = best_lag / 2;
  if (half_lag >= min_lag && half_lag < static_cast<int>(autocorr.size())) {
    if (autocorr[static_cast<std::size_t>(half_lag)] > 0.8 * autocorr[static_cast<std::size_t>(best_lag)]) {
      best_lag = half_lag;
    }
  }

  Result result;
  result.period_frames = best_lag;
  result.bpm = bpm_from_lag(best_lag, frame_rate);
  return result;
}

}  // namespace bpm
