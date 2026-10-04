# Emulator launch-history regression

Run from the repository root:

    python3 tests/emulator_history/run.py

For a source-only worktree, point --source at an existing verified
arduino-nofrendo/src dependency. The script verifies the pinned manifest first.
It compiles C++11 normal and ASan/UBSan binaries in temporary directories.

## Coverage

The fixture executes the actual NesApp declaration, constructor, destructor,
run and rememberSuccessfulLaunch methods, the actual osd_setsound callback,
and the pinned Nofrendo nofrendo_main/main_loop/internal_insert/nes_emulate
startup control flow. Core load/mapper and RTOS/audio boundaries are mocked;
Preferences storage and filesystem availability are the existing navigation
fixtures. The real recentroms.cpp writer/parser are used and writes counted.

- History is present during gameplay before any clean exit or cleanup.
- Simulated power loss bypasses run cleanup; a fresh reader sees the new order.
- Log/config/OSD/GUI/video/core/ROM/mapper/timer startup errors leave all stored
  history byte-for-byte unchanged (including order).
- Repeated runtime setup/reset callbacks and clean exit do not cause a second
  write. Each fresh NesApp session gets its own guard.
- GB/GBC execute their existing production run startup prefix: ROM, core and
  save-allocation failures leave history unchanged; successful launches persist
  before gameplay, including the intentionally nonfatal audio fallback.
- Other ROM systems and guest firmware entries remain unchanged.

The companion tests/navigation/run.py suite covers 20-entry history limits,
3999-byte NVS bounds, path validation, deduplication and Applications refresh.

## Audited launch scope

- src/keira/keira.h dispatches NES (.nes/.rom) to NesApp and GB/GBC to the same
  GameBoyApp. FileManager automatic/Open With dispatch and launcher folders converge
  on these handlers; there is no alternate built-in emulator launch path.
- NES previously wrote only after nofrendo_main returned zero. The fix records
  at osd_setsound, called at entry to nes_emulate after all fatal core startup
  checks, with one guard per NesApp. It is not a frame/render/input hook.
- GB/GBC already write once after loadRom, gbcore_create and loadSave succeed,
  before their long-running loop. No production change is necessary.
- Applications reads the same recent_nes/recent_gb/recent_gbc keys under the
  keira namespace; its refresh/reboot behavior and history helper are unchanged.
- External ScummVM/Doom are guest .bin firmwares, not built-in emulators.
  MultiBootApp and guestshortcuts.cpp already persist firmware shortcuts before
  reboot, not at guest exit. Keira cannot inspect guest game initialization.
  No game-level history for these external firmwares exists here.
- Existing multiboot limitation: the upload path records guest_fw after process
  success but before SDK finishAndReboot (OTA finalize/boot selection can still
  fail). This is a separate firmware-install shortcut policy, not this NES
  exit-only regression. It is left unchanged; no guest-success claim is made.

No SDK/dependency changes, physical ROM/save writes, flash or staging merge.
Physical hardware reset and real NVS power-loss behavior remain unverified.
The existing void history helper still does not report NVS write failures.

## NES menu compatibility

feature/nes-ingame-menu at 59b0dc4 is separate. The new method/member are additive
and the OSD call follows the existing audio-task setup. Retain both when merging;
remove the old exit-only history call once. Menu reset/load/save actions must not
clear launchRemembered or write history. Re-run both branches' suites and build
an isolated combined integration tree before merging features/stage.
