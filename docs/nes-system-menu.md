# NES in-game system menu (feature branch)

## Controls and defaults

Press **Select, then Start** (or both in the same controller sample) to pause.
Release either before two seconds to open the menu; hold both for two seconds
to exit directly to Keira. The game remains paused during chord arbitration. Start-first stays a game Start press, just
like the previous OSD; a solitary Select is still a short tap on release. The old
Select+Start screenshot gesture is replaced; two-second exit is restored.
Select+C save and Select+D load remain; ambiguous Select+C+D is consumed until a
fresh Select press. Menu A activates, B returns/resumes; directions navigate.

Top-level menu: Resume, Reset (Cancel-first confirmation), Screenshot, Controls,
More actions, Exit to Keira. The SDK shows five rows; Down scrolls to Exit, which
remains last. More actions contains Save/Load state, Turbo A/B and GB/GBC Frameskip.
State/reset/screenshot actions close the menu and resume. Reset is a soft reset.
Existing state handlers and state file format/names are unchanged.

Precision mode defaults OFF. When ON it changes **all four D-pad directions**:

1. New direction immediately forwards a short press (one OSD input sample).
2. Continued physical hold is suppressed until the initial delay (default 250ms).
3. After the delay, a continuous NES hold is forwarded, not rapid synthetic repeats.
4. Release, reversal, both-directions conflict, and modal/reset boundaries reset it.

The input sampling interval depends on rendered frames/frame skipping, so the
initial short press can span more than one emulated frame. The game still owns
its repeat/DAS logic: **its inherent additional repeat delay starts after the
continuous hold reaches it**. This feature does not bypass game-specific delay.
Horizontal and vertical axes have independent timing, so diagonals work. Controls provides -50/+50ms adjustments bounded to
50–1000ms; hand-edited valid values between these limits are also accepted.

Turbo A/B default ON to preserve existing C/D functionality. They enable or
suppress C-to-A and D-to-B turbo, respectively (the original two-on/two-off input
sample pulse pattern). Ordinary A/B are always normal held buttons; enabling
these preferences does not turn ordinary A/B into turbo. Save/load chords work
regardless of the turbo toggles.

## Pause and input safety

The menu runs synchronously inside the NES task's OSD input callback, at the
rendered-frame boundary. No CPU frames execute while it runs. All forwarded
joypad buttons are released first. Timer stop plus a timer-daemon semaphore
barrier ensures no timer callbacks accrue emulation ticks during the modal wait;
resume resets the timer period. No SDK/core changes or forced task suspension.

Audio pause is set under the existing sound mutex after any active audio chunk
finishes; DMA is zeroed. The audio task remains alive and sleeps at its usual
cadence, skips APU processing while paused, and continually resets its scheduling
baseline (no catch-up burst). I2S writes now have a bounded 50ms wait and break on
failed/zero-byte output, preventing a stuck write from trapping the sound mutex.
State/reset handlers take the same mutex while audio is paused. Exit uses the
existing orderly shutdown path.

The chord, every submenu boundary, confirmation, and resume are separated by a
sleeping **all physical buttons released** gate; button edge state, turbo phases,
and direction timing are cleared. Holding menu A, a direction, or only one
chord button cannot leak into gameplay or activate the next modal page.
Screenshot is the second entry, visible without scrolling. SDK Menu displays only
five entries (itemsY=80, 32px rows), scrolling with Down/Up or Right/Left pages;
Screenshot previously sat below the initial viewport. No competing shortcut added.
The next complete game frame is copied before queueDraw swaps buffers. Encoding
and file writing remain asynchronous, but cannot race a later menu/reset/exit.
Busy/allocation failure produces an error toast. Both canvases are cleared at
modal completion, and each game blit clears its entire canvas before drawing.
This fixes untouched side borders and stale alternate double-buffer contents.
Interlaced builds render complete game rows; physical display interlacing remains
unchanged, so both fields settle over the next two presentations.

## Per-ROM preferences

Same-directory sidecar, replacing only the final basename extension:

- /sd/ROMs/Magic Jewelry.nes -> /sd/ROMs/Magic Jewelry.conf
- /sd/ROMs/Game.v1.NES -> /sd/ROMs/Game.v1.conf

Example (defaults):

    version=1
    precision_mode=0
    direction_delay_ms=250
    turbo_a=1
    turbo_b=1

Reader: maximum 512 bytes, maximum 512-byte path buffers, numeric values only,
booleans 0/1, delay 50–1000ms. Blank lines, # comment lines, LF/CRLF allowed.
Version is mandatory; omitted setting keys use defaults. Duplicate/unknown keys,
embedded NUL, invalid numbers, missing version and oversized files are rejected
as a whole. No partially-applied malformed preferences.

Missing file quietly uses defaults. Invalid/unreadable/unsupported config shows
an on-screen notice and serial warning, uses defaults, and **refuses writes** to
that file. This deliberately protects even an oversized future file whose
version is beyond the read bound. Remove/repair such a file explicitly outside
firmware if desired; settings remain useful for the current session.

Writes happen only on actual settings changes, never per-frame/on resume/exit.
Write .conf.tmp, check write/flush/fsync/close, rename old .conf to .conf.bak,
promote .tmp, then remove .bak. Failed promotion attempts rollback; if rollback
also fails the previous bytes remain at .bak. Loader can use that backup when the
main file is absent, warns, and blocks further writes until manually recovered.
Any leftover .bak (including failed cleanup) is protected, never overwritten.
This backup-assisted transaction accommodates FAT rename-to-existing behavior;
it is **not a claim of filesystem/power-loss transactional atomicity**. An abrupt
power loss can leave .bak or .tmp; firmware preserves .bak and warns rather than
silently replacing it. Write failure is visible, and session settings are kept.
Conf files and .tmp/.bak never share state/save filename extensions.

## Reproducible host checks

    g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -Isrc \
      tests/nesmenu.cpp src/apps/nes/preferences.cpp -o /tmp/keira-nesmenu-tests
    /tmp/keira-nesmenu-tests
    python3 tests/nesmenu_input.py

Portable tests cover defaults, parser bounds/version/path, timing/reversal/
release/wrap/reset, release gate and write/open/flush/rename failure preservation.
The input harness compiles the actual OSD input code with event/controller shims:
solitary Start/Select, Start-first safety, consumed/ambiguous save/load chords,
no modal input leak, C turbo toggles, ordinary A and both precision axes.
These shims do not prove real audio/timer scheduling or physical gameplay.

## Physical device acceptance checklist (NOT performed)

- Launch Magic Jewelry and an action game; verify config-free defaults and
  ordinary A/B/Up/Down/Select/Start are unchanged.
- Select-first Start pauses immediately; short release opens menu, hold both
  for >=2s exits cleanly without screenshot. Start-first remains game Start. Hold extra A/direction at entry
  and exit: no game leak, accidental submenu activation, or stuck joypad.
- Leave menu open for at least 60s: game is frozen, audio silent, then resume
  without sped-up CPU frames/audio burst, watchdog reset, or heap loss. Repeat
  50 menu cycles and inspect task stack/heap watermarks.
- Precision OFF unchanged. ON: initial short Up/Down/Left/Right moves once, pre-delay
  hold suppressed, after delay continuous game hold. Test reversal/release,
  both held, Down+Left, min/max delay. Allow game's own additional repeat delay.
- C/D turbo each ON/OFF; ordinary A/B always work. Save/load quick chords still
  fire once and never produce Select/turbo; ambiguous three-button chord does not.
- Save/load from menu restores state; Reset Cancel/B leaves game intact; confirm
  resets; Screenshot file shows game, not menu; Exit returns cleanly to launcher.
- Settings round-trip on relaunch independently for two ROMs; multiple dots/spaces
  preserved; state/save files unaffected. No config writes from idle/resume/exit.
- Test malformed/oversized/version2/read-only/full-card with disposable copies:
  visible warning, safe defaults/session settings, unknown/prior config preserved.
  Inspect SD FAT promotion/rollback/backup recovery behavior separately.

No ROMs/saves were altered during host testing. No flashing is authorized here.

## GB/GBC parity

Both extensions dispatch to common GameBoyApp and use EmulatorMenuApp shared
paused UI/preferences. Same Select-first Start safety, solitary Select-on-release,
C/D state chords and ambiguous-chord consumption as NES. The old short screenshot gesture is replaced by menu-on-release; long hold exits. Menu CPU/audio work is synchronous in the game task:
no core frames or audio chunks execute while paused, DMA is zeroed, deadlines and
input/turbo/filter state are reset on resume. Ordinary A/B remain normal held keys.
C->A and D->B turbo default ON, two emulated frames on / two off, including skipped
LCD frames. The active-low adapter and core/save formats remain unchanged.
Reset calls the existing Gnuboy soft-reset API without recreating a cartridge or
writing launch history. Battery saves stay ROM.sav, snapshots ROM.ss0;
preferences replace only the final extension with .conf, same version 1 keys and
protected FAT backup recovery. Precision defaults OFF, delay 250ms, both turbo ON.
Frameskip is explicit menu AUTO/OFF (session-only, not an extra version-1 key).
AUTO remains the device-accepted default: 16743us deadlines, 1500us tolerance,
at most two consecutive LCD skips, CPU/audio always run. This checkout had no C
frameskip toggle to retain; C is now turbo and never changes speed implicitly.
GameBoy keeps its 240x216 bounded presentation, with all canvas pixels regenerated.

Additional physical checks: repeat checklist for .gb and .gbc; verify RGB palettes,
active-low controls, C/D cadence during automatic skips, AUTO default speed/audio,
manual OFF, .sav battery persistence and .ss0 state roundtrip, reset without history
writes, 60-second silent pause/no catchup, and screenshot then immediate reopen /
exit still saves the copied game frame. No physical checks or flashing performed.

## Exit/discoverability/localization follow-up

The initial menu had nine rows but Lilka Menu exposes only five at once. Both
Screenshot and Exit were below the initial viewport; scrolling existed, but there
was no obvious exit on page one. Root menu now has exactly five rows, with Exit
third and Screenshot second. Hold-to-exit and explicit menu Exit use identical
existing orderly NES shutdown / GB battery-save cleanup paths. Start-first remains
ordinary gameplay Start; consumed/ambiguous state chords cannot become menu/exit.
No screenshot is requested on long exit or Reset. A short chord does not forward
Start/Select to gameplay; release gates consume remaining physical buttons.

Every shared menu/options/confirmation/config notice, status postfix and paused
title uses K_S_EMU_* localization keys. Ukrainian (default) and English headers
contain the complete same key set; no other locales exist in this checkout.
Run python3 tests/menu_access.py for actual shared UI action routing, five-entry
accessibility, cancel-first reset, short/long/rollover hold tests in both locales
(normal and ASan/UBSan), plus the repository localization consistency gate.
Physical follow-up: in Ukrainian and English, confirm first-page Screenshot and
Exit visible, short release opens menu and >=2s chord exits NES/.gb/.gbc, with
no stray screenshots or game Start/Select, and long exit preserves GB battery RAM.
