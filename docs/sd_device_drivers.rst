SD-loaded device drivers: first-iteration requirements
=====================================================

Status: proposed implementation requirements, not an implemented feature.
Target: Lilka v2 / ESP32-S3. Language: English.

Goal
----

Allow separately compiled native drivers on the SD card to register external
audio outputs and displays connected through the extension header. Keira loads
configured drivers at startup and lets users select the output devices without
rebuilding firmware for every supported peripheral.

Existing foundations
--------------------

* Reuse the SDK's ``lilka::DynLoader`` and ELF relocation/symbol-resolution code.
  Do not introduce a second loader or treat drivers as ordinary foreground apps.
* Current ``DynApp`` loads a ``.so``, runs its entry point, then unloads it.
  Drivers require a separate persistent lifetime and a dedicated host API.
* Current exported DynApp APIs do not provide device registration or bus access.
* Audio applications currently configure/write I2S directly; the SDK's
  ``Audio`` class is not yet a selectable output abstraction.
* The SDK display is tied to ST7789. Existing canvases can be reused, but output
  presentation must be separated from that specific panel.
* Existing project extension documentation exposes 3.3 V, ground, six GPIOs
  (12, 13, 14, 21, 47, 48) and TX/RX on a 12-contact header. Verify the actual
  board revision and resource conflicts before assigning pins.

First-iteration scope
---------------------

1. One driver manager in Keira, following the existing Service/KeiraThread
   architecture when a background task is necessary. No independent task
   hierarchy and no mandatory polling task per driver.
2. Explicit startup configuration under ``/sd/drivers/``: enabled driver paths,
   per-device pin/configuration values and stable preferred device IDs.
   Configuration format is an implementation choice to document before coding.
   Do not execute every ``.so`` found on the card automatically.
3. Support one selected audio output and one selected display output at a time.
   Built-in outputs remain selectable and are the default/fallback.
4. Provide one external I2S audio reference driver and one SPI TFT reference
   driver. Choose exact tested peripheral models before implementation; do not
   advertise arbitrary amplifier or display compatibility.
5. Add device selectors and a compact driver status/error view to settings.
   Load configuration after SD initialization, before ordinary applications
   start. Missing SD or invalid configuration must not prevent normal startup.

Driver contract and lifetime
----------------------------

* Use a versioned C ABI with fixed-width types, structure sizes, driver/device
  IDs, capabilities and explicit error results. Do not pass C++ SDK objects,
  STL types or compiler-specific class layouts across the boundary.
* A defined exported entry point receives a bounded host-function table and
  returns a driver descriptor. Reject unsupported ABI versions before init.
* Define init, enable, disable and shutdown operations, plus audio submission
  or display presentation as appropriate. Initialization must not take over an
  output until registration and resource validation succeed.
* Keep the loaded module and its state alive while any callback can execute.
  Quiesce callbacks/transfers, disable hardware, release resources and then
  unload. Never retain function pointers into freed module code.
* Provide managed GPIO/I2C/SPI/I2S operations needed by reference drivers.
  Reserve resources before driving pins; reject conflicting claims and roll
  back partial initialization. Do not expose unrestricted SDK internals as ABI.
* Specify buffer ownership, supported formats, timeouts and callback threading.
  No SD reads, allocation or initialization in real-time transfer callbacks.
  Bound file sizes, enabled-driver count, buffers and transfer waits; publish
  concrete budgets after the reference devices are selected.
* Drivers are trusted native code, not sandboxed plugins. A buggy driver can
  crash Keira; error fallback is not a promise to recover from memory corruption.
  Provide a boot-time way to skip external drivers, independent of SD config.

Audio requirements
------------------

* Introduce a common PCM output interface for built-in and external outputs.
  Migrate all in-tree PCM producers, including startup audio, emulators and
  Liltracker. Apps must not silently restore hardcoded built-in pins.
* Preserve volume behavior and explicitly negotiate sample rate, bit depth and
  channel count. Support formats used by migrated apps or return a clear error.
* One owner controls each I2S peripheral. Switching must mute/stop or drain the
  producer safely, reconfigure the output, then resume without concurrent
  driver install/uninstall or writes. If live switching is not supported by a
  producer, stop playback explicitly rather than race its audio task.
* Keep MCLK disabled unless required and explicitly assigned. Never let an
  Arduino default implicitly claim GPIO0 or another internal pin.
* Use available extension GPIOs for external I2S signals; stock internal audio
  pins are not directly exposed on the extension connector.
* An analog PAM8302 alone is not a PCM output: it requires a DAC. The FM tuner's
  analog audio path is separate and is not automatically routable Keira audio.

Display requirements
--------------------

* Add a presentation backend that accepts RGB565 frames/regions independently
  of ST7789. Preserve the existing logical UI dimensions and orientation.
* Route the compositor's final image, including status bars and system overlays,
  to the selected output. Audit direct display drawing and special/interlaced
  paths so apps cannot bypass output selection.
* For the first reference panel, document an explicit fit policy (1:1,
  letterbox or supported scaling). No arbitrary-resolution UI reflow in v1.
* Manage external SPI separately from the internal display/SD bus where possible;
  retain bus locking, bounded transfers and safe buffer lifetime.
* A failed external display must restore built-in presentation. Settings must
  remain reachable; brightness/sleep control must respect each device's
  capabilities and must not PWM unrelated extension or amplifier pins.

Fallback, persistence and power
------------------------------

* Missing files, malformed ELF, unresolved imports, ABI mismatch, resource
  conflict and reported init/transfer failures must produce actionable status
  and preserve or restore a working built-in output.
* Persist stable IDs, not enumeration indices. A missing preferred device falls
  back without erasing the preference; failed initialization must release claims.
* Disable external amps/backlights when their outputs are inactive if hardware
  supports it. Document electrical default states and shutdown behavior.
* Extension power is 3.3 V, not a guaranteed high-power amplifier supply. Verify
  peripheral current budget, logic levels and any separate supply/backfeed path.

Acceptance criteria
-------------------

* Reference drivers load from SD at startup, register once, and appear in settings.
* Switching built-in/external audio works for every migrated in-tree PCM producer;
  volume, format changes and repeated start/stop remain correct.
* Launcher, menus, representative apps, overlays and display sleep/wake work on
  both outputs using the documented fit policy.
* Missing SD/driver, invalid ABI/ELF/imports, conflicting pins, initialization
  failure and reported transfer failure leave built-in controls/output usable.
* Repeated activation/deactivation releases resources without stale callbacks or
  growing allocations. Module unload occurs only after all users are quiescent.
* Physical reference-device tests verify pin mapping, power, sound, display
  orientation and useful performance. Host/source checks alone are insufficient.
* Existing DynApp applications and stock built-in behavior remain compatible.

Out of scope for iteration one
------------------------------

Hot-plug detection, arbitrary-device autodetection, driver downloads, a plugin
marketplace, kernel isolation, binary compatibility with arbitrary ESP-IDF/C++
libraries, display mirroring, simultaneous audio outputs, arbitrary-resolution
UI layouts, Lilka v1 and automatic integration with independently flashed guest
firmware. SDK changes will need a coordinated implementation branch; this
requirements-only branch does not change SDK or firmware behavior.
