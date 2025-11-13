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
| `stairs-ctrl/package.yaml` | Remote-ready ESPHome package (Ethernet build). |
| `stairs-ctrl/stairs.yaml` | Local shim that includes the package and injects secrets. |
| `includes/led_helpers_fcob.h` | Inline helper exposing `FcobProgressTracker` and math utilities reused by every effect. |

## Remote/Git Package

Reference the package directly from ESPHome:

```yaml
substitutions:
  device_name: stairs-ctrl-livingroom
  system_api_key: !secret stairs_api_key
  system_ota_password: !secret stairs_ota
  system_timezone: "Europe/London"

packages:
  stairs_ctrl:
    url: https://github.com/timota/_esphome
    files: [stairs-ctrl/package.yaml]
    ref: main
    refresh: 30min
```

### Key substitutions

| Key | Description |
| --- | --- |
| `device_name`, `device_friendly_name`, `device_comment`, `device_version` | Hostname, HA name, and project metadata. |
| `system_api_key`, `system_ota_password`, `system_timezone` | Security + timezone settings (override these!). |
| `light_num_leds`, `light_brightness_r/g/b`, `light_led_map` | Hardware-specific LED settings (count, gamma/brightness, and mapping). |
| `temperature_sensor_dig`, `temperature_sensor_ssr` | Dallas sensor addresses. |
| `ethernet_*` pins | LAN8720 wiring pins/addr/power. |

All substitution keys in `stairs-ctrl/package.yaml` have defaults so you can start quickly—override whichever ones differ.

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
- OFF effects schedule a one-shot `light.turn_off()` about 50 ms after every row clears, which gracefully triggers the existing relay automation.

## Helper Highlights (`includes/led_helpers_fcob.h`)

- `FcobProgressTracker` keeps per-row progress, enforces row-threshold gating, and handles catch-up timing (caps to 2× sub-step to avoid jitter).  
- `RuntimeConfig` carries all runtime knobs, including wobble enable/amp/freq.  
- `color_with_wobble()` samples HSV hue offsets per row/LED based on time, row index, LED index, and the configured amplitude/frequency.  
- Shared helpers (map traversal, easing, clamp, resume scanning) are inline for minimal call overhead.

## Usage

1. If using the package remotely, add the `packages:` snippet above and override the sensitive substitutions. For local testing, edit `stairs-ctrl/stairs.yaml` (which already injects secrets) and run `esphome run stairs-ctrl/stairs.yaml`.  
2. Toggle between the four effects in HA, adjust Per-LED Time / Fade Steps / Threshold until the pacing fits, and enable “Subtle Wobble” for the hue drift overlay.  
3. The relay switch still hard-powers the LED PSU whenever the light entity turns off.

## Notes

- GPIO15 is used for the relay; ESPHome warns because it is a strapping pin—ensure external hardware does not force boot modes.  
- Scan-in/out does not persist across reboots because the strip powers up dark; the helper only resumes while the device stays powered.  
- Keep `per_led_ms` modest (10–30 ms) to avoid excessive frame rates; `Fade Steps` between 1–3 typically looks best on FCOB strips.  
