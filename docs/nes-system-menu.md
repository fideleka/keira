# NES in-game system menu (feature branch)

## Controls and defaults

Press **Select, then Start** (or both in the same controller sample) to pause and
open the existing Lilka Menu widget. Start-first stays a game Start press, just
like the previous OSD; a solitary Select is still a short tap on release. The old
Select+Start screenshot/2-second exit gesture is replaced, not duplicated.
Select+C save and Select+D load remain; ambiguous Select+C+D is consumed until a
fresh Select press. Menu A activates, B returns/resumes; directions navigate.

Menu: Resume, Controls, Turbo A (C), Turbo B (D), Save state, Load state,
Screenshot, Reset (Cancel-first confirmation), Exit to launcher.
State/reset/screenshot actions close the menu and resume. Reset is a soft reset.
Existing state handlers and state file format/names are unchanged.

Precision mode defaults OFF. When ON it changes **only Left/Right**:

1. New direction immediately forwards a short press (one OSD input sample).
2. Continued physical hold is suppressed until the initial delay (default 250ms).
3. After the delay, a continuous NES hold is forwarded, not rapid synthetic repeats.
4. Release, reversal, both-directions conflict, and modal/reset boundaries reset it.

The input sampling interval depends on rendered frames/frame skipping, so the
initial short press can span more than one emulated frame. The game still owns
its repeat/DAS logic: **its inherent additional repeat delay starts after the
continuous hold reaches it**. This feature does not bypass game-specific delay.
Down and Up are unchanged. Controls provides -50/+50ms adjustments bounded to
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
Screenshot requests are deferred until the next game blit, rather than capturing
the menu; the existing screenshot service performs capture asynchronously.

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
no modal input leak, C turbo toggles, ordinary A and unchanged Down.
These shims do not prove real audio/timer scheduling or physical gameplay.

## Physical device acceptance checklist (NOT performed)

- Launch Magic Jewelry and an action game; verify config-free defaults and
  ordinary A/B/Up/Down/Select/Start are unchanged.
- Select-first Start opens menu immediately; hold both for >2s: no old exit or
  screenshot. Start-first remains game Start. Hold extra A/direction at entry
  and exit: no game leak, accidental submenu activation, or stuck joypad.
- Leave menu open for at least 60s: game is frozen, audio silent, then resume
  without sped-up CPU frames/audio burst, watchdog reset, or heap loss. Repeat
  50 menu cycles and inspect task stack/heap watermarks.
- Precision OFF unchanged. ON: initial short left/right moves once, pre-delay
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
