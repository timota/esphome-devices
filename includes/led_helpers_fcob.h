// FCOB helper utilities – staged rebuild
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "esphome/components/light/addressable_light.h"
#include "esphome/core/helpers.h"

namespace ledhelpers {

enum class FlowMode {
  Fill,
  Off,
};

enum class RowOrder {
  BottomToTop,
  TopToBottom,
};

enum class EaseProfile {
  Linear,
  CubicInOut,
  QuintInOut,
};

enum class EffectFlavor {
  FillBottomToTop,
  FillTopToBottom,
  OffBottomToTop,
  OffTopToBottom,
};

struct RuntimeConfig {
  uint32_t per_led_ms{24};
  int fade_steps{1};
  float row_threshold{0.2f};
  bool snake{false};
  EaseProfile ease{EaseProfile::CubicInOut};
};

struct EffectPlan {
  FlowMode flow{FlowMode::Fill};
  RowOrder order{RowOrder::BottomToTop};
};

struct ResumeSnapshot {
  std::vector<float> lit_rows;
};

struct RowProgress {
  int row_len{0};
  float lit_count{0.0f};
  float substep_acc{0.0f};
  bool active{false};
  bool finished{false};
};

class FcobProgressTracker {
 public:
  void bind_map(const std::vector<std::vector<int>> *map);
  void reset(bool clear_resume = true);

  void sync_from_strip(esphome::light::AddressableLight &strip, bool snake);
  void load_snapshot(const ResumeSnapshot &snapshot);
  ResumeSnapshot snapshot() const;

  void start_effect(const EffectPlan &plan, bool resume);

  bool render_frame(esphome::light::AddressableLight &strip,
                    const RuntimeConfig &cfg,
                    const esphome::Color &base_color,
                    uint32_t now_ms);

  bool finished() const { return finished_; }
  EffectPlan plan() const { return plan_; }

 private:
  const std::vector<std::vector<int>> *map_{nullptr};
  EffectPlan plan_{};
  std::vector<RowProgress> rows_;
  bool finished_{true};
  bool first_frame_{true};
  uint32_t last_frame_ms_{0};

  void ensure_row_cache();
  void refresh_row_lengths();
  void ensure_active_row();
  int first_available_row(bool from_top) const;
  int neighbor_row(int current, bool from_top) const;
  void activate_row(int idx);
  void update_finished_flag();

  void handle_fill_frame(esphome::light::AddressableLight &strip,
                         const RuntimeConfig &cfg,
                         const esphome::Color &base_color,
                         uint32_t dt_ms);
  void handle_off_frame(esphome::light::AddressableLight &strip,
                        const RuntimeConfig &cfg,
                        const esphome::Color &base_color,
                        uint32_t dt_ms);
};

uint32_t compute_step_ms(uint32_t per_led_ms, int fade_steps);
float apply_ease(EaseProfile ease, float t);
bool should_unlock(int len, int progress, float thr, bool off_mode);
bool should_unlock_on(int len, int progress, float thr);
bool should_unlock_off(int len, int progress, float thr);
int row_len(const std::vector<std::vector<int>> &map, int row);
int row_phys_at(const std::vector<std::vector<int>> &map, int row, int i, bool snake);
int row_head_index_fill(const std::vector<std::vector<int>> &map, int row, int pos, bool snake);
int row_head_index_off(const std::vector<std::vector<int>> &map, int row, int pos, bool snake);
int scan_resume_row_prefix(esphome::light::AddressableLight &strip,
                           const std::vector<std::vector<int>> &map,
                           int row,
                           bool snake);
bool is_led_lit_soft(esphome::light::AddressableLight &strip, int phys_led);
esphome::Color scale_color(const esphome::Color &c, float factor);
bool row_reverse_forward_fill(int row_index, bool snake_on);
bool advance_one_substep(float &acc_ms, uint32_t step_ms, uint32_t dt_ms);
float clamp01(float v);
FcobProgressTracker &global_tracker();

}  // namespace ledhelpers

// ---------------- Implementation ----------------
namespace ledhelpers {

namespace {
constexpr uint8_t kMinOnU8 = 6;
constexpr float kEpsilon = 0.0001f;
}  // namespace

inline void FcobProgressTracker::bind_map(const std::vector<std::vector<int>> *map) {
  map_ = map;
  ensure_row_cache();
  refresh_row_lengths();
}

inline void FcobProgressTracker::reset(bool clear_resume) {
  finished_ = true;
  first_frame_ = true;
  last_frame_ms_ = 0;
  if (!map_) {
    rows_.clear();
    return;
  }
  ensure_row_cache();
  refresh_row_lengths();
  for (auto &row : rows_) {
    row.active = false;
    row.substep_acc = 0.0f;
    if (clear_resume || plan_.flow == FlowMode::Fill) row.lit_count = 0.0f;
    if (clear_resume && plan_.flow == FlowMode::Off) row.lit_count = (float) row.row_len;
    row.finished = row.row_len <= 0;
  }
}

inline void FcobProgressTracker::sync_from_strip(esphome::light::AddressableLight &strip,
                                                 bool snake) {
  if (!map_) return;
  ensure_row_cache();
  refresh_row_lengths();
  for (size_t idx = 0; idx < rows_.size(); ++idx) {
    auto &row = rows_[idx];
    row.lit_count = (float) scan_resume_row_prefix(strip, *map_, (int) idx, snake);
    row.active = false;
    row.finished = row.row_len <= 0 || row.lit_count >= row.row_len - kEpsilon;
    row.substep_acc = 0.0f;
  }
}

inline void FcobProgressTracker::load_snapshot(const ResumeSnapshot &snapshot) {
  if (!map_) return;
  ensure_row_cache();
  refresh_row_lengths();
  const size_t lim = std::min(rows_.size(), snapshot.lit_rows.size());
  for (size_t i = 0; i < lim; ++i) {
    auto &row = rows_[i];
    row.lit_count =
        esphome::clamp(snapshot.lit_rows[i], 0.0f, (float) std::max(0, row.row_len));
    row.active = false;
    row.finished = row.row_len <= 0 || row.lit_count >= row.row_len - kEpsilon;
    row.substep_acc = 0.0f;
  }
}

inline ResumeSnapshot FcobProgressTracker::snapshot() const {
  ResumeSnapshot snap;
  snap.lit_rows.reserve(rows_.size());
  for (const auto &row : rows_) snap.lit_rows.push_back(row.lit_count);
  return snap;
}

inline void FcobProgressTracker::start_effect(const EffectPlan &plan, bool resume) {
  plan_ = plan;
  finished_ = false;
  first_frame_ = true;
  last_frame_ms_ = 0;
  if (!map_) {
    rows_.clear();
    finished_ = true;
    return;
  }
  ensure_row_cache();
  refresh_row_lengths();
  for (auto &row : rows_) {
    row.active = false;
    row.substep_acc = 0.0f;
    if (!resume) {
      row.lit_count = (plan_.flow == FlowMode::Fill) ? 0.0f : (float) row.row_len;
    } else {
      row.lit_count = esphome::clamp(row.lit_count, 0.0f, (float) row.row_len);
    }
    row.finished = row.row_len <= 0 ||
                   (plan_.flow == FlowMode::Fill ? (row.lit_count >= row.row_len - kEpsilon)
                                                 : (row.lit_count <= kEpsilon));
  }
  ensure_active_row();
  update_finished_flag();
}

inline bool FcobProgressTracker::render_frame(esphome::light::AddressableLight &strip,
                                              const RuntimeConfig &cfg,
                                              const esphome::Color &base_color,
                                              uint32_t now_ms) {
  if (!map_ || rows_.empty()) return false;
  ensure_row_cache();
  refresh_row_lengths();
  ensure_active_row();
  if (finished_) return true;

  if (first_frame_) {
    first_frame_ = false;
    last_frame_ms_ = now_ms;
  }
  uint32_t dt_ms = now_ms - last_frame_ms_;
  last_frame_ms_ = now_ms;

  const uint32_t step_ms = compute_step_ms(cfg.per_led_ms, cfg.fade_steps);
  if (step_ms > 0) {
    const uint32_t cap = step_ms * 2u;
    if (dt_ms > cap) dt_ms = cap;
  }

  if (plan_.flow == FlowMode::Fill) {
    handle_fill_frame(strip, cfg, base_color, dt_ms);
  } else {
    handle_off_frame(strip, cfg, base_color, dt_ms);
  }
  update_finished_flag();
  return true;
}

inline void FcobProgressTracker::ensure_row_cache() {
  if (!map_) {
    rows_.clear();
    return;
  }
  if (rows_.size() != map_->size()) rows_.assign(map_->size(), RowProgress{});
}

inline void FcobProgressTracker::refresh_row_lengths() {
  if (!map_) return;
  for (size_t i = 0; i < rows_.size(); ++i) {
    auto &row = rows_[i];
    row.row_len = row_len(*map_, (int) i);
    if (row.row_len < 0) row.row_len = 0;
    row.lit_count = esphome::clamp(row.lit_count, 0.0f, (float) row.row_len);
  }
}

inline void FcobProgressTracker::ensure_active_row() {
  if (rows_.empty()) return;
  for (const auto &row : rows_) {
    if (row.active && !row.finished) return;
  }
  const bool from_top = plan_.order == RowOrder::TopToBottom;
  const int idx = first_available_row(from_top);
  if (idx >= 0) rows_[idx].active = true;
}

inline int FcobProgressTracker::first_available_row(bool from_top) const {
  if (rows_.empty()) return -1;
  if (from_top) {
    for (int i = (int) rows_.size() - 1; i >= 0; --i)
      if (!rows_[i].finished) return i;
  } else {
    for (int i = 0; i < (int) rows_.size(); ++i)
      if (!rows_[i].finished) return i;
  }
  return -1;
}

inline int FcobProgressTracker::neighbor_row(int current, bool from_top) const {
  if (rows_.empty()) return -1;
  int idx = current;
  while (true) {
    idx += from_top ? -1 : 1;
    if (idx < 0 || idx >= (int) rows_.size()) return -1;
    if (!rows_[idx].finished) return idx;
  }
}

inline void FcobProgressTracker::activate_row(int idx) {
  if (idx < 0 || idx >= (int) rows_.size()) return;
  auto &row = rows_[idx];
  if (row.finished) return;
  row.active = true;
  row.substep_acc = 0.0f;
}

inline void FcobProgressTracker::update_finished_flag() {
  finished_ = true;
  for (const auto &row : rows_) {
    if (!row.finished) {
      finished_ = false;
      break;
    }
  }
}

inline void FcobProgressTracker::handle_fill_frame(esphome::light::AddressableLight &strip,
                                                   const RuntimeConfig &cfg,
                                                   const esphome::Color &base_color,
                                                   uint32_t dt_ms) {
  if (!map_) return;
  const uint32_t step_ms = compute_step_ms(cfg.per_led_ms, cfg.fade_steps);
  const float substep = 1.0f / (float) std::max(1, cfg.fade_steps);
  const bool from_top = plan_.order == RowOrder::TopToBottom;

  for (size_t ridx = 0; ridx < rows_.size(); ++ridx) {
    auto &row = rows_[ridx];
    if (row.row_len <= 0) {
      row.finished = true;
      continue;
    }
    if (row.active && !row.finished && step_ms > 0) {
      if (advance_one_substep(row.substep_acc, step_ms, dt_ms)) {
        row.lit_count += substep;
        if (row.lit_count >= row.row_len - kEpsilon) {
          row.lit_count = (float) row.row_len;
          row.finished = true;
          row.active = false;
        }
      }
    }

    const int lit_int = (int) std::floor(row.lit_count + kEpsilon);
    if (row.active && !row.finished) {
      if (should_unlock_on(row.row_len, lit_int, cfg.row_threshold)) {
        const int next = neighbor_row((int) ridx, from_top);
        if (next >= 0 && !rows_[next].active) activate_row(next);
      }
    }

    const int len = row.row_len;
    const int full = std::min(lit_int, len);
    const float frac = clamp01(row.lit_count - (float) full);
    for (int i = 0; i < len; ++i) {
      const int phys = row_phys_at(*map_, (int) ridx, i, cfg.snake);
      if (phys < 0 || phys >= strip.size()) continue;
      if (i < full) {
        strip[phys] = base_color;
      } else if (i == full && full < len) {
        strip[phys] = scale_color(base_color, apply_ease(cfg.ease, frac));
      } else {
        strip[phys] = esphome::Color::BLACK;
      }
    }
  }
}

inline void FcobProgressTracker::handle_off_frame(esphome::light::AddressableLight &strip,
                                                  const RuntimeConfig &cfg,
                                                  const esphome::Color &base_color,
                                                  uint32_t dt_ms) {
  if (!map_) return;
  const uint32_t step_ms = compute_step_ms(cfg.per_led_ms, cfg.fade_steps);
  const float substep = 1.0f / (float) std::max(1, cfg.fade_steps);
  const bool from_top = plan_.order == RowOrder::TopToBottom;

  for (size_t ridx = 0; ridx < rows_.size(); ++ridx) {
    auto &row = rows_[ridx];
    if (row.row_len <= 0) {
      row.finished = true;
      continue;
    }
    if (row.active && !row.finished && step_ms > 0) {
      if (advance_one_substep(row.substep_acc, step_ms, dt_ms)) {
        row.lit_count -= substep;
        if (row.lit_count <= kEpsilon) {
          row.lit_count = 0.0f;
          row.finished = true;
          row.active = false;
        }
      }
    }

    const int lit_int = (int) std::floor(row.lit_count + kEpsilon);
    if (row.active && !row.finished) {
      if (should_unlock_off(row.row_len, lit_int, cfg.row_threshold)) {
        const int next = neighbor_row((int) ridx, from_top);
        if (next >= 0 && !rows_[next].active) activate_row(next);
      }
    }

    const int len = row.row_len;
    const int full = std::min(lit_int, len);
    const float frac = clamp01(row.lit_count - (float) full);
    for (int i = 0; i < len; ++i) {
      const int phys = row_phys_at(*map_, (int) ridx, i, cfg.snake);
      if (phys < 0 || phys >= strip.size()) continue;
      if (i < full) {
        strip[phys] = base_color;
      } else if (i == full && full < len) {
        strip[phys] = scale_color(base_color, apply_ease(cfg.ease, frac));
      } else {
        strip[phys] = esphome::Color::BLACK;
      }
    }
  }
}

inline uint32_t compute_step_ms(uint32_t per_led_ms, int fade_steps) {
  if (fade_steps <= 0) fade_steps = 1;
  float step_f = (float) per_led_ms / (float) fade_steps;
  if (step_f < 2.0f) step_f = 2.0f;
  return (uint32_t) step_f;
}

inline float apply_ease(EaseProfile ease, float t) {
  t = clamp01(t);
  switch (ease) {
    case EaseProfile::Linear:
      return t;
    case EaseProfile::CubicInOut:
      return (t < 0.5f) ? 4.0f * t * t * t
                        : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    case EaseProfile::QuintInOut:
      return (t < 0.5f) ? 16.0f * t * t * t * t * t
                        : 1.0f - std::pow(-2.0f * t + 2.0f, 5.0f) / 2.0f;
    default:
      return t;
  }
}

inline bool should_unlock(int len, int progress, float thr, bool off_mode) {
  if (len <= 0) return false;
  const int gate = (int) std::ceil(clamp01(thr) * (float) len);
  if (!off_mode) return progress >= gate;
  const int cleared = len - progress;
  return cleared >= gate;
}

inline bool should_unlock_on(int len, int progress, float thr) {
  return should_unlock(len, progress, thr, false);
}

inline bool should_unlock_off(int len, int progress, float thr) {
  return should_unlock(len, progress, thr, true);
}

inline bool row_reverse_forward_fill(int row_index, bool snake_on) {
  if (!snake_on) return false;
  return (row_index % 2) == 1;
}

inline int row_len(const std::vector<std::vector<int>> &map, int row) {
  if (row < 0 || row >= (int) map.size()) return 0;
  return (int) map[row].size();
}

inline int row_phys_at(const std::vector<std::vector<int>> &map, int row, int i, bool snake) {
  if (row < 0 || row >= (int) map.size()) return -1;
  const auto &row_vec = map[row];
  const int len = (int) row_vec.size();
  if (i < 0 || i >= len) return -1;
  const bool rev = row_reverse_forward_fill(row, snake);
  const int logical = rev ? (len - 1 - i) : i;
  return row_vec[logical];
}

inline int row_head_index_fill(const std::vector<std::vector<int>> &map, int row, int pos,
                               bool snake) {
  if (pos < 0) return -1;
  return row_phys_at(map, row, pos, snake);
}

inline int row_head_index_off(const std::vector<std::vector<int>> &map, int row, int pos,
                              bool snake) {
  if (pos <= 0) return -1;
  return row_phys_at(map, row, pos - 1, snake);
}

inline int scan_resume_row_prefix(esphome::light::AddressableLight &strip,
                                  const std::vector<std::vector<int>> &map, int row, bool snake) {
  const int len = row_len(map, row);
  if (len <= 0) return 0;
  int lit = 0;
  for (int i = 0; i < len; ++i) {
    const int phys = row_phys_at(map, row, i, snake);
    if (phys < 0 || phys >= strip.size()) break;
    if (is_led_lit_soft(strip, phys)) {
      lit = i + 1;
    } else {
      break;
    }
  }
  return esphome::clamp(lit, 0, len);
}

inline bool is_led_lit_soft(esphome::light::AddressableLight &strip, int phys_led) {
  if (phys_led < 0 || phys_led >= strip.size()) return false;
  const auto color = strip[phys_led].get();
  const uint8_t peak = std::max({color.r, color.g, color.b});
  return peak >= kMinOnU8;
}

inline esphome::Color scale_color(const esphome::Color &c, float factor) {
  factor = clamp01(factor);
  auto apply = [&](uint8_t channel) -> uint8_t {
    return (uint8_t) esphome::clamp((int) std::roundf(channel * factor), 0, 255);
  };
  return esphome::Color(apply(c.r), apply(c.g), apply(c.b));
}

inline bool advance_one_substep(float &acc_ms, uint32_t step_ms, uint32_t dt_ms) {
  if (step_ms == 0) return false;
  const uint32_t cap = step_ms * 2u;
  uint32_t dt = dt_ms;
  if (dt > cap) dt = cap;
  acc_ms += (float) dt;
  if (acc_ms >= step_ms) {
    acc_ms -= (float) step_ms;
    return true;
  }
  return false;
}

inline float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

inline FcobProgressTracker &global_tracker() {
  static FcobProgressTracker tracker;
  return tracker;
}

}  // namespace ledhelpers
