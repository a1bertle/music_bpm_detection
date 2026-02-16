#include "bpm/meter_detector.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>

namespace bpm {

std::string time_signature_string(TimeSignature ts) {
  switch (ts) {
    case TimeSignature::TWO_FOUR:   return "2/4";
    case TimeSignature::THREE_FOUR: return "3/4";
    case TimeSignature::FOUR_FOUR:  return "4/4";
    case TimeSignature::SIX_EIGHT:  return "6/8";
  }
  return "4/4";
}

float MeterDetector::accent_score(const std::vector<float> &onset_at_beat,
                                  int grouping, int phase) const {
  int n = static_cast<int>(onset_at_beat.size());
  if (n < grouping) return 0.0f;

  // Compute mean onset strength at each position within the grouping.
  std::vector<double> position_sum(grouping, 0.0);
  std::vector<int> position_count(grouping, 0);

  for (int i = 0; i < n; ++i) {
    int pos = ((i - phase) % grouping + grouping) % grouping;
    position_sum[pos] += onset_at_beat[i];
    position_count[pos]++;
  }

  // Downbeat is position 0.
  if (position_count[0] == 0) return 0.0f;
  double downbeat_mean = position_sum[0] / position_count[0];

  // Mean of non-downbeat positions.
  double other_sum = 0.0;
  int other_count = 0;
  for (int p = 1; p < grouping; ++p) {
    other_sum += position_sum[p];
    other_count += position_count[p];
  }
  if (other_count == 0) return 0.0f;
  double other_mean = other_sum / other_count;

  // Normalize by overall standard deviation.
  double mean = std::accumulate(onset_at_beat.begin(), onset_at_beat.end(), 0.0) / n;
  double var = 0.0;
  for (float v : onset_at_beat) {
    double d = v - mean;
    var += d * d;
  }
  var /= n;
  double stddev = std::sqrt(var);

  return static_cast<float>((downbeat_mean - other_mean) / (stddev + 1e-6));
}

float MeterDetector::beat_autocorrelation(const std::vector<float> &onset_at_beat,
                                          int lag) const {
  int n = static_cast<int>(onset_at_beat.size());
  if (lag <= 0 || lag >= n) return 0.0f;

  double r0 = 0.0;
  for (int i = 0; i < n; ++i) {
    r0 += static_cast<double>(onset_at_beat[i]) * onset_at_beat[i];
  }
  if (r0 < 1e-12) return 0.0f;

  double r_lag = 0.0;
  for (int i = 0; i < n - lag; ++i) {
    r_lag += static_cast<double>(onset_at_beat[i]) * onset_at_beat[i + lag];
  }

  // Normalize: scale by n/(n-lag) to compensate for fewer terms, then divide
  // by R(0).
  double scale = static_cast<double>(n) / (n - lag);
  return static_cast<float>((r_lag * scale) / r0);
}

bool MeterDetector::check_compound_subdivision(
    const std::vector<std::size_t> &beat_samples,
    const std::vector<float> &onset_strength,
    int hop_size,
    bool verbose) const {
  // Check whether the subdivision between consecutive beats is ternary (3)
  // rather than binary (2).  If ternary, the meter is compound (6/8).
  //
  // Method: for each pair of consecutive beats, sample onset strength at
  // 1/3 and 2/3 of the interval (ternary grid) and at 1/2 (binary grid).
  // If ternary positions are consistently stronger, the subdivision is
  // compound.
  int n = static_cast<int>(beat_samples.size());
  if (n < 4) return false;

  int onset_len = static_cast<int>(onset_strength.size());
  double ternary_total = 0.0;
  double binary_total = 0.0;
  int count = 0;

  for (int i = 0; i < n - 1; ++i) {
    double start = static_cast<double>(beat_samples[i]);
    double end = static_cast<double>(beat_samples[i + 1]);
    double span = end - start;
    if (span <= 0) continue;

    // Ternary positions: 1/3 and 2/3 of the interval.
    int frame_t1 = static_cast<int>(std::round((start + span / 3.0) / hop_size));
    int frame_t2 = static_cast<int>(std::round((start + 2.0 * span / 3.0) / hop_size));

    // Binary position: 1/2 of the interval.
    int frame_b = static_cast<int>(std::round((start + span / 2.0) / hop_size));

    if (frame_t1 >= onset_len || frame_t2 >= onset_len || frame_b >= onset_len)
      continue;
    if (frame_t1 < 0 || frame_t2 < 0 || frame_b < 0)
      continue;

    double t_strength = (onset_strength[frame_t1] + onset_strength[frame_t2]) / 2.0;
    double b_strength = onset_strength[frame_b];

    ternary_total += t_strength;
    binary_total += b_strength;
    ++count;
  }

  if (count < 4) return false;

  double ternary_avg = ternary_total / count;
  double binary_avg = binary_total / count;

  if (verbose) {
    std::cout << "    Compound subdivision: ternary_avg=" << ternary_avg
              << ", binary_avg=" << binary_avg
              << ", ratio=" << (binary_avg > 1e-12 ? ternary_avg / binary_avg : 0.0)
              << ", count=" << count << "\n";
  }

  // Ternary subdivision wins if it averages meaningfully higher than binary.
  // Both values are z-score normalized, so negative means below-average onset
  // strength.  When both are negative, neither subdivision is pronounced — just
  // noise — so default to binary (not compound).  Additionally require a 10%
  // margin when both are positive, to avoid borderline false positives.
  if (ternary_avg <= 0.0) return false;
  if (binary_avg <= 0.0) return true;  // ternary positive, binary not
  return ternary_avg > 1.1 * binary_avg;
}

std::vector<std::size_t> MeterDetector::extract_downbeats(
    const std::vector<std::size_t> &beat_samples,
    int grouping, int phase) const {
  std::vector<std::size_t> downbeats;
  int n = static_cast<int>(beat_samples.size());
  for (int i = phase; i < n; i += grouping) {
    downbeats.push_back(beat_samples[i]);
  }
  return downbeats;
}

MeterDetector::Result MeterDetector::detect(
    const std::vector<std::size_t> &beat_samples,
    const std::vector<float> &onset_strength,
    int hop_size,
    int sample_rate,
    float bpm,
    bool verbose) const {
  (void)sample_rate;
  (void)bpm;

  Result result;
  int num_beats = static_cast<int>(beat_samples.size());

  // Not enough beats for reliable meter detection.
  if (num_beats < 8) {
    result.time_signature = TimeSignature::FOUR_FOUR;
    result.beats_per_measure = 4;
    result.downbeat_phase = 0;
    result.confidence = 0.0f;
    result.downbeat_samples = extract_downbeats(beat_samples, 4, 0);
    if (verbose) {
      std::cout << "Meter detection: too few beats (" << num_beats
                << "), defaulting to 4/4\n";
    }
    return result;
  }

  // Collect onset strengths at beat positions.
  int onset_len = static_cast<int>(onset_strength.size());
  std::vector<float> onset_at_beat(num_beats);
  for (int i = 0; i < num_beats; ++i) {
    int frame = static_cast<int>(beat_samples[i]) / hop_size;
    onset_at_beat[i] = (frame >= 0 && frame < onset_len)
        ? onset_strength[frame]
        : 0.0f;
  }

  // Test groupings N=2,3,4 at all phase offsets.
  constexpr float kAccentWeight = 0.7f;
  constexpr float kAutocorrWeight = 0.3f;

  int best_grouping = 4;
  int best_phase = 0;
  float best_score = -1e9f;
  float best_accent = 0.0f;

  if (verbose) {
    std::cout << "Meter detection:\n";
  }

  for (int g : {2, 3, 4}) {
    float autocorr = beat_autocorrelation(onset_at_beat, g);

    if (verbose) {
      std::cout << "  Testing grouping=" << g << ":\n";
    }

    for (int phi = 0; phi < g; ++phi) {
      float accent = accent_score(onset_at_beat, g, phi);
      float score = kAccentWeight * accent + kAutocorrWeight * autocorr;

      if (verbose) {
        std::cout << "    phase=" << phi
                  << ": accent_contrast=" << accent
                  << ", autocorr=" << autocorr
                  << ", score=" << score << "\n";
      }

      if (score > best_score) {
        best_score = score;
        best_grouping = g;
        best_phase = phi;
        best_accent = accent;
      }
    }
  }

  // 2/4 vs 4/4 disambiguation: the strong-weak alternation of 4/4 means
  // 2/4 almost always scores well.  Prefer 4/4 when the 4-beat grouping
  // has any meaningful accent contrast (beat 1 stronger than beat 3), since
  // 4/4 is far more common than 2/4 in practice.
  if (best_grouping == 2) {
    float autocorr4 = beat_autocorrelation(onset_at_beat, 4);
    float best4_accent = -1e9f;
    int best4_phase = 0;
    for (int phi = 0; phi < 4; ++phi) {
      float accent = accent_score(onset_at_beat, 4, phi);
      if (accent > best4_accent) {
        best4_accent = accent;
        best4_phase = phi;
      }
    }
    float score4 = kAccentWeight * best4_accent + kAutocorrWeight * autocorr4;
    // Prefer 4/4 if its score is at least 80% of the 2/4 score, or if the
    // 4-beat accent contrast is positive (beat 1 distinguishable from beat 3).
    if (best4_accent > 0.1f || score4 > best_score * 0.8f) {
      if (verbose) {
        std::cout << "  Preferring 4/4 over 2/4 (4-beat accent="
                  << best4_accent << ", score=" << score4 << ")\n";
      }
      best_grouping = 4;
      best_phase = best4_phase;
      best_accent = best4_accent;
      best_score = score4;
    }
  }

  // Map grouping to time signature.
  switch (best_grouping) {
    case 2: result.time_signature = TimeSignature::TWO_FOUR; break;
    case 3: result.time_signature = TimeSignature::THREE_FOUR; break;
    case 4: result.time_signature = TimeSignature::FOUR_FOUR; break;
  }
  result.beats_per_measure = best_grouping;
  result.downbeat_phase = best_phase;

  // Confidence: accent_contrast / 2.0 clamped to [0, 1].
  result.confidence = std::max(0.0f, std::min(1.0f, best_accent / 2.0f));

  // Low-confidence fallback: default to 4/4 when the winning non-4/4
  // grouping doesn't clearly outperform grouping=4.  This avoids
  // overriding genuine 3/4 waltzes (where grouping=3 wins by a clear
  // margin) while still falling back for noisy/ambiguous results.
  if (result.confidence < 0.15f && best_grouping != 4) {
    float best4_fallback_score = -1e9f;
    int best4_fallback_phase = 0;
    for (int phi = 0; phi < 4; ++phi) {
      float accent = accent_score(onset_at_beat, 4, phi);
      float autocorr = beat_autocorrelation(onset_at_beat, 4);
      float score = kAccentWeight * accent + kAutocorrWeight * autocorr;
      if (score > best4_fallback_score) {
        best4_fallback_score = score;
        best4_fallback_phase = phi;
      }
    }
    // Only fall back if the winner doesn't exceed the best 4/4 score by >10%.
    if (best_score < best4_fallback_score * 1.1f) {
      if (verbose) {
        std::cout << "  Low confidence (" << result.confidence
                  << "), falling back to 4/4 (winner score=" << best_score
                  << " vs 4/4 score=" << best4_fallback_score << ")\n";
      }
      result.time_signature = TimeSignature::FOUR_FOUR;
      result.beats_per_measure = 4;
      result.downbeat_phase = best4_fallback_phase;
    } else if (verbose) {
      std::cout << "  Low confidence (" << result.confidence
                << ") but winner clearly beats 4/4 (score=" << best_score
                << " vs 4/4=" << best4_fallback_score << "), keeping "
                << best_grouping << "-grouping\n";
    }
  }

  // 6/8 check: if the winner is 2/4, check for compound (ternary)
  // subdivision.  6/8 at the dotted-quarter level looks like 2/4 but with
  // beats subdividing into 3 rather than 2.
  if (result.time_signature == TimeSignature::TWO_FOUR) {
    bool compound = check_compound_subdivision(beat_samples, onset_strength,
                                               hop_size, verbose);
    if (verbose) {
      std::cout << "  Compound subdivision check: "
                << (compound ? "ternary (6/8)" : "binary (2/4)") << "\n";
    }
    if (compound) {
      result.time_signature = TimeSignature::SIX_EIGHT;
      // beats_per_measure stays 2 — these are dotted-quarter beats.
    }
  }

  // Also check 6/8 when grouping is 3: if beats are at the dotted-quarter
  // level in compound time, the subdivision between consecutive beats will
  // be ternary (3 eighth notes) rather than binary (2 eighth notes in 3/4).
  if (result.time_signature == TimeSignature::THREE_FOUR) {
    bool compound = check_compound_subdivision(beat_samples, onset_strength,
                                               hop_size, verbose);
    if (verbose) {
      std::cout << "  Compound subdivision check (3/4): "
                << (compound ? "ternary (6/8)" : "binary (3/4)") << "\n";
    }
    if (compound) {
      result.time_signature = TimeSignature::SIX_EIGHT;
      // A full 6/8 measure = 2 groups of 3 beats = 6 beats.
      result.beats_per_measure = 6;
    }
  }

  result.downbeat_samples = extract_downbeats(beat_samples,
                                              result.beats_per_measure,
                                              result.downbeat_phase);

  if (verbose) {
    std::cout << "  Selected: " << time_signature_string(result.time_signature)
              << ", phase=" << result.downbeat_phase
              << ", confidence=" << result.confidence << "\n";
  }

  return result;
}

}  // namespace bpm
