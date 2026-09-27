Game Boy / Game Boy Color emulator plan
=======================================

Status
------

Experimental implementation on ``feature/gameboy-emulator``. Keira dispatches
``.gb`` and ``.gbc`` from File Manager to an in-Keira emulator app. This
local spike substitutes the ESP32-oriented Gnuboy core for Walnut-CGB while
retaining the app, display scaling, controls, and save-file interface. It loads ROMs
into PSRAM, maps D-pad/A/B/Start/Select, draws a centered 240×216 area-weighted
image, returns to Keira on a 1.5-second Select+Start hold, and loads/writes
cartridge RAM beside the ROM as ``game.gb.sav`` or ``game.gbc.sav``. Gnuboy's
own APU supplies stereo samples to Lilka's I2S output at Keira's saved volume.
**The new core and its audio are not device-verified yet.** Anton has
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
Start did not skip an intro under Walnut; its joypad patch resolved that on
device. The new Gnuboy adapter maps physical buttons to Gnuboy's pad handler;
Start and the other controls must be retested. Whether the new renderer resolves
startup-logo artifacts also needs a device test.

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
Walnut-CGB was initially selected for its MIT-licensed, single-header GB/GBC
core and small integration surface. Device logs then showed 27–31 fps, with
26–31 ms of core CPU time after subtracting the scaler. This local experiment
uses the ESP32-oriented Gnuboy core from retro-go under GPLv2; imported source,
license, and provenance are in ``lib/Gnuboy``. It is not an upstream Keira PR,
and no ROMs are included. A synthetic host comparison with video enabled was
about 0.11 ms/frame for Gnuboy versus 0.21 ms/frame for Walnut; that is **not**
an ESP32 or Bomberman speed measurement.

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
* Map D-pad, A, B, Start, and Select directly. Select+Start held for 1.5
  seconds exits without a screenshot; a short press/release requests one, as
  in Keira's NES app. C toggles automatic frameskip so Anton can compare
  full-visual and speed-prioritised modes without another firmware build.
  D remains available for a later control.
* Use the established display, input, and audio paths where possible. Measure
  frame pacing, display transfer time, audio underruns, heap/PSRAM use, and
  input latency on hardware. Walnut's last device log showed 27–31 fps,
  ~26–31 ms of core work plus ~4.5 ms of scaling, and ~18 ms per SPI transfer.
  The Gnuboy wrapper keeps the same scaler and smaller 240×216 display canvas,
  but its core uses direct memory maps and less frequent timer updates.
  ``GB perf [Gnuboy]`` and ``GB display`` must establish the actual device
  improvement. The first Gnuboy log showed ~40–41 emulated fps, ~16–18 ms of
  core work, ~4.6–5.3 ms of scaling, ~1–3 ms of audio/queue work, and ~18 ms
  per SPI transfer. Adaptive frame skipping now lets Gnuboy emulate without
  LCD work when a rendered frame misses its deadline, while preserving input,
  game logic, and audio. It shows at least one frame out of every three. The
  next log distinguishes emulated fps, queued visual fps, and actual displayed
  fps. This is a speed-versus-visual-smoothness tradeoff, not proof of full
  speed. The ST7789 still has no proven frame-sync mechanism on Lilka.
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
