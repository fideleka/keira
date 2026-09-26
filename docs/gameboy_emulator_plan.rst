Game Boy / Game Boy Color emulator plan
=======================================

Status
------

First source milestone on ``feature/gameboy-emulator``. Keira now dispatches
``.gb`` and ``.gbc`` from File Manager to a Walnut-CGB-based app. It loads ROMs
into PSRAM, maps D-pad/A/B/Start/Select, draws at 240×216, returns to Keira on
a 1.5-second Select+Start hold, and loads/writes cartridge RAM beside the ROM
as ``game.gb.sav`` or ``game.gbc.sav``. **Audio is not connected yet.** No
firmware build or device test has been done; do not merge into
``features/stage`` yet.

This first adapter keeps each ROM in PSRAM for fast reads. Large cartridges may
be rejected when Keira cannot reserve enough contiguous PSRAM; SD-backed ROM
paging is not implemented. Existing battery RAM is loaded on launch and saved
on the normal exit gesture. Forced power loss before exit is not yet covered.

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
* GB and GBC both render 160×144. Start with a centered, aspect-preserving
  240×216 nearest-neighbor view on the 280×240 Lilka screen, leaving margins
  around the rounded corners. Offer native 160×144 later only if useful.
* Map D-pad, A, B, Start, and Select directly. Reserve C/D for optional
  actions; choose an exit gesture that does not collide with game input or
  Keira's existing screenshot/exit behavior.
* Use the established display, input, and audio paths where possible. Measure
  frame pacing, display transfer time, audio underruns, heap/PSRAM use, and
  input latency on hardware. Avoid assuming CPU speed alone proves 60 fps.
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
