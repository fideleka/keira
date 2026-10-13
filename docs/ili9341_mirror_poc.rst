Temporary ILI9341 main-screen mirror PoC
======================================

Branch: ``poc/ili9341-screen-mirror``. Compiled-in hardware experiment, not the
SD-loaded driver interface. SDK source is unchanged.

Hardware
--------

Blue no-touch ILI9341, 240x320, no exposed CS pin:

* Lilka GPIO12 -> CLK (display pin 3).
* Lilka GPIO13 -> MOSI (pin 4).
* Lilka GPIO14 -> RES (pin 5).
* Lilka GPIO21 -> DC (pin 6).
* GND -> GND (pin 1).
* 3.3 V -> VCC (pin 2), only after confirming the module's 3.3 V rating.
* MISO (pin 8) unused. BLK (pin 7) is not driven by this firmware: establish its
  electrical requirements before connecting it. A visible image requires a
  correctly powered/enabled backlight; a dark panel alone does not prove SPI
  initialization failed.

The module owns the user SPI bus exclusively; internal display/SD SPI is left
alone. Do not concurrently use the Bluetooth/FM pin mapping or scripts driving
these extension GPIOs. Do not drive signals into an unpowered display; unplug
the signal harness until the display power arrangement is established.

Behavior
--------

* Startup on the external panel: red, green, blue, each for 300 ms.
* Then mirror the AppManager's composed panel/app canvas, including canvas-based
  toasts and volume/brightness feedback. Original app canvases are not modified.
* 280x240 landscape content is centred on 320x240 with 20-pixel black side bars.
  Portrait canvas dimensions use portrait panel orientation and centred margins.
* At most 10 captures/sec, plus transfer time. 20 MHz SPI; one persistent
  framebuffer, normally 134400 bytes, and an 8192-byte service stack.
* Snapshot locks are released before external SPI writes. The new service has
  the ordinary Service idle priority and does not block the built-in compositor
  during the wire transfer.
* While the built-in display is sleeping, blank the external image. Physical
  backlight power is not switched. Wake redraws the mirror.
* Initialization/allocation failure ends the PoC service, not the built-in UI.
  A write-only link cannot detect an absent/miswired panel reliably.

Limitations
-----------

This is a canvas-composition mirror, not a readback of the built-in LCD's GRAM.
Boot splash/card frames before services start and applications writing directly
to the physical display instead of retained canvases are not mirrored. No touch,
device selector, persistent configuration, runtime driver loading or Bluetooth
integration is included. The local minimal ILI9341 adapter reuses the existing
Arduino GFX bus/TFT primitives; cached dependencies contain ST7789 only, so no
new dependency download or shared dependency edit was needed.

Build profiles (not yet built or flashed)
---------------------------------------

The mirror is enabled by default in every existing v2 profile on this temporary
PoC branch. Keep your current build-runner profile; no special PoC profile or
hardware modification is required. Stock hardware uses ``v2``; existing modified
backlight/ADC profiles retain their original hardware flags and inherit the
mirror flag. Legacy v1 is unchanged. The older ``*-ili9341-mirror-poc`` profiles
remain compatibility aliases, but are not required.

Use the profile matching the actual hardware. The reviewed sibling SDK revision is
``8a1bbfa74a3e995ec8fd18e62c74f5986385e8cb``; the existing sibling library layout
and dependency versions are preserved. Target compilation/linking and library
resolution remain unverified until a firmware build is explicitly authorized.

Checks and physical trial
-------------------------

``python3 tests/display_mirror/check.py`` compiles the production adapter/service
against host mocks and checks RGB565 initialization, orientation/address windows,
centred framebuffer forwarding, sleep blanking, initialization failure and SPI
transfer outside snapshot locks. It does not certify real ESP32 compilation,
panel electrical behavior, timing or color order.

On hardware, first verify RGB startup colors, then Launcher/status bar and menu
mirroring, overlays and sleep/wake. Record orientation, actual colors, refresh
performance and SD/audio coexistence. If colors or orientation differ, adjust
the local adapter only. Firmware build, packaging and flashing are not performed
by this source-only implementation.
