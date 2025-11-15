// FCOB helper utilities – staged rebuild
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "esphome/components/globals/globals_component.h"
#include "esphome/components/light/addressable_light.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"
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
  // Per-frame knobs pulled from YAML controls.
  uint32_t per_led_ms{24};
  int fade_steps{1};
  float row_threshold{0.2f};
  bool snake{false};
  EaseProfile ease{EaseProfile::CubicInOut};
  bool wobble_enabled{false};
  float wobble_amp_deg{0.0f};
  float wobble_freq_deg{12.0f};
};

struct EffectPlan {
  // High-level intent describing the running effect.
  FlowMode flow{FlowMode::Fill};
  RowOrder order{RowOrder::BottomToTop};
};

struct ResumeSnapshot {
  // Lightweight snapshot so scan-in/out can resume statefully.
  std::vector<float> lit_rows;
};

struct RowProgress {
  // Working state per mapped row.
  int row_len{0};
  float lit_count{0.0f};
  float substep_acc{0.0f};
  bool active{false};
  bool finished{false};
};

struct BaseColorState {
  // Cached HSV + RGB for wobble sampling.
  esphome::Color rgb{esphome::Color::BLACK};
  float h{0.0f};
  float s{0.0f};
  float v{0.0f};
};

class FcobProgressTracker {
 public:
  // Attach the LED map (id(map) from YAML).
  void bind_map(const std::vector<std::vector<int>> *map);
  // Reset progress; optionally keep the current lit counts for resume.
  void reset(bool clear_resume = true);

  // Scan the strip to recover already-lit prefixes (scan-in/out).
  void sync_from_strip(esphome::light::AddressableLight &strip, bool snake);
  // Load an external snapshot back into working memory.
  void load_snapshot(const ResumeSnapshot &snapshot);
  // Capture current per-row progress.
  ResumeSnapshot snapshot() const;

  // Start an effect plan; optionally reuse resume data.
  void start_effect(const EffectPlan &plan, bool resume);

  // Advance the effect by one frame and repaint the strip.
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

  // Ensure our row vector matches the current map size.
  void ensure_row_cache();
  // Refresh cached row lengths after any map updates.
  void refresh_row_lengths();
  // Make sure at least one unfinished row is active.
  void ensure_active_row();
  // Find the next unfinished row from either end.
  int first_available_row(bool from_top) const;
  // Find the neighbor row relative to the active one.
  int neighbor_row(int current, bool from_top) const;
  // Flag a row as active and reset its timers.
  void activate_row(int idx);
  // Aggregate per-row finished flags.
  void update_finished_flag();

  void handle_fill_frame(esphome::light::AddressableLight &strip,
                         const RuntimeConfig &cfg,
                         const BaseColorState &base_state,
                         float t_sec,
                         uint32_t dt_ms);
  void handle_off_frame(esphome::light::AddressableLight &strip,
                        const RuntimeConfig &cfg,
                        const BaseColorState &base_state,
                        float t_sec,
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
void rgb2hsv(uint8_t r, uint8_t g, uint8_t b, float &h, float &s, float &v);
esphome::Color hsv2rgb(float h, float s, float v);
float sin_deg_fast(float degrees);
float smoothstep(float edge0, float edge1, float x);
esphome::Color wobble_sample(const BaseColorState &base_state,
                             const RuntimeConfig &cfg,
                             int row_index,
                             int phys_led,
                             float t_sec);
esphome::Color color_with_wobble(const BaseColorState &base_state,
                                 const RuntimeConfig &cfg,
                                 int row_index,
                                 int phys_led,
                                 float intensity,
                                 float t_sec);

}  // namespace ledhelpers

// ---------------- Implementation ----------------
namespace ledhelpers {

namespace {
constexpr uint8_t kMinOnU8 = 6;                 // lit detection floor
constexpr float kEpsilon = 0.0001f;             // tiny tolerance for comparisons
constexpr float kWobbleVMin = 0.15f;            // wobble ramps in near this V
constexpr float kWobbleVMax = 0.60f;            // wobble peaks by this V
constexpr float kRowPhaseMul = 9.5f;            // row-specific wobble phase spread
constexpr float kLedPhaseMul = 0.5f;            // per-pixel wobble phase spread
constexpr float kPi = 3.14159265358979323846f;  // pi constant
}  // namespace

inline void FcobProgressTracker::bind_map(const std::vector<std::vector<int>> *map) {
  map_ = map;
  ensure_row_cache();
  refresh_row_lengths();
}

// Clear cached progress and optionally zero resume data.
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

// Recover per-row lit prefix counts from the strip, used for scan-in/out.
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

// Restore progress from a previously taken snapshot.
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

// Capture current per-row lit counts.
inline ResumeSnapshot FcobProgressTracker::snapshot() const {
  ResumeSnapshot snap;
  snap.lit_rows.reserve(rows_.size());
  for (const auto &row : rows_) snap.lit_rows.push_back(row.lit_count);
  return snap;
}

// Initialize an effect plan and optionally reuse resume state.
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

// Step the effect once and repaint the entire strip.
inline bool FcobProgressTracker::render_frame(esphome::light::AddressableLight &strip,
                                              const RuntimeConfig &cfg,
                                              const esphome::Color &base_color,
                                              uint32_t now_ms) {
  if (!map_ || rows_.empty()) return false;
  ensure_row_cache();
  refresh_row_lengths();
  ensure_active_row();

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

  BaseColorState base_state;
  base_state.rgb = base_color;
  rgb2hsv(base_color.r, base_color.g, base_color.b, base_state.h, base_state.s, base_state.v);

  const float t_sec = now_ms / 1000.0f;

  if (plan_.flow == FlowMode::Fill) {
    handle_fill_frame(strip, cfg, base_state, t_sec, dt_ms);
  } else {
    handle_off_frame(strip, cfg, base_state, t_sec, dt_ms);
  }
  update_finished_flag();
  return true;
}

// Ensure rows_ vector matches the bound map.
inline void FcobProgressTracker::ensure_row_cache() {
  if (!map_) {
    rows_.clear();
    return;
  }
  if (rows_.size() != map_->size()) rows_.assign(map_->size(), RowProgress{});
}

// Sync cached row lengths and clamp lit counts.
inline void FcobProgressTracker::refresh_row_lengths() {
  if (!map_) return;
  for (size_t i = 0; i < rows_.size(); ++i) {
    auto &row = rows_[i];
    row.row_len = row_len(*map_, (int) i);
    if (row.row_len < 0) row.row_len = 0;
    row.lit_count = esphome::clamp(row.lit_count, 0.0f, (float) row.row_len);
  }
}

// Turn on the first unfinished row if none are active.
inline void FcobProgressTracker::ensure_active_row() {
  if (rows_.empty()) return;
  for (const auto &row : rows_) {
    if (row.active && !row.finished) return;
  }
  const bool from_top = plan_.order == RowOrder::TopToBottom;
  const int idx = first_available_row(from_top);
  if (idx >= 0) rows_[idx].active = true;
}

// Find first unfinished row scanning from either side.
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

// Find next unfinished neighbor from the current row.
inline int FcobProgressTracker::neighbor_row(int current, bool from_top) const {
  if (rows_.empty()) return -1;
  int idx = current;
  while (true) {
    idx += from_top ? -1 : 1;
    if (idx < 0 || idx >= (int) rows_.size()) return -1;
    if (!rows_[idx].finished) return idx;
  }
}

// Arm a row for animation.
inline void FcobProgressTracker::activate_row(int idx) {
  if (idx < 0 || idx >= (int) rows_.size()) return;
  auto &row = rows_[idx];
  if (row.finished) return;
  row.active = true;
  row.substep_acc = 0.0f;
}

// Recompute the aggregate finished_ flag.
inline void FcobProgressTracker::update_finished_flag() {
  finished_ = true;
  for (const auto &row : rows_) {
    if (!row.finished) {
      finished_ = false;
      break;
    }
  }
}

// Progress ON animation and repaint rows with wobble applied.
inline void FcobProgressTracker::handle_fill_frame(esphome::light::AddressableLight &strip,
                                                   const RuntimeConfig &cfg,
                                                   const BaseColorState &base_state,
                                                   float t_sec,
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
      float intensity = 0.0f;
      if (i < full) intensity = 1.0f;
      else if (i == full && full < len) intensity = apply_ease(cfg.ease, frac);
      strip[phys] = color_with_wobble(base_state, cfg, (int) ridx, phys, intensity, t_sec);
    }
  }
}

// Progress OFF animation and repaint rows with wobble applied.
inline void FcobProgressTracker::handle_off_frame(esphome::light::AddressableLight &strip,
                                                  const RuntimeConfig &cfg,
                                                  const BaseColorState &base_state,
                                                  float t_sec,
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
      float intensity = 0.0f;
      if (i < full) intensity = 1.0f;
      else if (i == full && full < len) intensity = apply_ease(cfg.ease, frac);
      strip[phys] = color_with_wobble(base_state, cfg, (int) ridx, phys, intensity, t_sec);
    }
  }
}

// Convert per-LED timing + fade steps into a sub-step interval.
inline uint32_t compute_step_ms(uint32_t per_led_ms, int fade_steps) {
  if (fade_steps <= 0) fade_steps = 1;
  float step_f = (float) per_led_ms / (float) fade_steps;
  if (step_f < 2.0f) step_f = 2.0f;
  return (uint32_t) step_f;
}

// Apply selected easing profile to a 0..1 value.
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

// Decide when the next row should unlock based on threshold progress.
inline bool should_unlock(int len, int progress, float thr, bool off_mode) {
  if (len <= 0) return false;
  const int gate = (int) std::ceil(clamp01(thr) * (float) len);
  if (!off_mode) return progress >= gate;
  const int cleared = len - progress;
  return cleared >= gate;
}

// Helper for ON-direction threshold checks.
inline bool should_unlock_on(int len, int progress, float thr) {
  return should_unlock(len, progress, thr, false);
}

// Helper for OFF-direction threshold checks.
inline bool should_unlock_off(int len, int progress, float thr) {
  return should_unlock(len, progress, thr, true);
}

// Determine if this row should be traversed in reverse due to snake mode.
inline bool row_reverse_forward_fill(int row_index, bool snake_on) {
  if (!snake_on) return false;
  return (row_index % 2) == 1;
}

// Safe row length lookup.
inline int row_len(const std::vector<std::vector<int>> &map, int row) {
  if (row < 0 || row >= (int) map.size()) return 0;
  return (int) map[row].size();
}

// Logical index -> physical LED index with optional zig-zag reversal.
inline int row_phys_at(const std::vector<std::vector<int>> &map, int row, int i, bool snake) {
  if (row < 0 || row >= (int) map.size()) return -1;
  const auto &row_vec = map[row];
  const int len = (int) row_vec.size();
  if (i < 0 || i >= len) return -1;
  const bool rev = row_reverse_forward_fill(row, snake);
  const int logical = rev ? (len - 1 - i) : i;
  return row_vec[logical];
}

// Physical LED at the ON head.
inline int row_head_index_fill(const std::vector<std::vector<int>> &map, int row, int pos,
                               bool snake) {
  if (pos < 0) return -1;
  return row_phys_at(map, row, pos, snake);
}

// Physical LED at the OFF head.
inline int row_head_index_off(const std::vector<std::vector<int>> &map, int row, int pos,
                              bool snake) {
  if (pos <= 0) return -1;
  return row_phys_at(map, row, pos - 1, snake);
}

// Count how many LEDs in a row are currently lit (used for resume).
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

// Quick brightness check with hysteresis to detect "lit" LEDs.
inline bool is_led_lit_soft(esphome::light::AddressableLight &strip, int phys_led) {
  if (phys_led < 0 || phys_led >= strip.size()) return false;
  const auto color = strip[phys_led].get();
  const uint8_t peak = std::max({color.r, color.g, color.b});
  return peak >= kMinOnU8;
}

// Scale a color intensity while preserving hue.
inline esphome::Color scale_color(const esphome::Color &c, float factor) {
  factor = clamp01(factor);
  auto apply = [&](uint8_t channel) -> uint8_t {
    return (uint8_t) esphome::clamp((int) std::roundf(channel * factor), 0, 255);
  };
  return esphome::Color(apply(c.r), apply(c.g), apply(c.b));
}

// Advance time accumulators by at most one sub-step per frame.
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

// Clamp a float to [0,1].
inline float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

// Shared singleton tracker used by all YAML effects.
inline FcobProgressTracker &global_tracker() {
  static FcobProgressTracker tracker;
  return tracker;
}

// Minimal RGB->HSV conversion for wobble calculations.
inline void rgb2hsv(uint8_t r, uint8_t g, uint8_t b, float &h, float &s, float &v) {
  float rf = r / 255.0f;
  float gf = g / 255.0f;
  float bf = b / 255.0f;
  float cmax = std::max(rf, std::max(gf, bf));
  float cmin = std::min(rf, std::min(gf, bf));
  float delta = cmax - cmin;

  if (delta == 0.0f) h = 0.0f;
  else if (cmax == rf) h = 60.0f * std::fmod(((gf - bf) / delta), 6.0f);
  else if (cmax == gf) h = 60.0f * (((bf - rf) / delta) + 2.0f);
  else h = 60.0f * (((rf - gf) / delta) + 4.0f);
  if (h < 0.0f) h += 360.0f;

  s = (cmax == 0.0f) ? 0.0f : (delta / cmax);
  v = cmax;
}

// Minimal HSV->RGB conversion.
inline esphome::Color hsv2rgb(float h, float s, float v) {
  h = std::fmod(h, 360.0f);
  if (h < 0.0f) h += 360.0f;
  s = clamp01(s);
  v = clamp01(v);

  float c = v * s;
  float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;

  float rf = 0, gf = 0, bf = 0;
  if      (h < 60.0f)  { rf = c; gf = x; bf = 0; }
  else if (h < 120.0f) { rf = x; gf = c; bf = 0; }
  else if (h < 180.0f) { rf = 0; gf = c; bf = x; }
  else if (h < 240.0f) { rf = 0; gf = x; bf = c; }
  else if (h < 300.0f) { rf = x; gf = 0; bf = c; }
  else                 { rf = c; gf = 0; bf = x; }

  uint8_t rr = (uint8_t) esphome::clamp((int) std::round((rf + m) * 255.0f), 0, 255);
  uint8_t gg = (uint8_t) esphome::clamp((int) std::round((gf + m) * 255.0f), 0, 255);
  uint8_t bb = (uint8_t) esphome::clamp((int) std::round((bf + m) * 255.0f), 0, 255);
  return esphome::Color(rr, gg, bb);
}

// Degrees-based sine helper.
inline float sin_deg_fast(float degrees) {
  return std::sinf(degrees * (kPi / 180.0f));
}

// Smoothstep helper for wobble amplitude scaling.
inline float smoothstep(float edge0, float edge1, float x) {
  float t = clamp01((x - edge0) / (edge1 - edge0));
  return t * t * (3.0f - 2.0f * t);
}

// Sample a wobble-adjusted color for a specific LED.
inline esphome::Color wobble_sample(const BaseColorState &base_state,
                                    const RuntimeConfig &cfg,
                                    int row_index,
                                    int phys_led,
                                    float t_sec) {
  if (!cfg.wobble_enabled || cfg.wobble_amp_deg <= 0.0f || base_state.v <= 0.0f) {
    return base_state.rgb;
  }
  const float amp_scale = smoothstep(kWobbleVMin, kWobbleVMax, base_state.v);
  if (amp_scale <= 0.0f) return base_state.rgb;

  const float hue_amp = cfg.wobble_amp_deg * amp_scale;
  float phase = t_sec * cfg.wobble_freq_deg;
  phase += (float) phys_led * kLedPhaseMul;
  phase += (float) row_index * kRowPhaseMul;

  const float hue = base_state.h + sin_deg_fast(phase) * hue_amp;
  return hsv2rgb(hue, base_state.s, base_state.v);
}

// Apply wobble (if enabled) and an intensity scalar to the base color.
inline esphome::Color color_with_wobble(const BaseColorState &base_state,
                                        const RuntimeConfig &cfg,
                                        int row_index,
                                        int phys_led,
                                        float intensity,
                                        float t_sec) {
  intensity = clamp01(intensity);
  if (intensity <= 0.0f) return esphome::Color::BLACK;

  esphome::Color c = base_state.rgb;
  if (cfg.wobble_enabled && cfg.wobble_amp_deg > 0.0f && cfg.wobble_freq_deg != 0.0f) {
    c = wobble_sample(base_state, cfg, row_index, phys_led, t_sec);
  }
  if (intensity >= 0.999f) return c;
  return scale_color(c, intensity);
}

}  // namespace ledhelpers

namespace esphome {
namespace stairs_effects {

using led_map_t = std::vector<std::vector<int>>;

class StairsEffectsComponent : public Component {
 public:
  void setup() override {}
  void loop() override {}

  void set_led_map(globals::GlobalsComponent<led_map_t> *map) { map_holder_ = map; }
  void set_per_led_number(number::Number *num) { per_led_number_ = num; }
  void set_fade_steps_number(number::Number *num) { fade_steps_number_ = num; }
  void set_row_threshold_number(number::Number *num) { row_threshold_number_ = num; }
  void set_snake_switch(switch_::Switch *sw) { snake_switch_ = sw; }
  void set_wobble_switch(switch_::Switch *sw) { wobble_switch_ = sw; }
  void set_wobble_strength_number(number::Number *num) { wobble_strength_number_ = num; }
  void set_wobble_frequency_number(number::Number *num) { wobble_frequency_number_ = num; }
  void set_easing_select(select::Select *sel) { easing_select_ = sel; }
  void set_shutdown_delay(uint32_t delay_ms) { shutdown_delay_ms_ = delay_ms; }

  const led_map_t *led_map() const {
    return map_holder_ != nullptr ? &map_holder_->value() : nullptr;
  }
  ledhelpers::RuntimeConfig build_runtime_config() const;
  uint32_t shutdown_delay_ms() const { return shutdown_delay_ms_; }

 private:
  globals::GlobalsComponent<led_map_t> *map_holder_{nullptr};
  number::Number *per_led_number_{nullptr};
  number::Number *fade_steps_number_{nullptr};
  number::Number *row_threshold_number_{nullptr};
  switch_::Switch *snake_switch_{nullptr};
  switch_::Switch *wobble_switch_{nullptr};
  number::Number *wobble_strength_number_{nullptr};
  number::Number *wobble_frequency_number_{nullptr};
  select::Select *easing_select_{nullptr};
  uint32_t shutdown_delay_ms_{50};
};

class StairsBaseEffect : public light::AddressableLightEffect {
 public:
  StairsBaseEffect(StairsEffectsComponent *parent,
                   const std::string &name,
                   ledhelpers::FlowMode flow,
                   ledhelpers::RowOrder order,
                   bool off_mode);
  void start() override {
    this->initialized_ = false;
    this->shutdown_scheduled_ = false;
    light::AddressableLightEffect::start();
  }
  void apply(light::AddressableLight &it, const Color &current_color) override;

 protected:
  StairsEffectsComponent *parent_;
  ledhelpers::FlowMode flow_;
  ledhelpers::RowOrder order_;
  bool off_mode_;
  ledhelpers::FcobProgressTracker tracker_;

  bool initialized_{false};
  bool snake_state_{false};
  bool shutdown_scheduled_{false};
  uint32_t shutdown_at_{0};
};

class StairsFillUpEffect : public StairsBaseEffect {
 public:
  StairsFillUpEffect(StairsEffectsComponent *parent, const std::string &name);
};

class StairsFillDownEffect : public StairsBaseEffect {
 public:
  StairsFillDownEffect(StairsEffectsComponent *parent, const std::string &name);
};

class StairsOffUpEffect : public StairsBaseEffect {
 public:
  StairsOffUpEffect(StairsEffectsComponent *parent, const std::string &name);
};

class StairsOffDownEffect : public StairsBaseEffect {
 public:
  StairsOffDownEffect(StairsEffectsComponent *parent, const std::string &name);
};

// ---- Inline implementations ----

inline ledhelpers::RuntimeConfig StairsEffectsComponent::build_runtime_config() const {
  ledhelpers::RuntimeConfig cfg;
  if (per_led_number_ != nullptr) {
    float val = per_led_number_->state;
    if (val < 1.0f) val = 1.0f;
    cfg.per_led_ms = static_cast<uint32_t>(val);
  }
  if (fade_steps_number_ != nullptr) {
    int steps = static_cast<int>(fade_steps_number_->state);
    if (steps <= 0) steps = 1;
    cfg.fade_steps = steps;
  }
  if (row_threshold_number_ != nullptr) {
    float thr = row_threshold_number_->state;
    if (thr < 0.0f) thr = 0.0f;
    if (thr > 1.0f) thr = 1.0f;
    cfg.row_threshold = thr;
  }
  cfg.snake = snake_switch_ != nullptr ? snake_switch_->state : false;
  cfg.wobble_enabled = wobble_switch_ != nullptr ? wobble_switch_->state : false;
  if (wobble_strength_number_ != nullptr) cfg.wobble_amp_deg = wobble_strength_number_->state;
  if (wobble_frequency_number_ != nullptr) cfg.wobble_freq_deg = wobble_frequency_number_->state;

  if (easing_select_ != nullptr) {
    const std::string &state = easing_select_->state;
    if (state == "Linear") cfg.ease = ledhelpers::EaseProfile::Linear;
    else if (state == "Quint InOut") cfg.ease = ledhelpers::EaseProfile::QuintInOut;
    else cfg.ease = ledhelpers::EaseProfile::CubicInOut;
  }
  return cfg;
}

inline StairsBaseEffect::StairsBaseEffect(StairsEffectsComponent *parent,
                                          const std::string &name,
                                          ledhelpers::FlowMode flow,
                                          ledhelpers::RowOrder order,
                                          bool off_mode)
    : light::AddressableLightEffect(name),
      parent_(parent),
      flow_(flow),
      order_(order),
      off_mode_(off_mode) {}

inline StairsFillUpEffect::StairsFillUpEffect(StairsEffectsComponent *parent, const std::string &name)
    : StairsBaseEffect(parent, name, ledhelpers::FlowMode::Fill, ledhelpers::RowOrder::BottomToTop, false) {}

inline StairsFillDownEffect::StairsFillDownEffect(StairsEffectsComponent *parent, const std::string &name)
    : StairsBaseEffect(parent, name, ledhelpers::FlowMode::Fill, ledhelpers::RowOrder::TopToBottom, false) {}

inline StairsOffUpEffect::StairsOffUpEffect(StairsEffectsComponent *parent, const std::string &name)
    : StairsBaseEffect(parent, name, ledhelpers::FlowMode::Off, ledhelpers::RowOrder::BottomToTop, true) {}

inline StairsOffDownEffect::StairsOffDownEffect(StairsEffectsComponent *parent, const std::string &name)
    : StairsBaseEffect(parent, name, ledhelpers::FlowMode::Off, ledhelpers::RowOrder::TopToBottom, true) {}

inline void StairsBaseEffect::apply(light::AddressableLight &it, const Color &current_color) {
  const auto *map = parent_->led_map();
  if (map == nullptr || map->empty()) {
    for (int i = 0; i < it.size(); ++i) it[i] = Color::BLACK;
    return;
  }

  tracker_.bind_map(map);
  auto cfg = parent_->build_runtime_config();
  bool snake_now = cfg.snake;
  bool restart = false;
  if (!initialized_ || snake_now != snake_state_) restart = true;

  if (restart) {
    tracker_.sync_from_strip(it, snake_now);
    tracker_.start_effect({flow_, order_}, true);
    snake_state_ = snake_now;
    initialized_ = true;
    shutdown_scheduled_ = false;
  }

  tracker_.render_frame(it, cfg, current_color, millis());

  it.schedule_show();

  if (!off_mode_) return;

  if (!tracker_.finished()) {
    shutdown_scheduled_ = false;
    return;
  }

  if (!shutdown_scheduled_) {
    shutdown_scheduled_ = true;
    shutdown_at_ = millis() + parent_->shutdown_delay_ms();
    return;
  }

  if ((int32_t)(millis() - shutdown_at_) >= 0) {
    auto call = this->state_->make_call();
    call.set_state(false);
    call.perform();
    shutdown_scheduled_ = false;
  }
}

}  // namespace stairs_effects
}  // namespace esphome
