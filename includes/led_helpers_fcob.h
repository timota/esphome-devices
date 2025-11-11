// FCOB helper skeleton — stage 2 interface design
#pragma once

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
  std::vector<float> lit_rows;  // each entry is 0..row_len
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

  // Returns true when frame rendered; false if idle/no map bound.
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

  // Internal helpers (implemented in Stage 3)
  void ensure_row_cache();
  void refresh_row_lengths();
  void update_row_activity(const RuntimeConfig &cfg);
  void handle_fill_frame(esphome::light::AddressableLight &strip,
                         const RuntimeConfig &cfg,
                         const esphome::Color &base_color,
                         uint32_t now_ms);
  void handle_off_frame(esphome::light::AddressableLight &strip,
                        const RuntimeConfig &cfg,
                        const esphome::Color &base_color,
                        uint32_t now_ms);
};

// Shared helpers (implemented in Stage 3)
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

}  // namespace ledhelpers
