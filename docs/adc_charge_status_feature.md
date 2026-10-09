# Keira ADC charging-status feature (prototype)

Hardware: [battery_charge_status_mod.rst](battery_charge_status_mod.rst).
Use the corrected LTV827 pinout (3=A2, 4=K2), CHRG tag 33k, STDBY tag 10k.

## Opt-in

- `v2-adc-charge-status`: ADC-tag modification, stock backlight wiring.
- `v2-modified-backlight-adc-charge-status`: ADC tags AND independently biased,
  isolated amplifier SD, as required by existing modified-backlight profile.
- Existing `v2` / `v2-modified-backlight` stay unchanged; there is no automatic
  detection of whether the modification is physically installed.
- Flag: `LILKA_ADC_CHARGE_STATUS=1`; v1 ignores it.

Uses the SDK charge monitor (`LILKA_ADC_CHARGE_STATUS=1`) and
`Battery::getChargeSnapshot()`; preserve sibling SDK configuration and matching general
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
voltage/percentage. During startup, a solid gray battery silhouette (no cross) and `...` indicate
that the first state is being measured (icon-only shows that same gray silhouette).
During band changes, retain the last confirmed icon/text/percentage/voltage until
three matching samples confirm the replacement. A confirmed absent battery
uses the original crossed gray battery icon + `N/A` (icon-only omits the text). Never recompute percent
or voltage from provisional charging tags; cache the normal battery values. After USB unplug, require three normal-band samples and
reset percent smoothing so the current battery estimate reappears immediately.

## Provisional bands and bounds

**These are normal-divider reconstructed volts returned by SDK readRawVoltage,
NOT actual GPIO3 volts and NOT full-level calibrated volts.**

- Absent: 0.00 <= V < 0.50 (same threshold as the SDK; three samples)
- Charged: 0.50 <= V < 1.40
- Charging: 1.50 < V < 2.65
- Battery: 2.80 < V <= 4.60
- Guard bands, invalid values, NaN/infinity: reset candidate debounce,
  retain the last confirmed display. With no confirmed state yet, keep `...`.
  There is no invalid-reading timeout: retained status is last-known, not proof
  that a disconnected or faulty sensor remains healthy.

Require three consecutive matching samples at the SDK's 1 Hz poll.
Detection is bounded and allocation-free. One 32-reading median is shared by
state and percentage estimation; a calibration attempt adds one fresh median.
Keira drawing performs no ADC/NVS work. The opt-in SDK worker owns these tasks;
stock/v1 builds do not start it. Existing status-bar Canvas/String behavior stays.

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

## SDK-owned charging and automatic calibration

The matching SDK owns ADC sampling, charge-state classification/debounce, and
full-reference calibration. Keira's profiles enable the shared SDK hardware flag
`LILKA_ADC_CHARGE_STATUS=1`, which also selects its charging presentation. No Keira battery-calibration service
or private decoder remains.

A confirmed **Charged → Battery** transition starts **30 seconds** of stabilization;
reconnection/tag, invalid/guard/absent or below-3.5 V samples cancel the opportunity.
Startup on Battery and unplugging from Charging never calibrate. Saving is checked
and attempted once per charge cycle; failure preserves the old reference.

The SDK worker runs independently of hidden widgets, fullscreen apps and LCD
sleep. It shares one 32-reading median per second between detection and percentage
estimation. Keira only reads a coherent cached snapshot and renders it, retaining
its icons, localized labels, provisional-state behavior and visual percentage
smoothing. Raw/voltage and discharge-profile APIs are unchanged.

Battery settings remains **Profile only**. Existing saved calibration/profile
values are preserved. Use matching `features/stage` or merged SDK
`features/stage-lilplayer` and Keira `features/stage`.

Checks (no firmware build or flash):

```sh
python3 tests/charge_status.py
python3 tools/checklang.py
# In the matching SDK checkout:
python3 tests/battery_calibration/run.py
```

SDK implementation and public API: [battery calibration and snapshots](https://github.com/fideleka/sdk/blob/features/stage/docs/BATTERY_CALIBRATION_SAVE.md).
Firmware size, live heap, physical UI, timing and power remain unmeasured by the
source-only checks. The SDK worker requests a 3 KiB stack instead of Keira's former
4 KiB service stack; there is no per-poll allocation or input/render-task NVS work.
