# Stairs Controller (FCOB)

Addressable FCOB strip controller built with ESPHome + ESP32.  
Features:

- 244-pixel WS2811 FCOB map with alternating “snake” rows toggle.
- Four runtime effects (fill/off in both directions) sharing a single C++ helper so scan-in/out picks up where the last effect stopped.
- Per-LED timing, fade-step subdivision, row-threshold gating, and easing (Linear, Cubic, Quint).
- Optional wobble overlay (continuous hue drift) that keeps animating even when an effect is finished.
- Power relay automation, OTA/API/web server, and runtime switches/numbers for tuning.

## Files

| File | Purpose |
| --- | --- |
| `stairs.yaml` | ESPHome node definition, controls, light effects. |
| `includes/led_helpers_fcob.h` | Inline helper exposing `FcobProgressTracker` and math utilities reused by every effect. |

## Controls & Sensors

| Entity | Type | Description |
| --- | --- | --- |
| `Snake (zig-zag rows)` | switch | Enables alternating row direction. |
| `Subtle Wobble` | switch | Enables wobble overlay (hue drift). |
| `Per-LED Time (ms)` | number | Travel time for one LED head. |
| `Fade Steps` | number | Sub-steps per LED (1–64). |
| `Row Trigger Threshold` | number | Fraction of a row that must finish before the next row unlocks. |
| `Wobble Strength (hue °)` | number | Hue delta applied by wobble. |
| `Wobble Frequency (deg/s)` | number | Speed of the wobble animation. |
| `Easing` | select | Linear / Cubic InOut / Quint InOut for the fractional head. |
| `Digital LED Power Relay` | switch | Relay driving the 24 V supply. |

## Effects

All four effects are defined as `addressable_lambda` blocks that delegate to `ledhelpers::global_tracker()`:

1. **FCOB Fill Up** – rows light bottom → top.  
2. **FCOB Fill Down** – rows light top → bottom.  
3. **FCOB Off Up** – rows fade off bottom → top.  
4. **FCOB Off Down** – rows fade off top → bottom.

Every lambda:

- Binds the LED map, validates controls, and fills `RuntimeConfig`.
- Applies the same wobble/amplitude/frequency values.
- Calls `start_effect(...)` only when the direction or snake flag changes, so scan-in/out is seamless.
- Calls `render_frame(...)` each update so wobble keeps running even after rows complete.

## Helper Highlights (`includes/led_helpers_fcob.h`)

- `FcobProgressTracker` keeps per-row progress, enforces row-threshold gating, and handles catch-up timing (caps to 2× sub-step to avoid jitter).  
- `RuntimeConfig` carries all runtime knobs, including wobble enable/amp/freq.  
- `color_with_wobble()` samples HSV hue offsets per row/LED based on time, row index, LED index, and the configured amplitude/frequency.  
- Shared helpers (map traversal, easing, clamp, resume scanning) are inline for minimal call overhead.

## Usage

1. Update `secrets.yaml` with Wi-Fi/API/OTA credentials.  
2. Compile: `esphome run stairs.yaml`.  
3. In Home Assistant (or ESPHome Dashboard) toggle the effect you want, adjust Per-LED Time / Fade Steps / Threshold until the gait fits your stairs, and enable “Subtle Wobble” if you want the continuous hue drift.  
4. Use the relay switch if you need to hard-power-cycle the LED PSU before OTA flashing.

## Notes

- GPIO15 is used for the relay; ESPHome warns because it is a strapping pin—ensure external hardware does not force boot modes.  
- Scan-in/out does not persist across reboots because the strip powers up dark; the helper only resumes while the device stays powered.  
- Keep `per_led_ms` modest (10–30 ms) to avoid excessive frame rates; `Fade Steps` between 1–3 typically looks best on FCOB strips.  
