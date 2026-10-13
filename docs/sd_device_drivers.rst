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
Use one PSRAM capture buffer, with nonblocking producer acquisition and an
asynchronous service consuming complete frames outside compositor locks. If a
transfer owns the buffer, skip capture and retry newest content, never enqueue.
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
extra display framebuffer. Frame pixels are host-order RGB565; host transport
performs wire byte ordering. Clear margins on dimension changes. Disable sends
display-off where possible but does not promise electrical backlight shutdown.

Memory/performance budgets and regression gates
----------------------------------------------

Initial explicit ceilings: four discovered modules, eight fields per device,
4 KiB manifest, 64 KiB ELF, 8 KiB executable text and 32 KiB data per module,
32 persisted device records. Release raw ELF file bytes after relocation.
Frame cap 76800 pixels/153600 bytes in PSRAM, allocated only for routed external
output; typical landscape 134400 bytes. One reusable 4096-byte internal DMA
staging buffer only while a display owns SPI. Worker stack 6144 bytes; no task
per driver, no per-frame allocations, no growing queues. Do not silently fall
back to a large internal-RAM framebuffer or unsafe PSRAM executable fallback.
Driver-disabled code/metadata overhead remains bounded and must be measured.

Measure capture/transfer time, effective FPS, frame drops, input latency, free
internal/PSRAM heap, largest free blocks, minimum heap and worker stack watermark.
20 MHz has about 54 ms raw wire time per 280x240 frame; remove the PoC's extra
100 ms post-transfer delay. Faster SPI only after bench qualification. Dirty
regions are deferred until complete-frame correctness is established.

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
