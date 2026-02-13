#include "bpm/beat_tracker.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bpm {

BeatTracker::Result BeatTracker::track(const std::vector<float> &onset_strength,
                                       int period_frames,
                                       int hop_size,
                                       float alpha) const {
  Result result;
  if (period_frames <= 0 || hop_size <= 0 || onset_strength.empty()) {
    return result;
  }

  int total_frames = static_cast<int>(onset_strength.size());
  int min_lag = std::max(1, static_cast<int>(std::round(period_frames * 0.5f)));
  int max_lag = std::max(min_lag + 1, static_cast<int>(std::round(period_frames * 2.0f)));

  std::vector<double> dp(static_cast<std::size_t>(total_frames), -std::numeric_limits<double>::infinity());
  std::vector<int> prev(static_cast<std::size_t>(total_frames), -1);

  for (int t = 0; t < total_frames; ++t) {
    double best_score = onset_strength[static_cast<std::size_t>(t)];
    int best_prev = -1;

    int start = std::max(0, t - max_lag);
    int end = std::max(0, t - min_lag);
    for (int p = start; p <= end; ++p) {
      int lag = t - p;
      double log_ratio = std::log(static_cast<double>(lag) / static_cast<double>(period_frames));
      double penalty = alpha * (log_ratio * log_ratio);
      double score = dp[static_cast<std::size_t>(p)] + onset_strength[static_cast<std::size_t>(t)] - penalty;
      if (score > best_score) {
        best_score = score;
        best_prev = p;
      }
    }

    dp[static_cast<std::size_t>(t)] = best_score;
    prev[static_cast<std::size_t>(t)] = best_prev;
  }

  int search_start = static_cast<int>(total_frames * 0.9f);
  search_start = std::max(0, std::min(search_start, total_frames - 1));
  int best_end = search_start;
  double best_score = dp[static_cast<std::size_t>(search_start)];
  for (int t = search_start; t < total_frames; ++t) {
    if (dp[static_cast<std::size_t>(t)] > best_score) {
      best_score = dp[static_cast<std::size_t>(t)];
      best_end = t;
    }
  }

  std::vector<std::size_t> beat_frames;
  int idx = best_end;
  while (idx >= 0) {
    beat_frames.push_back(static_cast<std::size_t>(idx));
    idx = prev[static_cast<std::size_t>(idx)];
  }
  std::reverse(beat_frames.begin(), beat_frames.end());

  result.score = best_score;
  result.beat_samples.reserve(beat_frames.size());
  for (std::size_t frame : beat_frames) {
    result.beat_samples.push_back(frame * static_cast<std::size_t>(hop_size));
  }

  return result;
}

}  // namespace bpm
