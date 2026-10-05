# SDK global adjustment integration

Use the matching Lilka SDK system-adjustment-shortcuts source branch. Global
Select-first arbitration is owned solely by the SDK controller; Keira installs no
parallel shortcut handler. Ordinary Select, Select+Start pause/exit, per-axis NES/GB
precision delays and turbo mapping are unchanged.

- Volume rises by 1 below 5, then by 5 (0→1→2→3→4→5→10→15); down remains -5,
  clamped to 0..100 with mute. Taps and holds share this rule, repeat at 400/100 ms,
  and is saved after 600 ms without a change by the SDK settings task.
- Shortcut directions never reach Keira polling or callback consumers and remain
  suppressed until physical release, including Select-first-release cases.
- **Brightness is hardware-blocked:** Select+Left/Right remains ordinary application input;
  it does not dim Lilka v2. Its only exposed backlight/sleep line also controls the
  I2S/MAX98357 module. No shared-pin PWM or fake brightness setting
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

## Centered volume feedback

AppManager's existing display/render owner presents a 75%-width, centered 76px-high
black panel with a double white border, 22px-thick progressbar (cyan fill), and
unscaled regular SDK FONT_10x20 percent/MUTE text (10px character advance). Every shortcut tap/repeat renews the 1200ms timeout,
including limit hits; opposite directions do not trigger feedback. Rotation uses
current display dimensions. No font/cursor state is modified.

The render task reads one coherent SDK RAM snapshot per 60Hz presentation tick.
The shared SDK transaction excludes the opaque feedback footprint from background
clears and canvas transfers, preserving interlaced fields outside it. Changed
feedback is rasterized into bounded RAM scanlines and sent through one LCD window;
unchanged feedback is not resent. Static scenes send no LCD pixels. On expiry the
retained statusbar/foreground layers and black letterbox margins are composed
before restoring each affected pixel once, including paused/static screens. App
switches, rotation and dirty backgrounds force appropriate outside-panel redraws.
No app/front/back/screenshot canvas contains the overlay, and screenshots remain
clean. NES customBlit and GB/GBC scanline framebuffers already submit via queueDraw;
they need no emulator-side SPI drawing or overlay mutation.

Coverage: all built-in Keira apps/menus using AppManager, fullscreen/bounded
framebuffers, NES normal/interlaced paths, GB/GBC and paused system menus. No new
background display task or shared SPI writer exists. Apps that bypass AppManager
and write SPI directly cannot be made safe by this hook and are not covered.
Device flicker, frame rate and stack headroom under full-frame presentation remain
unmeasured; the extra work lasts only while feedback is active plus one expiry tick.

## Host verification (no firmware build)

Run python3 tests/system_shortcuts.py --sdk /path/to/matching/sdk-worktree. It
compiles actual GB/NES/tracker output functions, SDK PCM scaling, AudioPlayer's
actual loop gain expression and existing per-axis DirectionFilter against host
boundaries, in C++11 normal and ASan/UBSan modes. Binaries use temporary directories.
Run python3 tests/volume_overlay.py --sdk /path/to/matching/sdk-worktree for actual
SDK presentation methods and AppManager run/renderToCanvas under pixel/lock stubs,
normal and ASan/UBSan: centered 280x240/240x280 geometry, bar fractions/clamps,
paused menu/fullscreen/bounded/interlaced presentation, clean expiry, immutable
canvases and overlay-free screenshots. Custom emulator queueDraw coverage is also
source-checked. Existing tests/nesmenu_input.py, gameboy_input.py, emulator_menu.py, menu_access.py,
menu_render.py and navigation/run.py remain applicable. For navigation, set
LILKA_SDK_MENU_SOURCE to the matching SDK lib/lilka/src/lilka/menu.cpp.

These review worktrees contain source only; platformio.ini/dependency selection is
not changed or live-resolved. Future explicitly authorized firmware validation must
select this matching SDK revision, not silently use an old SDK sibling. Hardware
brightness, audio latency/clicks, stack headroom, I2S device behavior and persistence
across reboot remain outstanding device gates.

### Regular overlay font regression

The overlay uses the maintained U8g2 u8g2_font_10x20_t_cyrillic
asset (the SDK's FONT_10x20), not enlarged custom block glyphs. A private,
stack-local decoder sends clipped horizontal spans to off-screen RAM targets;
scanline targets clip to one row. Application font/cursor state, canvases and
screenshots remain untouched, and only final panel rows reach the LCD.

Run python3 tests/volume_overlay.py --sdk ../sdk-system-shortcuts. It compiles
the real U8g2 font decoder, line clipping and exact regular font asset from the
existing read-only ../lilka-sdk/lib/lilka/.pio/libdeps/v2/U8g2/src/clib dependency.
Use --u8g2 /existing/path/to/clib or U8G2_CLIB elsewhere; the test never
creates .pio or fetches dependencies. Normal, ASan and UBSan runs compare
regular text pixels with independent U8g2 rendering and a pinned M bitmap,
including every digit, percent and MUTE, both orientations, guarded rows,
source/screenshot immutability and final-only LCD transfers. The intentional
intermediate-write mutation must still fail.

The persistent presentation state stays 624 bytes on the host (560-byte row).
The font context is 248 bytes on this 64-bit host, temporarily on the stack;
there is no per-frame heap allocation or additional framebuffer. The existing
font asset is 6979 bytes, referenced rather than copied into production source.
A changed panel decodes at most four glyphs on each of its 20 text-band rows;
unchanged feedback does no rasterization/transfer. The test prints a 1000-panel
host timing, not an ESP32/SPI benchmark or embedded stack/flash size claim.
Firmware builds, device timing and stack watermark checks remain unperformed.
