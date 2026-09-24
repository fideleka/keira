NES emulator usability plan
===========================

Scope
-----

Make the built-in Nofrendo emulator usable without resetting the console while
keeping the standard NES controls unchanged.

Controls
--------

* ``A`` and ``B`` remain the normal NES buttons.
* Holding ``C`` generates Turbo A at approximately 15 presses per second.
* Holding ``D`` generates Turbo B at approximately 15 presses per second.
* ``Select + C`` saves state slot 0 for the current ROM.
* ``Select + D`` loads state slot 0 for the current ROM.
* ``Select`` alone sends an NES Select tap on release.
* ``Start`` without Select sends NES Start immediately while held.
* Pressing ``Start`` while Select is already held reserves both buttons for a
  system chord: releasing either before two seconds takes one screenshot;
  holding both for two seconds exits to the Keira launcher.

Save states
-----------

The first iteration uses Nofrendo's existing SNSS state support and one slot per
ROM. A ROM named ``game.nes`` stores its state as ``game.ss0`` beside the ROM.
Nofrendo's existing on-screen success or error message remains the user
feedback. Multiple slots and a pause menu are intentionally deferred.

Input handling
--------------

Chord actions take priority over individual buttons. Select is held back from
the game until release; a solitary release produces a one-frame tap, while
save/load and screenshot/exit chords consume it. Start reaches the game
immediately if Select was not held when Start was pressed; a later Select press
cannot retroactively turn it into a screenshot or exit chord. Save/load chords
suppress turbo while Select is held and fire once per Select press. Turbo output
is combined with the physical A/B state so a generated turbo release never
releases a physical button that is still held. Starting with C or D can produce
a turbo pulse before Select is pressed, so Select-first is the clean save/load
gesture.

The global screenshot shortcut ignores Select + Start while NES is foreground.
NES requests a screenshot from the service only when its short chord ends, so
the screenshot is taken once and a long exit hold cannot save one accidentally.

Exit and cleanup
----------------

The exit chord is held for two seconds to prevent accidental termination. The
exit path stops audio generation and the emulator timer before requesting
Nofrendo shutdown. After ``nofrendo_main()`` returns, Keira releases the audio
task, timer, mutexes, GUI, video and logging state before the app task exits and
the launcher resumes. Nofrendo's process-global configuration remains alive
because its legacy close routine does not reset its root pointer for reuse.

Cleanup must be idempotent and reset static handles so another ROM can be
started without rebooting.

Validation
----------

* Verify normal A/B and Turbo A/B independently and while mixed.
* Verify Start is sent immediately when pressed alone, including when Select
  is pressed afterwards; no screenshot or exit should result from Start-first.
* Verify a solitary Select press does nothing until release, then reaches the
  game as exactly one tap.
* Save, alter gameplay, load and confirm state restoration.
* Confirm Select-first save/load does not send Select or turbo to the game, and
  pressing both C and D with Select does not trigger either state action.
* Confirm a short Select-then-Start chord takes exactly one screenshot after
  either button is released; a two-second hold exits without a screenshot.
* Reboot and confirm the state file remains loadable.
* Exit and relaunch the same and different ROMs at least 20 times.
* Confirm audio stops immediately and no NES timer remains active.
* Compare free heap before first launch and after repeated exits.
* Confirm launcher, SD, FTP, clock and input remain functional after exit.
* Run localization, formatting, static-analysis and PlatformIO v2 build gates.
* Measure static RAM and flash impact before merging into ``features/stage``.

Known risks
-----------

* Nofrendo currently registers process-exit cleanup but does not invoke it when
  its main loop returns inside Keira.
* Audio runs in a separate FreeRTOS task and must stop before NES state is
  destroyed.
* A brief Select input may reach a game before a save/load chord is recognized;
  device testing will determine whether additional chord suppression is needed.
* Save states are emulator-version-specific and are not guaranteed portable to
  other NES emulators.
