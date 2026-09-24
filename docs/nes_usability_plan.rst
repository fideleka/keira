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
* Holding ``Select + Start`` for one second exits to the Keira launcher.

Save states
-----------

The first iteration uses Nofrendo's existing SNSS state support and one slot per
ROM. A ROM named ``game.nes`` stores its state as ``game.ss0`` beside the ROM.
Nofrendo's existing on-screen success or error message remains the user
feedback. Multiple slots and a pause menu are intentionally deferred.

Input handling
--------------

Chord actions take priority over individual buttons. Save/load chords suppress
turbo while active and fire only once per press. Turbo output is combined with
the physical A/B state so a generated turbo release never releases a physical
button that is still held.

Exit and cleanup
----------------

The exit chord is held for one second to prevent accidental termination. The
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
* Save, alter gameplay, load and confirm state restoration.
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
