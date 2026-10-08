# Modified-Lilka backlight test

Branch: `test/modified-backlight-brightness`, based on Keira `020f5a1`.
SDK source inspected: sibling `../sdk` -> `lilka-sdk`, revision `63df8e1`.

## Hardware requirement

**Only use on Lilka v2 after the amplifier module's SD header connection has been physically disconnected from GPIO46 and independently biased.** Stock hardware shares GPIO46 between display BLK and amplifier SD; PWM would interfere with audio. Anton reports SD isolated, additional bias resistors installed, about 590 kΩ measured VIN-to-SD and functioning audio. These reports are not an independent hardware inspection.

## Test behaviour

- Boots directly into a full-screen test, instead of the launcher.
- Hold Select + Left: dim by 10 percentage points.
- Hold Select + Right: brighten by 10 percentage points.
- Initial repeat delay 400 ms, then 100 ms; opposing directions or Start cancel adjustment.
- Range 0–100%. At 0%, Select + Right still restores light.
- Every boot starts at 100%; no brightness NVS reads/writes, settings, or automatic sleep.
- LCD remains awake. Only GPIO46 is driven by LEDC: channel 7 / timer 3, 20 kHz, 8-bit duty. Percent is PWM duty, not calibrated perceived brightness.
- Failure to set up PWM leaves GPIO46 HIGH and displays an error.
- The ordinary services, autorun scripts, status bar and launcher are not started in this isolated test, avoiding other LEDC users and input conflicts. No music player or audio test is added.
- Board-power-saving APIs and I²S configuration are untouched; SDK sources are unchanged.

The v2 environment on this branch defines `KEIRA_BACKLIGHT_TEST=1`. Removing that definition restores normal startup. Do not merge this default into general-use firmware without an explicit modified-hardware gate.

## Checks

Host-only control test:

```sh
g++ -std=c++11 -Wall -Wextra -Werror -Isrc tests/backlight_control.cpp -o /tmp/keira-backlight-control-test
/tmp/keira-backlight-control-test
python3 tools/checklang.py
git diff --check
```

Covers clamping, zero-brightness recovery, no adjustment without Select, direction reversal, repeat timing, no delayed bursts, opposing directions, Start priority, rollover and reboot default.

`make checklang` requires a `python` executable; on this host the same script is run with installed `python3`. clang-format-20 and cppcheck are unavailable; no installations were made.

No PlatformIO firmware build, link, packaging or flash was performed. Hardware PWM, perceived brightness, flicker/noise and concurrent audio remain device tests. In particular, independent amplifier bias does not by itself prove immunity to physical wiring noise from PWM.
