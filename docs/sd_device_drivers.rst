SD-loaded device drivers: display iteration
=========================================

Target: Lilka v2 / ESP32-S3. Implementation branch: feature/sd-device-drivers.
The working poc/ili9341-screen-mirror is retained as a hardware reference;
Anton confirmed image, orientation and colors on 2026-10-13, with BLK floating.
He also reported stale previous-menu content and slow refresh. Those are defects
requiring verification, not an accepted limitation of the new implementation.

Architecture and discovery
--------------------------

Keira scans /sd/drivers/<package>/manifest.json on startup after SD/VFS setup.
A package contains manifest.json and driver.so. Bounded manifests identify ABI,
driver UUID, device UUID, name and type. The first supported type is display;
audio/Bluetooth remains a later milestone. Reuse the SDK ELF relocation loader,
not a second loader. Compatible native modules execute a registration entry
which calls the host registration callback with a versioned C descriptor,
settings schema and lifecycle/presentation callbacks. Registration must not
claim pins, start transfers or create tasks. Activation follows saved enabled
state. Modules remain loaded while registered callbacks/settings are referenced.

Drivers are trusted native code, not isolated plugins. Scan only the dedicated
drivers directory; malformed/unsupported packages produce status, not normal
boot failure. Hold Select at startup to skip execution of external modules.
Missing card/packages preserve NVM preferences. Existing DynApp symbols and
lifetime must remain unchanged. Driver entry uses the loader's main(argc,argv)
shim, with argc=1 and argv[0] an opaque pointer to the versioned host API, not a
command-line string. ABI negotiation is required before registration.

Devices and settings
--------------------

Devices is a top-level Keira menu, listing registered/failed packages and
requested enabled versus active/error status. Device pages contain generic
Enable, Status, driver identity/version, Reset and driver-declared settings.
No display/panel-specific logic belongs in these pages. The initial schema
supports bounded integer/boolean/choice fields with stable field IDs, labels,
defaults, limits and step/choice metadata. Pins/rotation/SPI/refresh are schema
fields of the SD ILI9341 driver, not Keira UI special cases. Settings edits are
validated, explicitly persisted and safely re-activate enabled devices. Failed
edits restore the previous settings/output. NVS errors must be surfaced.

Stable driver UUID identifies an implementation; stable device UUID identifies
one configured instance. Neither changes with package path, list order or driver
version. Duplicate UUIDs are rejected. NVS uses a short numbered key registry
because namespace/key names allow only 15 characters. Records contain complete
UUIDs, schema version, enabled preference and field-ID/value pairs, avoiding
hash collisions and enumeration-index persistence. Unknown/future versions are
not silently overwritten. Persist only user edits, not frames/polling. Missing
packages retain their records. Reset clears only that device's settings.

Display routing
---------------

Display settings cycles Internal, External, Both. Mode is persisted globally,
separate from whether a driver is enabled. Internal avoids external captures
and transfers. External uses the currently active display driver; Both presents
the same logical image internally and externally. Only one external display
is active; enabling another performs a serialized handover. Failed activation
or reported transfer failure falls back internally without erasing the chosen
mode or enabled preference. Physical presence cannot be detected by write-only
SPI. Built-in controls must remain reachable.

The compositor owns capture at a coherent frame boundary, under its existing
thread/layer locks. Clear the entire external composition before copying retained
layers to prevent stale uncovered pixels. Include status bar, fullscreen behavior,
toasts and system volume/brightness feedback. No independent screenshot poller,
no source-canvas mutation for external overlays and no queued historical frames.
Use two shared PSRAM presentation frames, with nonblocking producer acquisition
and an asynchronous service consuming complete frames outside compositor locks.
Both LCDs read the same composed pixels. The external service leases one frame;
the compositor reuses the other while it is in flight. Pending content is
latest-frame-wins, never a queue of historical images.
Re-capture transitions, overlay expiry, orientation and wake. Audit raw/direct
and interlaced application paths; do not claim arbitrary direct writes are
mirrored without proof. Before first application presentation, boot cards remain
internal. Internal-only behavior retains its existing compositor fast path.

ILI9341 reference driver
------------------------

Blue 2.8-inch, 240x320, eight-pin, no touch/CS. CLK=12, MOSI=13, RES=14, DC=21;
MISO unused. VCC=3.3 V and GND from extension, BLK disconnected as device-tested.
No brightness capability advertised. Dedicated external SPI (not display/SD bus),
managed GPIO whitelist/resource ownership and rollback. Only host handles cross
the C ABI; no Arduino/C++ objects or unrestricted SDK internals.

Preserve 1:1 logical dimensions: 280x240 on 320x240, 20-pixel black side margins;
portrait uses appropriate rotation and centred margins. Default 20 MHz initially,
with driver-defined selectable speed and refresh cap. Use bulk bounded DMA-capable
SPI transfers and a small internal staging buffer, no per-pixel SPI calls and no
private framebuffer allocated by the driver. Frame pixels are host-order RGB565; host transport
performs wire byte ordering. Clear margins on dimension changes. Disable sends
display-off where possible but does not promise electrical backlight shutdown.

Memory/performance budgets and regression gates
----------------------------------------------

Initial explicit ceilings: four discovered modules, eight fields per device,
4 KiB manifest (8 KiB capped JSON allocator), 64 KiB ELF, 8 KiB executable text and 32 KiB data per module,
32 persisted device records. Release raw ELF file bytes after relocation.
Each shared frame is capped at 76800 pixels/153600 bytes in PSRAM, allocated
to the actual logical size only for routed external output. The pair currently
uses 268800 bytes (280x240 or 240x280), versus 134400 for the old snapshot.
One reusable 4096-byte internal DMA
staging buffer only while a display owns SPI. Worker stack 6144 bytes; no task
per driver, no per-frame allocations, no growing queues. Do not silently fall
back to a large internal-RAM framebuffer or unsafe PSRAM executable fallback.
Driver-disabled code/metadata overhead remains bounded and must be measured.

Measure capture/transfer time, effective FPS, frame drops, input latency, free
internal/PSRAM heap, largest free blocks, minimum heap and worker stack watermark.
20 MHz has about 54 ms raw wire time per 280x240 frame; remove the PoC's extra
100 ms post-transfer delay. Faster SPI only after bench qualification. Dirty
row updates were added after device-confirmed complete-frame presentation; see
the performance correction below.

Verify baseline versus driver-disabled, External and Both: launcher/menu
transitions, overlays, cropped/fullscreen/interlaced applications, emulators,
SD operations, audio, sleep/wake and rotation. Repeated enable/disable and failed
activation must release state/SPI/frame resources with no stale callbacks or
heap growth. Failure paths include missing SD/file, malformed ELF/imports,
unsupported ABI, duplicate UUID, invalid persisted values, resource conflicts,
out-of-memory and reported transfer failure. Native memory corruption cannot be
contained by ordinary fallback. No firmware size/runtime claims from host mocks.

Windows build-all and packaging
------------------------------

Add a driver compilation function/option to the existing Windows runner. Use the
selected Keira tree's ABI header and driver sources, the installed matching
ESP32-S3 toolchain and explicit freestanding linker layout compatible with the
SDK loader. Build result includes driver.so, manifest.json and hashes under
sd/drivers/<package>/, separate from firmware flash files. No driver compile
in check-only mode. Report driver build errors/results separately; selected
firmware/app and SDK SHAs remain recorded. Never silently deploy packages.
Interactive Keira builds offer driver compilation; explicit CLI selection supports
unattended runs. Update runner docs/tests/Windows archive. ABI versions are
recorded so packaging cannot accidentally imply arbitrary cross-version support.

Implementation and acceptance sequence
--------------------------------------

1. Document requirements/ABI/discovery/persistence; inspect current loader.
2. Implement bounded manager, UUID records and generic Devices UI.
3. Add mode cycling and coherent asynchronous presentation.
4. Build freestanding SD ILI9341 driver and Windows packaging.
5. Run applicable localization/format/static checks and meaningful host tests
   for ELF validation, settings identity, lifecycle/ownership and routing.
6. Physical validation by Anton: SD-only driver, ordinary firmware profile,
   persistent enable/mode/settings, stale-menu-free updates, measured refresh,
   repeated toggles, missing card/invalid driver fallback and regressions.

Do not merge into staging until source checks and requested device acceptance
are complete. Driver binaries need rebuilding when the ABI breaks. Dynamic apps,
Lilka v1, audio producer migration, hotplug/autodetection, downloads/marketplace,
arbitrary UI scaling and concurrent external displays are outside this iteration.

First source iteration: delivered behavior and validation
-------------------------------------------------------

Startup registration/discovery, generic Devices settings, UUID NVS records,
Internal/External/Both routing, the freestanding ILI9341 module and Windows
compiler packaging are implemented. Drivers default disabled; mode defaults
Internal. Registered module code/schema stay resident even when disabled, so
Devices can show their settings without re-executing native code. Disable frees
instance state/SPI/DMA/frame, not the registered code. Restart re-scans packages.
Only one external display is active; other enabled preferences remain intact,
and the saved active-display UUID determines the preferred startup instance.

The external bus is ESP-IDF SPI2_HOST, matching Lilka's user SPI2/Arduino FSPI;
SPI3_HOST is the internal display/SD bus and must not be claimed. Managed driver
claims do not intercept legacy Lua/raw GPIO/SPI code: do not concurrently run
extension GPIO tests or scripts touching driver-owned pins. Driver registration
cannot open SPI before an instance state exists. Internal mode calls the driver's
output-off callback and avoids external captures; the backlight itself is not
controlled. No display brightness setting is declared for this board.

Compositor capture clears uncovered pixels, includes final retained layers and
current overlay, and retries newest content after busy-buffer drops. Static
scenes without redraw/feedback changes produce no new transfer. External-only
keeps internal fallback until the service has actually started; a failed task
allocation cannot turn off the only working UI. The worker has no historical
frame queue and no extra 100 ms post-transfer sleep. Normal disable releases
claims; a 100 ms SPI polling-end timeout quarantines the in-flight transaction,
DMA buffer and bus until reboot, rather than freeing hardware-owned memory.
This exceptional 4 KiB retention is intentional and bounded, not a retry leak.

Serial diagnostics every two seconds during external routing report captures,
transfers, FPS x1000, latest capture/transfer microseconds, free/largest/minimum
internal heap, free PSRAM and worker stack watermark. Runtime FPS/headroom and
physical regressions are not measured here. Loader admission retains a 32 KiB
internal free-heap floor and requires an 8 KiB executable block; unsafe PSRAM
code fallback is rejected. Budgeted native code in the reference module is
about 1 KiB, not an Arduino display library embedded in every driver.

Validation performed:

* Native ESP32-S3 .so compilation and production ELF preflight acceptance.
* Actual changed firmware translation units compile as C++11 with the installed
  Xtensa/Arduino headers (source/object checks, no firmware linking).
* ASan/UBSan production manager/reference-driver tests: NVS field-ID matching,
  unsupported saved schema/reset, complete/cropped frame clearing, busy capture,
  sleep blanking, failed pin-change rollback, frame allocation failure,
  worker-unavailable fallback, DMA timeout quarantine and twenty toggles without
  retained normal-path allocations.
* Existing navigation normal/sanitized, emulator menu and NES double-buffer
  render checks pass. Current SDK/Keira overlay harness passes both locales in
  normal, ASan and UBSan modes, including new output modes, busy capture retry,
  unchanged scenes, overlay expiry and sleep/wake.
* The actual SDK menu construction/layout was host-rendered at 280x216 and
  visually inspected for Devices, populated settings/scrolled fields and Display
  mode. This uses an approximate recording font, not a physical screenshot.
* Windows runner's 56 existing tests and four new package/cleanup checks pass.
* Localization check and focused clang-format-20/new-code cppcheck pass.
  Repository-wide make checks were run; pre-existing formatting failures in
  unrelated sections and source/host-fixture ODR conflicts prevent a blanket
  green result. They are not silently treated as passing.

The cached Arduino/IDF4 headers exposed an existing missing pdTICKS_TO_MS macro
in Launcher/Lua. An additive fallback in thread.h preserves newer FreeRTOS
implementations while allowing these source checks with the installed toolchain.
SDK sources, stock pin defaults and staging refs are unchanged. Full firmware
linking/size comparisons, target boot, physical stale-menu/refresh tests, live
heap/stack measurements and hardware audio/SD/emulator regression acceptance
remain pending. Do not interpret host tests as device qualification.

Build/test instructions
-----------------------

Use the updated Windows build-all runner with --interactive --branch-ask
--drivers, select Keira and feature/sd-device-drivers, and keep your ordinary
hardware profile/SDK features/stage. Copy the resulting sd folder contents to
the SD root before restarting. Enable ILI9341 in Devices, then cycle Display
mode to Both. Missing package or disabled driver keeps internal fallback.

Direct driver-only compilation::

    python tools/build_drivers.py --toolchain <PlatformIO-core>/packages/toolchain-xtensa-esp32s3/bin --output <SD-staging-root>

Host lifecycle/ELF check (existing sibling SDK and cached ArduinoJson required)::

    python tests/devices/run.py <SD-staging-root>/drivers/ili9341/driver.so

The standalone driver compiler uses --no-relax and -Bsymbolic: the installed
Xtensa linker otherwise fails while merging literal/GOT relocation metadata.
It emits no SDK/libc imports and records SHA256 hashes for both package files.
Changing ABI layout/version requires rebuilding matching firmware and drivers.

Activation diagnostics and GPIO ownership correction
--------------------------------------------------

After the first physical activation attempt Anton reported the generic
"Activation failed: pins/SPI/memory" message. That message did not distinguish
host failures. ESP32-S3's unused data4..data7 fields had incorrectly remained
zero (GPIO0), while the host mock omitted these fields. They now explicitly use
-1 along with MISO/WP/HD; the mock models and checks every unused line. This
configuration correction is not yet proof of the reported hardware root cause.
SPI bus/device setup, DMA allocation, GPIO and transfer errors now retain their
exact ESP-IDF name/code through activation cleanup and appear in Devices status
and serial logs. Native callback and state allocation failures remain distinct.

GPIO Manager was not a passive readout: its constructor set all six expansion
pins to INPUT, and IN/OUT labels came from its own cached mode table. Opening it
could therefore disrupt an active display. Driver claims are now exposed through
a small atomic pin mask. GPIO Manager skips claimed pins during initialization,
marks them Driver/Reserved and blocks manual mode/value/PWM edits. A DMA timeout
keeps those claims reserved until restart. Free pins retain their previous
behavior. This does not intercept arbitrary legacy Lua/raw GPIO code.

Failure-injection tests now verify exact bus/device/DMA errors, claim rollback,
and protected GPIO initialization/PWM operations; sanitizer checks pass. Target
C++11 syntax checks and localization/focused format gates pass. The driver ABI,
settings schema and SD binary are unchanged: rebuild/flash Keira only for this
fix. The actual SDK GPIO menu construction was host-rendered and inspected,
including reserved pins and free-pin rows. Physical activation remains pending;
if it still fails, the new exact status identifies the next diagnostic step.

Polling-start compatibility correction
--------------------------------------

Anton then reported "SPI transfer start: ESP_ERR_INVALID_ARG (0x102)". The
installed IDF4.4 spi_master.h explicitly documents that polling_start accepts
only portMAX_DELAY; the previous 100 ms start argument was invalid. The start
now uses the supported argument. This manager exclusively owns the bus/device,
serializes calls and queues no asynchronous transactions. Polling completion
retains its 100 ms timeout and safe DMA quarantine. This is not a switch to an
unbounded transfer-completion wait, and no additional buffer/task is allocated.
The host mock now rejects unsupported start waits: it reproduced the original
activation failure before the source fix and passes the fixed production path.
ABI/SD driver unchanged; firmware-only rebuild. Hardware retry remains pending.


External presentation performance correction
-------------------------------------------

Anton measured about 10.9 FPS with 15-16 ms capture and 65 ms transfer and
reported general slowdown and static-menu flicker. The worker previously ran
at idle priority and polling DMA completion occupied a CPU throughout each
transfer. Identical menu redraws unnecessarily sent whole frames.

The worker now uses normal task priority and ESP-IDF queued DMA with blocking
completion (100 ms submission/completion waits). Queue depth remains one, with
one persistent transaction and 4096-byte staging buffer. Timeout or unexpected
completion still quarantines the bus/DMA/pins until reboot; no in-flight buffer
is reused or freed.

Capture compares composed scanlines against the single retained PSRAM frame,
uses full-width canvas rows directly, and copies only changed rows. A 640-byte
stack scratch row handles clipped/overlapping layers and feedback; 40 bytes of
dirty bits accumulate across captures until presentation. No second frame or
per-frame heap allocation is added. Unchanged captures queue no transfer.
Overlay visibility uses one capture timestamp for all rows.

ABI 1 has an optional size-gated ``present_rows`` descriptor tail. Legacy short
descriptors still register and fall back to full-frame presentation without
reading the tail. ILI9341 1.1.0 supports contiguous changed-row windows, with
full-frame fallback above eight bands or at least 75 percent changed rows.
The driver retains complete clearing after geometry/rotation changes and
rejects out-of-range row windows. UUIDs and stored settings are unchanged.
New descriptors require the updated firmware; install firmware first.

Diagnostics append ``spi-hz``, ``unchanged``, ``full``, ``bands`` and cumulative
``pixels``. Idle FPS can now be zero by design. Compare interval counter deltas
and transfer/capture times during animation, not idle FPS. At the unchanged
20 MHz default a full 280x240 frame still needs about 54 ms of raw wire time;
this cannot equal the internal display's full-screen animation rate. The
existing 40 MHz setting is not automatically selected; Anton subsequently
confirmed it works on his wiring and selected it in Devices.

Rebuild/flash Keira and rebuild the SD driver with ``--drivers`` on
``feature/sd-device-drivers``. Replace ``drivers/ili9341/driver.so`` and its
manifest on the SD card with the freshly generated package, then restart.
An old SD driver benefits from blocking DMA and unchanged-frame suppression,
but does not support partial transfers.

Source validation: ASan/UBSan production manager/driver tests cover unchanged
traffic, one-row/banded/full fallback, accumulated dirty rows, overlay removal,
negative clipping, truly short ABI descriptors, invalid row windows, queue
submission errors, timeout/wrong-completion quarantine, NVS rollback and 20
activation cycles. GPIO ownership and both-locale normal/ASan/UBSan compositor
overlay regression checks pass. The freestanding Xtensa driver builds and
passes production ELF preflight; firmware source checks use installed target
headers. Changed-source clang-format 20.1.8, cppcheck 2.13 and localization gates
pass. Repository-wide make gates still fail on unchanged statusbar formatting
and existing host-fixture ODR conflicts (Acquire/FMEntry). No full firmware
link or hardware timing/flicker/stack/audio/SD soak is claimed. Physical confirmation remains a follow-up after installing both
components.

Dual-display responsiveness follow-up
-------------------------------------

This historical mitigation is superseded by shared-frame presentation below.
Both mode sampled the external mirror at most once every 67 ms (15 FPS),
checking the deadline before taking canvas locks. Deferred redraws remain
dirty so the next capture takes the latest composed image. External-only
keeps its existing capture cadence; driver transfer-rate settings are unchanged.
This bounds mirror-copy frequency, not the duration of an individual capture.

The compositor now yields for at least one scheduler tick after an overrun
and rebases its deadline. It no longer repeatedly runs against expired 60 FPS
deadlines, starving other equal-priority tasks. No buffer or task is added;
only one atomic timestamp is retained. The stable external snapshot is still
needed so the app can draw while the slower external transfer reads it.

This follow-up is firmware-only: rebuild/flash Keira on the same branch.
The preceding partial-transfer update still requires the 1.1.0 SD driver.
Host checks cover sustained overruns, normal pacing/recovery, tick wrap,
Both-mode capture deadlines and retained redraws. Actual dual-display latency
and throughput remain unverified until tested on the device.


Shared double-buffered presentation
-----------------------------------

The fixed Both-mode 15 FPS capture cap is removed. The driver-selected refresh
limit and actual SPI throughput still bound external transfers; they do not
bound internal rendering or game simulation. Anton has already selected 40 MHz
in Devices and confirmed it works with his wiring. Preserve this saved setting;
there is no schema/default migration or driver binary change. At 40 MHz, raw
wire time for 280x240 RGB565 is 26.88 ms, excluding commands/staging/scheduling.

Apps already have their own drawing/back canvases. Those public APIs are
unchanged. Two shared final presentation frames replace the single private
external snapshot. Layers and feedback are composed once into these shared
pixels; both LCD paths read the same completed image. This is not zero-copy
app rendering: status bars, cropped layers and feedback still need composition.
It removes the separate external-only image and internal reads from mutable
app canvases in shared mode, not every pixel copy in the system.

The compositor holds app canvas locks only during composition, never during
internal or external SPI. External DMA retains an immutable frame lease under
a dedicated frame lock, then releases that lock for the entire transfer. The
producer always writes the unleased frame, replacing any pending older image.
Internal SPI consumes the current shared frame synchronously after releasing
canvas locks. It uses the RAM/bulk Arduino_GFX bitmap overload; the const/flash
bitmap overload would perform per-pixel writes. The SDK's HWSPI bulk path reads
without modifying the shared RGB565 input, so both readers can overlap.

Frame metadata/dirty bits are protected independently from the driver mutex.
External leases snapshot dimensions and accumulated row bits at acquisition;
changes captured during a transfer remain pending for the next transfer.
Internal dirty bits are independent, allowing both screens to suppress static
traffic without confusing their different refresh histories. No historical
queue, additional service task, per-frame allocation or forced core affinity
is introduced: the existing FreeRTOS service is runnable on either ESP32-S3 core.
Internal-only keeps its original presentation/interlace path. Shared mode
presents the final composed image, rather than internal-only field parity.

Settings/disable/driver handover wait for both readers before freeing frames.
Failure to allocate either buffer rolls back both allocations and preserves
internal fallback. The pair costs one additional actual-size PSRAM frame:
134400 bytes (131.25 KiB) for current dimensions, with a ceiling of 153600
additional bytes. There are two 40-byte dirty bitmaps and small bounded metadata,
plus lifetime/frame mutexes; no new full-screen internal-RAM buffer or task stack.

This change is firmware-only on feature/sd-device-drivers. Rebuild/flash Keira;
keep the existing 1.1.0 SD driver and saved 40 MHz/refresh settings. Existing
ABI-1 drivers without partial-row support still receive complete frames.

Source checks exercise production external DMA while twenty newer frames are
composed and internally presented: the leased pixels remain immutable, the
canvas producer never takes the driver mutex, internal and external readers use
shared storage, and the next transfer consumes latest content without a backlog.
Allocation failure on the second buffer, legacy ABI, partial/fragmented/full
rows, dirty accumulation, mode transitions, overlays, sleep/wake, timeout
quarantine, settings persistence and repeated activation remain covered.
Hardware FPS/input latency, PSRAM contention, heap/stack watermarks, audio/SD
soak and full firmware linking/size remain unverified by these source checks.


Live serial FPS/timing monitor
-----------------------------

Always-on, firmware-only diagnostics emit about every two seconds while playing;
no UI toggle, NVS writes, per-frame logging, heap allocation or extra framebuffer.
The compositor ``perf:`` and ``perf-time:`` lines work in Internal, External and Both mode:

* ``app`` / ``mode`` / ``window-ms`` identify the foreground application, effective
  output route (including internal fallback) and measured averaging interval.
* ``submit-fps`` counts foreground ``queueDraw`` submissions, including submissions
  superseded before display. This is rendered/submitted FPS, NOT emulated CPU
  simulation FPS and not a direct input-latency measurement.
* ``internal-fps`` counts compositor iterations that actually updated the internal
  LCD; a multi-band/layer update counts once, not once per SPI call. Unchanged
  scenes and External-only can correctly show zero.
* ``compositor-fps`` counts awake presentation-loop iterations, not game frames.
* ``interval-avg/max-us`` measures compositor start-to-start intervals including
  pacing, stalls and diagnostic output. ``work-avg/max-us`` measures active
  iteration time including lock waits, composition and internal transfers,
  excluding deliberate pacing and the serial report itself.
* ``internal-avg/max-us`` measures time spent in internal presentation calls per
  updating iteration, not the total game frame time. Shared mode includes its
  lifetime/metadata lock waits; legacy mode sums layer/overlay calls.
* ``overruns`` counts iterations whose work reaches the nominal 16 ms compositor
  budget. Window statistics reset after each report, app/route change and wake;
  app frame counters are sampled under the existing canvas mutex. Diagnostic
  text is copied under the thread-list lock and printed after all those locks
  are released. USB/serial output still has some overhead, visible in intervals.

The ``devices:`` and ``devices-memory:`` lines retain existing counters and
heap diagnostics. The ``perf-ext:`` line adds:

* ``external-fps``: actual external presentation attempts per elapsed window;
  reported successful routing normally excludes failures because a failure
  immediately switches to internal fallback. The legacy ``devices: fps-x1000`` field
  remains for comparison with older logs (divide it by 1000).
* ``capture-avg/max-us``: average/worst successful compare/composition operation,
  including unchanged comparisons.
* ``transfer-avg/max-us``: average/worst complete external driver presentation,
  including DMA waits and command/staging overhead, not just raw SPI wire time.

Reports are split to fit the SDK's 256-byte log formatter without truncation.
All durations are microseconds: divide by 1000 for milliseconds. Compare the
same game/scene in Internal, External and Both at the same SPI/refresh settings.
Submit FPS much higher than LCD FPS means displayed submissions are being
skipped; high work/interval maxima reveal stalls that averages can hide. Low
external FPS in a static menu is expected and is not a performance regression.
The monitor does not prove improved performance until logs are collected on
hardware. Both-locale compositor tests execute the real monitor with 75 synthetic
submissions/s and 25 compositor updates/s and verify unlocked serial reporting;
interval arithmetic covers idle/reset, long-frame totals and clock wrap.
