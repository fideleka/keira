ScummVM manager and engine firmware plan
========================================

Status
------

This document records the implementation decisions for running ScummVM games
as Lilka multiboot applications.  It is a design document, not an indication
that the feature is implemented.

Goals
-----

* Open a ``.scummvm`` manifest directly from Keira's file manager, much like
  opening a ROM.
* Keep the game data, support files, configuration, and saves on the SD card.
* Build one small guest firmware per ScummVM engine instead of trying to fit
  every engine in one image.
* Flash only the engine required by the selected game into Lilka's guest OTA
  partition.
* Start the selected game directly, without making the user browse for it a
  second time inside ScummVM.
* Return to Keira when the game exits.
* Avoid changing Lilka's partition table.

Non-goals for the first version
-------------------------------

* Automatically detecting arbitrary ScummVM games from their data files.
* Supporting every ScummVM engine.
* Bundling copyrighted game data.
* Running the manager as a separate guest firmware.
* Loading native engine code dynamically from SD into PSRAM.
* Modifying the Keira launcher or File Manager beyond the narrowly required
  file association and manager entry point.

Architecture
------------

Lilka v2 has two 6.25 MiB application partitions.  Keira occupies ``app0``
and a multiboot guest occupies ``app1``::

    app0: Keira
      |
      +-- ScummVM manager (normal Keira app)
            |
            +-- read .scummvm manifest
            +-- choose engine binary
            +-- pass launch request through RTC memory
            +-- flash app1 when required
            `-- reboot

    app1: one ScummVM engine firmware
      |
      +-- read launch request
      +-- mount SD
      +-- start selected game
      `-- restart on exit -> bootloader rollback -> Keira

The manager must run inside Keira.  A manager running from ``app1`` cannot
safely overwrite its own running partition with the selected engine.  A guest
manager would require an unnecessary manager -> Keira -> engine two-reboot
handoff.

Engine firmware images
----------------------

Each guest image contains the shared ESP32-S3 backend and exactly one ScummVM
engine.  Initial engine identifiers and binary locations are fixed by Keira::

    scumm     /sd/scummvm/engines/scumm.bin
    agi       /sd/scummvm/engines/agi.bin
    sky       /sd/scummvm/engines/sky.bin
    queen     /sd/scummvm/engines/queen.bin
    dreamweb  /sd/scummvm/engines/dreamweb.bin

The first proof build targets only ``scumm``.  The engine image must be a raw
ESP-IDF application image, not a merged image containing a bootloader,
partition table, or filesystem.

Every engine image must fit in ``0x640000`` bytes (6.25 MiB).  The inspected
T-Deck build is about 7.22 MiB because it includes five engines and HE; it is
therefore not directly usable but is a suitable porting base.

The initial manager uses a compiled engine registry instead of allowing a
manifest to provide an arbitrary firmware path.  This prevents a malformed
manifest from turning a game launch into unrestricted firmware execution and
keeps engine upgrades centralized.

SD card layout
--------------

Recommended layout::

    /sd/scummvm/
    |-- engines/
    |   |-- scumm.bin
    |   |-- agi.bin
    |   |-- sky.bin
    |   |-- queen.bin
    |   `-- dreamweb.bin
    |-- data/
    |   |-- themes/
    |   `-- engine-data/
    `-- saves/

    /sd/games/scummvm/
    |-- Monkey Island/
    |   |-- Monkey Island.scummvm
    |   |-- 000.lfl
    |   `-- ...
    `-- Kings Quest I/
        |-- Kings Quest I.scummvm
        `-- ...

Engine support data may later be split into per-engine folders, but game
manifests must not depend on a private LittleFS partition.  Keira's SPIFFS
partition remains owned by Keira.

Manifest format
---------------

``.scummvm`` files are UTF-8 JSON documents.  Version 1 example::

    {
      "schema": "keira-scummvm-v1",
      "title": "The Secret of Monkey Island",
      "engine": "scumm",
      "gameId": "monkey",
      "path": ".",
      "language": "en",
      "platform": "pc"
    }

Required fields
~~~~~~~~~~~~~~~

``schema``
    Must be exactly ``keira-scummvm-v1``.

``title``
    Human-readable game title.

``engine``
    Identifier from Keira's compiled engine registry.

``gameId``
    ScummVM game identifier accepted by the selected engine.

``path``
    Game-data directory.  Relative paths are resolved relative to the
    manifest's directory.  ``.`` therefore makes a game folder portable.

Optional fields
~~~~~~~~~~~~~~~

``language``
    ScummVM language code, such as ``en`` or ``de``.

``platform``
    ScummVM platform identifier, such as ``pc`` or ``amiga``.

``description``
    Optional descriptive text for the future library view.

``icon``
    Optional path, relative to the manifest, for a future game icon.

Unknown fields are ignored for forward compatibility.  Unknown schemas,
engines, or invalid required fields fail closed with a readable error.

Path rules
~~~~~~~~~~

* Resolve and normalize paths before use.
* The resolved game path must remain under ``/sd``.
* Reject missing files, directories that cannot be opened, overlong strings,
  and manifests above a small bounded size.
* Do not execute firmware paths supplied by the manifest.
* Paths containing spaces must work.

File Manager integration
------------------------

Add a narrowly scoped ``FT_SCUMMVM`` file type:

* recognize the ``.scummvm`` extension case-insensitively;
* assign a dedicated icon when one becomes available;
* dispatch opening to ``ScummVMManagerApp(manifestPath)``;
* preserve existing behavior for every other file type.

The direct-open path is the first UI.  A standalone ScummVM library app may
later scan the SD card for manifests and call the exact same parser and launch
service.

Launch handoff
--------------

Keira already carries a 1024-byte CRC-protected command buffer in RTC memory
for multiboot.  The manager should use ``lilka::multiboot.setCMDParams()`` to
pass a compact launch token before rebooting.

The existing argument parser does not understand quoted paths.  The initial
handoff therefore passes a URL-safe encoded manifest path without spaces, for
example::

    /sd/scummvm/engines/scumm.bin manifest=<base64url-path>

The guest backend implements the small compatible RTC-buffer reader, verifies
the CRC, decodes the manifest path, then clears or invalidates the request so
it is not processed again after returning to Keira.  The guest reopens and
validates the manifest rather than trusting unvalidated launch values.

An alternative fixed binary structure may replace the encoded command in a
later protocol version, but it must retain a version and integrity check.

Engine selection and caching
----------------------------

Prototype behavior is deliberately simple and safe: always flash the selected
engine, then boot it.

Caching is a later optimization.  Keira may boot the existing ``app1`` image
without rewriting it only when centrally maintained metadata proves that it
matches the selected engine binary.  The cache identity should include at
least:

* canonical engine-binary path;
* file size;
* content digest or build identifier.

Cache metadata must be updated or invalidated by every Keira path that writes
a multiboot binary, not only by the ScummVM manager.  Otherwise launching an
unrelated ``.bin`` could leave stale metadata and cause Keira to boot the wrong
guest image.

Guest firmware responsibilities
-------------------------------

The Lilka ScummVM backend is based on
``varna9000/scummvm-tdeck`` and must:

* build with ESP-IDF for ESP32-S3, QIO flash, and 8 MiB octal PSRAM;
* use Lilka's 280x240 landscape ST7789 display and shared SPI bus;
* mount the physical SD card at ``/sd``;
* use no private LittleFS partition;
* read themes, engine data, configuration, games, and saves from SD;
* map Lilka controls to pointer, clicks, shortcuts, and virtual keyboard;
* offer a silent audio backend for Lilka units without MAX98357A;
* optionally support Lilka's I2S pins (BCLK 42, LRCK 1, data 2);
* start the requested game directly;
* call ``esp_restart()`` on exit;
* never mark the guest OTA image valid, allowing rollback to Keira.

Initial control mapping
-----------------------

Suggested first mapping::

    D-pad            Move pointer
    A                Left click
    B                Right click / Escape, selected by context
    C                F5 / game menu
    D                F7 / load shortcut
    Start            Enter
    Select           Virtual keyboard
    Select + Start   Exit to Keira, with hold confirmation

Pointer acceleration is required: fine motion for taps and faster motion while
a direction remains held.  Exact B/C/D behavior should be tested with early
SCUMM games before being treated as final.

Configuration and saves
-----------------------

All engine binaries share:

* ``/sd/scummvm/scummvm.ini`` for configuration;
* ``/sd/scummvm/saves`` for save files;
* ``/sd/scummvm/data`` for common themes and engine data.

Targets must have stable unique identifiers so that two editions of the same
game do not overwrite each other's saves.  The manager should initially use a
target derived from the manifest filename plus ``gameId`` or accept an
explicit future ``target`` field.

Licensing and content
---------------------

ScummVM and the selected ESP32 port are GPLv3.  Distributed binaries must be
accompanied by the corresponding source and license obligations.  Proprietary
game data is never bundled.  Freeware games or demos may be packaged only when
their individual licenses permit redistribution.

Implementation phases
---------------------

Phase 0: size and boot proof
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1. Fork the T-Deck ScummVM port into a separate repository/branch.
2. Build only the ``scumm`` engine without HE.
3. Use Lilka's exact 16 MiB partition layout for the size check.
4. Prove that the raw application image is no larger than ``0x640000``.
5. Boot the raw image through Keira multiboot and prove rollback to Keira.

Acceptance: a raw SCUMM-only image fits, boots under Keira's bootloader, and a
restart returns to Keira without changing the partition table.

Phase 1: Keira manifest path
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1. Add the manifest model and bounded JSON parser.
2. Add ``FT_SCUMMVM`` to File Manager.
3. Add the fixed engine registry.
4. Validate paths and present errors without flashing.
5. Add parser/path-resolution unit tests where the repository's test setup
   permits; otherwise isolate pure functions for host testing.

Acceptance: opening a valid manifest resolves the expected engine and game
paths; malformed manifests cause no state change.

Phase 2: launch handoff
~~~~~~~~~~~~~~~~~~~~~~~

1. Encode the manifest path in the RTC multiboot command.
2. Flash the selected engine with visible progress and cancellation before
   reboot begins.
3. Implement the guest RTC reader and manifest revalidation.
4. Launch one known freeware/demo SCUMM title directly.

Acceptance: opening one manifest starts the game and exiting returns to Keira.

Phase 3: Lilka backend usability
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1. Adapt display geometry and shared SPI arbitration.
2. Add pointer acceleration and button mapping.
3. Add the virtual keyboard path.
4. Store configuration and saves on SD.
5. Add silent audio and optional I2S audio variants.

Acceptance: a complete play/save/load/exit/relaunch cycle works without data
loss, stuck audio, display corruption, or requiring Reset.

Phase 4: engine cache
~~~~~~~~~~~~~~~~~~~~~

1. Centralize guest-image cache metadata in Keira's multiboot writer.
2. Verify the cached app identity before ``bootLast()``.
3. Invalidate metadata when any other binary is flashed.
4. Compare first launch and cached launch behavior.

Acceptance: relaunching a game using the same engine performs no app-partition
write, while switching engines reliably flashes the selected image.

Phase 5: library manager and more engines
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1. Add a launcher app that scans for ``.scummvm`` manifests.
2. Reuse the direct-open parser and launch service.
3. Port and size-check engines one at a time.
4. Add optional icons, sorting, favorites, and recent games only after the
   basic path is stable.

Validation matrix
-----------------

For every engine image:

* raw image size is at most ``0x640000``;
* verbose build confirms the intended backend and dependencies;
* cold launch and cached launch work;
* paths containing spaces and non-ASCII text work;
* missing engine/game files fail without changing the boot partition;
* game saves survive exit, reboot, and engine switching;
* at least twenty launch/exit cycles return to Keira;
* free internal heap and PSRAM are recorded before and during a game;
* SD and display operate concurrently without SPI corruption;
* button chords cannot trigger accidental exit;
* no guest image calls ``esp_ota_mark_app_valid_cancel_rollback()``;
* Keira localization, formatting, static checks, and the full v2 build pass.

Repository and branch ownership
-------------------------------

Keira-side manifest parsing, File Manager association, launch UI, and cache
metadata belong to the Keira ``feature/scummvm-manager`` branch.

The ScummVM ESP32-S3 backend and engine binaries belong in a separate fork and
feature branch.  Both repositories form one integration unit for end-to-end
testing, but engine source must not be copied into Keira merely to simplify a
build.

References
----------

* ScummVM T-Deck ESP32-S3 port:
  https://github.com/varna9000/scummvm-tdeck
* ScummVM upstream:
  https://github.com/scummvm/scummvm
* ESP-IDF bootloader compatibility:
  https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/bootloader.html

