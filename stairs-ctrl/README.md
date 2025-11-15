# Stairs Controller (FCOB)

## General

ESPHome project for an ESP32-based FCOB strip controller. It focuses on “row-first” animations for stair lighting and exposes four sequential effects (fill/off in both directions) that all share one helper, so changing effects resumes from the last position. Highlights:

- Programmable LED mapping: treat the strip as neatly indexed rows (with optional zig-zag) regardless of how it’s wired.
- Per-component map validation with built-in binary/text diagnostics; effects pause automatically if the mapping fails sanity checks.
- Directional fill/off effects with configurable per-LED timing, fade steps, row-threshold gating, and easing (Linear, Cubic InOut, Quint InOut).
- Optional wobble overlay (continuous hue drift) that keeps animating even after an effect ends.
- Automatic light shutdown for OFF effects (~50 ms after the last row clears) which also drops the relay power.
- Built-in OTA/API/web server plus runtime controls exposed to Home Assistant.

## How to Use

### Files

| File | Purpose |
| --- | --- |
| `stairs-ctrl/package.yaml` | Remote-ready ESPHome package (fetches helper via `external_components`). |
| `stairs-ctrl/package-local.yaml` | Local-only variant referencing the bundled helper without cloning. |
| `stairs-ctrl/example.yaml` | Sample local config consuming the package + substitutions. |
| `stairs-ctrl/custom_components/stairs_effects` | Custom component exposing the helper + effects. |

### Remote/Git Package

```yaml
substitutions:
  device_name: stairs-ctrl-livingroom
  system_api_key: !secret stairs_api_key
  system_ota_password: !secret stairs_ota
  system_timezone: "Europe/London"

packages:
  stairs_ctrl:
    url: https://github.com/timota/esphome-devices
    files: [stairs-ctrl/package.yaml]
    ref: main
    refresh: 30min
```

For local/offline development (when this repo is already on disk), include `stairs-ctrl/package-local.yaml` instead so ESPHome reads the helper from the bundled `custom_components/` directory without cloning.

### Stairs Effects component

```yaml
stairs_effects:
  - id: stairs_effects_component
    led_map_id: map
    led_count: ${light_num_leds}
    map_valid_binary_sensor:
      name: "LED Map Valid"
      entity_category: diagnostic
    map_status_text_sensor:
      name: "LED Map Status"
      entity_category: diagnostic
      icon: mdi:list-status
  - id: stairs_effects_component_upper
    led_map_id: upstairs_map

light:
  effects:
    - stairs_effects.fill_up: &stairs_defaults
        component_id: stairs_effects_component
        per_led_number_id: per_led_ms
        fade_steps_number_id: fade_steps_num
        row_threshold_number_id: row_trigger_threshold
        snake_switch_id: snake_mode
        wobble_switch_id: subtle_wobble_enable
        wobble_strength_number_id: subtle_hue_amp
        wobble_frequency_number_id: wobble_freq_deg
        easing_select_id: easing_mode
        shutdown_delay: 50ms
    - stairs_effects.fill_down:
        <<: *stairs_defaults
    - stairs_effects.off_up:
        <<: *stairs_defaults
    - stairs_effects.off_down:
        <<: *stairs_defaults
    - stairs_effects.fill_up:
        component_id: stairs_effects_component_upper
        per_led_number_id: per_led_ms_upper
        fade_steps_number_id: fade_steps_num_upper
        row_threshold_number_id: row_trigger_threshold_upper
        snake_switch_id: snake_mode_upper
        wobble_switch_id: subtle_wobble_enable_upper
        wobble_strength_number_id: subtle_hue_amp_upper
        wobble_frequency_number_id: wobble_freq_deg_upper
        easing_select_id: easing_mode_upper
        shutdown_delay: 100ms
```

#### Key substitutions

| Key | Description |
| --- | --- |
| `device_name`, `device_friendly_name`, `device_comment`, `device_version` | Hostname, HA name, metadata. |
| `system_api_key`, `system_ota_password`, `system_timezone` | Security + timezone (override these!). |
| `light_num_leds`, `light_brightness_r/g/b`, `light_led_map` | LED hardware settings and mapping. |
| `temperature_sensor_dig`, `temperature_sensor_ssr` | Dallas sensor addresses. |
| `ethernet_*` pins | LAN8720 wiring pins, addr, power. |

All substitution keys in `package.yaml` have defaults so you can bootstrap quickly—override anything that differs from your hardware.

### Controls & Sensors

| Entity | Type | Description |
| --- | --- | --- |
| `Snake (zig-zag rows)` | switch | Enables zig-zag traversal per row. |
| `Subtle Wobble` | switch | Toggles hue wobble overlay. |
| `Per-LED Time (ms)` | number | Travel time for the head. |
| `Fade Steps` | number | Sub-steps per LED (1–64). |
| `Row Trigger Threshold` | number | Fraction of a row required before the next row unlocks. |
| `Wobble Strength (hue °)` | number | Hue delta applied by wobble. |
| `Wobble Frequency (deg/s)` | number | Wobble speed. |
| `Easing` | select | Linear / Cubic InOut / Quint InOut. |
| `Digital LED Power Relay` | switch | Relay for the PSU. |
| `LED Map Valid` | binary sensor | Exposes per-component validation result. |
| `LED Map Status` | text sensor | Human-readable validation summary (error reason or OK). |

### Usage

1. Remote deployments: add the `packages:` snippet, override sensitive substitutions, and provision via ESPHome Dashboard or CLI (the helper/effects are fetched automatically through `external_components`). Local/offline testing: reference `package-local.yaml` (or run `esphome run stairs-ctrl/example.yaml`) so the helper is loaded from the checked-in `custom_components/` folder.  
2. In Home Assistant, select an effect, then tune Per-LED Time / Fade Steps / Row Threshold and toggle “Subtle Wobble” as you like.  
3. Let OFF effects finish naturally to trigger the automatic turn-off (which also releases the relay).

## Technical

### Effects

Four built-in effects are exposed via `stairs_effects.fill_*` / `stairs_effects.off_*`:

1. **Stairs Fill Up** – rows light bottom → top.  
2. **Stairs Fill Down** – rows light top → bottom.  
3. **Stairs Off Up** – rows fade off bottom → top (auto power-down).  
4. **Stairs Off Down** – rows fade off top → bottom (auto power-down).

Each effect owns its own `FcobProgressTracker` plus pointers to the runtime numbers/selects/switches provided in its YAML block, so you can run multiple maps (or duplicated effect sets) side by side with different controls. Effects refuse to render if their component reports an invalid map, keeping the LEDs dark and surfacing the diagnostic via the map-status sensor/log.

### Helper Highlights (`fcob_helper/led_helpers_fcob.h`)

- `FcobProgressTracker` tracks per-row progress, enforces row thresholds, and caps catch-up timing after slow frames.
- `RuntimeConfig` bundles per-LED timing, fade steps, thresholds, snake flag, easing, and wobble parameters.
- `color_with_wobble()`/`wobble_sample()` compute hue offsets per LED based on time, row, and amplitude.
- Support helpers (mapping, easing, clamp, resume scanning) are inline for minimal overhead.

## Mapping

Mapping lets the firmware address LEDs in any logical order. The `light_led_map` substitution holds an array of arrays: each inner list represents a physical row (in order or reversed). By updating that map you can match serpentine wiring, matrices, or stair treads without touching the effect logic. The `Snake (zig-zag rows)` switch flips row traversal per index, so you can dynamically choose between straight or serpentine addressing.

On boot every `stairs_effects` component validates its assigned map once (bounds, duplicates, empty rows) using the configured `led_count`. Results are published through the optional binary/text sensors shown above; if validation fails the component logs the error and effects stay idle until the configuration is fixed.

## Notes

- GPIO15 drives the relay and GPIO5 powers the Ethernet PHY—both are ESP32 strapping pins, so avoid strong external pull-ups/downs at boot.  
- Scan-in/out does not persist across reboots because the strip starts dark; progress survives only while the controller stays powered.  
- For smooth motion keep `per_led_ms` around 10–30 ms and `Fade Steps` between 1–3.  
