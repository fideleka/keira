Game Boy / Game Boy Color emulator plan
=======================================

Status
------

Experimental implementation on ``feature/gameboy-emulator``. Keira dispatches
``.gb`` and ``.gbc`` from File Manager to a Walnut-CGB-based app. It loads ROMs
into PSRAM, maps D-pad/A/B/Start/Select, draws a centered 240×216 area-weighted
image, returns to Keira on a 1.5-second Select+Start hold, and loads/writes
cartridge RAM beside the ROM as ``game.gb.sav`` or ``game.gbc.sav``. Audio
register reads/writes feed the bundled MiniGB APU and its stereo samples go to
Lilka's I2S output, with
Keira's saved volume level. **Audio is not device-verified yet.** Anton has
tested an earlier revision on-device and reported Bomberman GBC as much slower
than his reference device. The latest input, scaling, and speed changes have
not been retested. The assistant has not run a firmware build; do not
merge into ``features/stage`` yet.

This first adapter keeps each ROM in PSRAM for fast reads. Large cartridges may
be rejected when Keira cannot reserve enough contiguous PSRAM; SD-backed ROM
paging is not implemented. Existing battery RAM is loaded on launch and saved
on the normal exit gesture. Forced power loss before exit is not yet covered.

Anton reported inverted/stuck game input, screenshot capture during long-press
exit, unpleasant scaling, and artifacts in some startup logos. The first three
have direct source fixes on this branch. A later Bomberman GB report showed
Start did not skip an intro; Walnut's joypad register was updated only on game
writes, so the adapter now refreshes it on physical input changes and the
local core patch handles row selection and joypad interrupts correctly. The
game area is cleared before each emulated frame to avoid stale rows while the
LCD is disabled or a startup frame is incomplete. Whether that fully resolves
the logo artifacts still
requires another device test; Walnut's line renderer may have other accuracy
limits for individual games.

Goal and first milestone
------------------------

Open user-supplied ``.gb`` and ``.gbc`` ROMs from Keira's File Manager and run
them on Lilka v2. The first playable milestone should cover one GB and one
GBC title with picture, controls, audio, a reliable exit to Keira, and
battery-backed saves across restart. Confirm actual frame rate and audio
behavior on the device before claiming full-speed emulation.

Existing-solutions preflight
----------------------------

Keira already integrates NES through Nofrendo, but this checkout has no GB/GBC
emulator core. Prefer adapting an existing core rather than writing an emulator.
Walnut-CGB was selected for its MIT-licensed, single-header GB/GBC core and
small integration surface. The imported source and license are under
``src/apps/gameboy/vendor`` with provenance in ``UPSTREAM.md``. ESP32-oriented
Gnuboy alternatives exist, but the reviewed copies are GPLv2 and require a
larger port. No ROMs are included.

Proposed integration
--------------------

* Prefer an in-Keira ``App`` like NES: File Manager association for ``.gb`` and
  ``.gbc``, no guest-firmware copy or reboot for each launch. Reassess this if
  the chosen core cannot fit Keira's flash/RAM budget cleanly.
* GB and GBC both render 160×144. The current view is a centered 240×216
  area-weighted 1.5× scaler on the 280×240 Lilka screen. Each two source pixels
  become their two original colours with a blended pixel between; adjacent
  scanlines use the same method. It avoids the uneven block widths of the
  earlier nearest-neighbor scaler while remaining larger than native size.
* Map D-pad, A, B, Start, and Select directly. Reserve C/D for optional
  actions. Select+Start held for 1.5 seconds exits without a screenshot; a
  short press/release requests one, as in Keira's NES app.
* Use the established display, input, and audio paths where possible. Measure
  frame pacing, display transfer time, audio underruns, heap/PSRAM use, and
  input latency on hardware. Avoid assuming CPU speed alone proves 60 fps.
  After Anton reported slow Bomberman GBC gameplay, the adapter prefers
  internal RAM for the hot emulator state (with PSRAM fallback), uses Walnut's
  faster dual-fetch CPU loop, and clears only scanlines missing from a frame.
  A serial ``GB perf`` line every 120 emulated frames reports core+video time,
  audio+draw-queue time, total active frame time, observed emulation FPS, and
  whether internal RAM was available. The line does **not** time the separate
  AppManager SPI display transfer. Compare observed emulation FPS with the
  59.7 fps target: if emulation is near target but the visible image still
  lags, measure that display path next.
* Store per-ROM battery RAM on SD and flush it safely on normal exit. Check
  mapper and RTC behavior (especially MBC3) separately; do not promise every
  cartridge type in the first milestone.

Implementation order and release gate
-------------------------------------

1. Verify candidate cores and licenses; pick one with GB and GBC support.
2. Integrate one ROM launch, frame output, controls, and clean return on a
   feature branch. Source is now in place, but hardware validation is pending.
3. Add audio, frame pacing, and per-ROM battery saves; then test GB and GBC on
   device with user-supplied compatible ROMs.
4. Measure flash/RAM and gameplay performance, fix concrete issues, and only
   then consider a merge into ``features/stage``.

Do not build or package firmware as a routine assistant check; Anton builds on
his computer before uploading. Source-only review is the default unless a
specific blocker makes a build essential or Anton requests one.
