# Keira ADC charging-status feature (prototype)

Hardware: [battery_charge_status_mod.rst](battery_charge_status_mod.rst).
Use the corrected LTV827 pinout (3=A2, 4=K2), CHRG tag 33k, STDBY tag 10k.

## Opt-in

- `v2-adc-charge-status`: ADC-tag modification, stock backlight wiring.
- `v2-modified-backlight-adc-charge-status`: ADC tags AND independently biased,
  isolated amplifier SD, as required by existing modified-backlight profile.
- Existing `v2` / `v2-modified-backlight` stay unchanged; there is no automatic
  detection of whether the modification is physically installed.
- Flag: `KEIRA_ADC_CHARGE_STATUS=1`; v1 ignores it.

No SDK changes or new libraries. Uses existing `Battery::readRawVoltage()` and
`readEstimatedLevel()`; preserve sibling SDK configuration and matching general
SDK staging for Keira. Feature base is local Keira stage `152b727` (includes
previous corrected hardware documentation), whose parent is remote stage
`f441106`. SDK general stage inspected: `add708a5`.

## Display behavior

Battery setting Off stays Off. With normal battery band, all four existing modes
retain calibrated estimated percentage, existing smoothing, and raw-voltage mode.
With CHRG tag: yellow lightning battery + CHG / ЗАР (no charging percentage).
With STDBY tag: green check battery + FULL / ГОТО (termination, not fuel-gauge data).
Icon-only mode shows only the changed battery icon. Percent-only and voltage
modes show the charge icon + short state instead of falsely reporting tagged
voltage/percentage. During startup, an empty battery outline and `...` indicate
that the first state is being measured (icon-only shows the outline).
During band changes, retain the last confirmed icon/text/percentage/voltage until
three matching valid samples confirm the replacement. Never recompute percent
or voltage from provisional charging tags; cache the normal battery values. After USB unplug, require three normal-band samples and
reset percent smoothing so the current battery estimate reappears immediately.

## Provisional bands and bounds

**These are normal-divider reconstructed volts returned by SDK readRawVoltage,
NOT actual GPIO3 volts and NOT full-level calibrated volts.**

- Charged: 0.50 <= V < 1.40
- Charging: 1.50 < V < 2.65
- Battery: 2.80 < V <= 4.60
- Guard bands, absent/invalid values, NaN/infinity: reset candidate debounce,
  retain the last confirmed display. With no confirmed state yet, keep `...`.
  There is no invalid-reading timeout: retained status is last-known, not proof
  that a disconnected or faulty sensor remains healthy.

Require three consecutive matching samples at the existing 1 Hz status-bar tick.
Decoder has constant work and three small state fields; no allocations or NVS.
Sampling: 32-sample SDK median once/tick; battery-percent modes additionally use
existing 32-sample estimated-level read. Thus <=64 ADC conversions/tick; voltage
and tagged states use 32. No new worker/task/lock, writes, pin reconfiguration or
positive voltage injection. Existing status-bar Canvas/String behavior retained.

Device measurements MUST establish bands (including optocoupler saturation,
battery range, unplug/replug, Wi-Fi/display load). Tags indicate charger output,
not a separate physical USB-presence detector. Fault/absent-cell states can be
ambiguous. Console load can prevent TP4056 termination. Calibrate full level only
with USB disconnected; do not infer charge progress from tagged ADC readings.

## Verification

`python3 tests/charge_status.py` and `--sanitize` compile production decoder and
extracted real drawBattery/stableBatteryLevel against HAL stubs, with UK/EN,
stock v2, modified v2, and v1 guard. Test transitions, invalid/guard-band inputs,
theoretical full voltage range, restored percentage, startup placeholder,
unchanged pending-state display and cached percent/voltage, all four display modes,
no percent sampling in tagged states, bounded widget widths, and ADC call counts.
`python3 tools/checklang.py` checks localization parity.

Fresh host captures of the real drawBattery function using the installed
FONT_9x15 Cyrillic font and production RGB565 icons were inspected for startup,
pending plug/full/unplug, and confirmed states in English and Ukrainian.
`make clang-format` / `make cppcheck` were attempted but tools are unavailable.

Host tests are NOT firmware build/link or device certification. No build,
flash, packaging, dependency installation or hardware changes were performed.
