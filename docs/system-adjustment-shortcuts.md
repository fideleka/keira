# SDK global adjustment integration

Use the matching Lilka SDK system-adjustment-shortcuts source branch. Global
Select-first arbitration is owned solely by the SDK controller; Keira installs no
parallel shortcut handler. Ordinary Select, Select+Start pause/exit, per-axis NES/GB
precision delays and turbo mapping are unchanged.

- Volume changes by 5 points, clamps to 0..100 with mute, repeats at 400/100 ms,
  and is saved after 600 ms without a change by the SDK settings task.
- Shortcut directions never reach Keira polling or callback consumers and remain
  suppressed until physical release, including Select-first-release cases.
- **Brightness is hardware-blocked:** Select+Left/Right remains ordinary application input;
  it does not dim Lilka v2. Its only exposed backlight/sleep line also controls the
  I2S/MAX98357 module. No shared-pin PWM, fake brightness setting or display overlay
  has been added. See SDK docs/system-adjustment-shortcuts.md.

## Live master volume in actual output paths

NES already calls audio.getVolume() once per generated frame; the updated SDK makes
that a RAM read rather than per-frame NVS access. Game Boy no longer snapshots
volume at startup: each audio frame uses current master volume. Tracker scales
bounded 128-sample scratch chunks in its I2S sink without modifying const synth
input; its existing 0.25 track-local initial gain stays separate from master gain.
The WAV/file sink is intentionally not changed (exports retain track-local gain).
AudioPlayer applies live system master gain before each generator loop, including
custom outputs/analyzer sinks. Its public setGain/getGain now describe application
local gain (0..4, initial unity), multiplied by the live master for actual output;
application gain cannot bypass global mute. No per-buffer persistence is done.
Sound settings observes live master changes and refreshes its local volume row; its
explicit Save/exit reconciles newer global values instead of overwriting them with
a stale initial snapshot, while preserving unsaved local edits when master is unchanged.

## Host verification (no firmware build)

Run python3 tests/system_shortcuts.py --sdk /path/to/matching/sdk-worktree. It
compiles actual GB/NES/tracker output functions, SDK PCM scaling, AudioPlayer's
actual loop gain expression and existing per-axis DirectionFilter against host
boundaries, in C++11 normal and ASan/UBSan modes. Binaries use temporary directories.
Existing tests/nesmenu_input.py, gameboy_input.py, emulator_menu.py, menu_access.py,
menu_render.py and navigation/run.py remain applicable. For navigation, set
LILKA_SDK_MENU_SOURCE to the matching SDK lib/lilka/src/lilka/menu.cpp.

These review worktrees contain source only; platformio.ini/dependency selection is
not changed or live-resolved. Future explicitly authorized firmware validation must
select this matching SDK revision, not silently use an old SDK sibling. Hardware
brightness, audio latency/clicks, stack headroom, I2S device behavior and persistence
across reboot remain outstanding device gates.
