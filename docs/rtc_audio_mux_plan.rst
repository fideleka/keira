Battery-backed RTC over multiplexed audio pins
==============================================

Status
------

This document records the hardware and firmware design for adding a
battery-backed I2C real-time clock to Lilka v2 without consuming extension,
UART, display, SD, or permanent audio GPIO capacity.  It is a design document;
the feature is not implemented yet.

Goals
-----

* Preserve all extension-header compatibility.
* Preserve future MAX98357A I2S audio support.
* Keep time while Lilka's mechanical power switch is off.
* Read the RTC once during early boot before normal peripheral initialization.
* Write the RTC only when the user explicitly sets time, or when a trusted NTP
  synchronization discovers an uninitialized RTC.
* Store UTC in the RTC and apply Keira's configured time zone only when
  displaying local time.
* Electrically isolate the RTC from high-speed I2S traffic whenever it is not
  being accessed.

Non-goals for the first version
-------------------------------

* RTC alarms or wake-from-power-off support.
* Using the RTC square-wave or 32 kHz outputs.
* Replacing NTP as the preferred source of authoritative time.
* Continuously polling the RTC while Keira is running.
* Sharing the display/SD SPI bus through additional chip-select decoding.

Why software-only pin reuse is unsafe
-------------------------------------

The unused-audio build can temporarily treat GPIO1 and GPIO2 as I2C, but the
future audio configuration uses the same pins for high-speed I2S::

    GPIO42  I2S BCLK
    GPIO1   I2S LRCK
    GPIO2   I2S data out

Leaving an RTC connected directly to GPIO1/2 would expose its SDA/SCL pins to
I2S traffic.  The RTC could interpret arbitrary audio transitions as I2C start,
address, or write sequences.  Merely stopping ``Wire`` or powering down an RTC
module in firmware does not guarantee electrical isolation and may allow
back-powering through input-protection structures.

The compact design therefore inserts a dual analog switch in only the RTC
branch.  Audio remains connected directly to the ESP32 at all times.

Compact hardware design
-----------------------

Primary components
~~~~~~~~~~~~~~~~~~

* one compact 3.3 V-compatible DS3231 I2C RTC module with backup cell;
* one TS5A23157 dual SPDT analog switch in VSSOP-10;
* one 1 Mohm resistor, 0603 or 0805;
* one 100 nF ceramic capacitor, 0603 or 0805;
* one 1 uF ceramic capacitor, 0603 or 0805;
* two 4.7 kohm pull-up resistors only if the RTC module does not already
  provide them.

The exact RTC module pinout, installed pull-ups, backup-cell chemistry, and
charging behavior must be verified before assembly.  The module is always
powered from Lilka's 3.3 V rail, never 5 V.

Signal topology
~~~~~~~~~~~~~~~

The MAX98357A audio signals remain directly connected::

    GPIO42 ------------------------------ MAX98357A BCLK
    GPIO1  ------------------------------ MAX98357A LRCK
    GPIO2  ------------------------------ MAX98357A DIN

The RTC branch passes through the two TS5A23157 channels::

    GPIO1/LRCK -------- COM1
                         |-- NC1 -------- RTC SCL
                         `-- NO1 -------- not connected

    GPIO2/DIN  -------- COM2
                         |-- NC2 -------- RTC SDA
                         `-- NO2 -------- not connected

    GPIO46/SLEEP ------- IN1 + IN2

For the standard TS5A23157 truth table, control LOW connects COM to NC and
control HIGH connects COM to NO.  The selected supplier datasheet must be
checked before layout or soldering.  If a compatible part uses the opposite
truth table, swap the NC and NO assignments; never connect the unused throw to
ground or power.

Switch states
~~~~~~~~~~~~~

================  ====================  ================================
GPIO46/SLEEP       RTC switch            Result
================  ====================  ================================
LOW               COM connected to NC   RTC connected to GPIO1/2
HIGH              COM connected to NO   RTC isolated; I2S operates normally
================  ====================  ================================

Connect a 1 Mohm resistor from GPIO46 to ground.  It weakly selects the RTC
path while GPIO46 is high-impedance during early reset without materially
loading the ESP32-S3 strapping pin.  This assumption must be checked on the
assembled board by verifying normal boot and download mode before relying on
the modification.

Power and decoupling
~~~~~~~~~~~~~~~~~~~~

::

    TS5A23157 V+  -------- 3.3 V
    TS5A23157 GND -------- GND
    100 nF --------------- directly across switch V+ and GND

    RTC VCC -------------- 3.3 V
    RTC GND -------------- GND
    1 uF ----------------- near RTC VCC and GND

The RTC remains powered while Lilka is on.  The module's backup cell keeps its
oscillator and registers alive when Lilka is switched off.

I2C pull-ups belong on the RTC side of the switch::

    RTC SDA ---- 4.7 kohm ---- 3.3 V
    RTC SCL ---- 4.7 kohm ---- 3.3 V

Most RTC modules already provide pull-ups.  Measure SDA-to-VCC and SCL-to-VCC
with power disconnected; a reading in the approximate 4.7--10 kohm range means
additional pull-ups are unnecessary.

Backup-cell safety
~~~~~~~~~~~~~~~~~~

Cheap RTC modules vary.  Before installation:

* identify the installed cell and whether it is rechargeable;
* identify any charging diode/resistor on the module;
* never allow a module to charge a non-rechargeable CR-series cell;
* do not replace the included yellow cell until its chemistry and charging
  circuit are understood.

Firmware architecture
---------------------

One service owns RTC transactions and the GPIO1/2 mode transition.  Other
applications must not manipulate the switch or start I2C on these pins
directly.

Conceptual interface::

    class RtcService {
    public:
        bool readEarlyUtc(time_t &utc);
        bool writeUtc(time_t utc, RtcWriteReason reason);
        bool isPresent() const;
        bool isValid() const;
    };

``RtcWriteReason`` initially distinguishes explicit manual changes from the
first trusted NTP initialization.  The first implementation does not rewrite
the RTC on every NTP poll.

Early boot sequence
-------------------

RTC reading must happen before normal I2S initialization::

    1. Drive GPIO46 LOW and allow the analog switch to settle.
    2. Keep GPIO42 inactive and detach any I2S routing from GPIO1/2.
    3. Configure GPIO2 as SDA and GPIO1 as SCL at 100 kHz.
    4. Probe the RTC and inspect its oscillator-stop/validity status.
    5. If valid, read UTC and initialize the ESP32 system clock.
    6. Stop I2C and return GPIO1/2 to a safe high-impedance state.
    7. Drive GPIO46 HIGH to isolate the RTC.
    8. Continue normal board, display, and I2S initialization.

RTC absence, invalid data, or an I2C timeout must never block boot.  Keira
continues with its existing unsynchronized-clock behavior until manual or NTP
time becomes available.

Runtime write sequence
----------------------

RTC writes are rare and coordinated with audio and display ownership::

    1. Acquire the RTC/audio transition mutex.
    2. If audio is active, defer the write or stop it cleanly.
    3. Disable and detach I2S from GPIO1/2/42.
    4. Put GPIO1/2 in a safe high-impedance state.
    5. Drive GPIO46 LOW; this isolates/mutes normal peripherals and connects
       the RTC branch.
    6. Wait briefly for switch and I2C levels to settle.
    7. Start I2C on SDA GPIO2 and SCL GPIO1.
    8. Write UTC and verify it by reading back.
    9. Stop I2C and return GPIO1/2 to high impedance.
    10. Drive GPIO46 HIGH to disconnect the RTC.
    11. Restore display state and I2S routing/audio if needed.
    12. Release the transition mutex.

A short display blank during this rare operation is acceptable.  A write must
not interrupt active audio unless the audio owner supports a clean pause and
resume; otherwise it remains queued until audio becomes idle.

GPIO46 and power-saving coordination
------------------------------------

GPIO46 currently controls Lilka's peripheral sleep behavior.  With this
hardware modification, driving GPIO46 LOW also connects the RTC to GPIO1/2.
Therefore every code path that enters power-saving mode must guarantee that
I2S is stopped and GPIO1/2 are safe before changing GPIO46.

The implementation must audit:

* SDK startup audio initialization;
* Keira audio player;
* MadPlayer;
* LilTracker;
* any direct I2S use;
* ``Board::enablePowerSavingMode()`` and ``disablePowerSavingMode()``.

If this coordination requires generic SDK behavior changes, those changes
belong in a matching SDK feature branch.  Keira and the modified SDK must then
be treated as one integration unit; no SDK hardening fix should live only on a
Keira staging branch.

Time policy
-----------

The RTC stores UTC only.

At boot
    If RTC data is valid, initialize the system clock from it.  The configured
    Keira POSIX time-zone rule converts UTC to local display time.

After trusted NTP synchronization
    If the RTC is absent or already valid, do not perform an unnecessary
    write.  If the RTC is present but uninitialized/invalid, write the trusted
    UTC time once and verify it.

After a manual time change
    Update the system clock and RTC UTC value, then verify the RTC readback.

Periodic drift correction is deliberately deferred until real hardware drift
has been measured.  It can later write only when the RTC differs from NTP by a
meaningful threshold.

RTC validity
------------

For DS3231, validity should use both:

* the oscillator-stop flag (OSF); and
* bounded calendar fields after BCD conversion.

Do not decide validity solely from a plausible year.  Clear OSF only after a
successful trusted-time write and readback.

Implementation phases
---------------------

Phase 0: bench hardware proof
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1. Assemble RTC, TS5A23157, pull-down, and decoupling on an adapter board.
2. Verify the exact TS5A23157 truth table and module pull-ups.
3. Confirm GPIO46 LOW connects both I2C lines.
4. Confirm GPIO46 HIGH isolates both lines with a multimeter or logic analyzer.
5. Verify normal boot and USB download mode with the 1 Mohm GPIO46 pull-down.
6. Exercise I2S while monitoring the RTC side; no audio transitions may cross
   the disabled switch.

Acceptance: RTC I2C works at 100 kHz when selected, the isolated side remains
quiet during I2S, and all normal Lilka boot modes still work.

Phase 1: early read
~~~~~~~~~~~~~~~~~~~

1. Add the RTC transaction helper and DS3231 read/validity support.
2. Read before audio initialization.
3. Initialize the system UTC clock from a valid RTC.
4. Fail open when the module is absent or invalid.

Acceptance: after a hard power-off, Keira boots with correct UTC-derived local
time without Wi-Fi and still boots normally when the RTC is unplugged.

Phase 2: trusted writes
~~~~~~~~~~~~~~~~~~~~~~~

1. Add manual-time write and readback.
2. Initialize an invalid RTC after trusted NTP synchronization.
3. Queue writes while audio is active.
4. Expose concise logs/status for missing, invalid, read, and write states.

Acceptance: manual and first-NTP writes survive hard power-off and do not
corrupt audio, display, or I2C state.

Phase 3: sleep/audio integration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1. Centralize GPIO46 transitions through the RTC/audio coordinator.
2. Update every power-saving path to quiesce I2S first.
3. Test all audio applications and repeated sleep/wake cycles.

Acceptance: no switch connection occurs while GPIO1/2 carry I2S, and audio can
resume cleanly after an RTC transaction or sleep cycle.

Validation matrix
-----------------

* cold boot from RTC with Wi-Fi unavailable;
* boot with no RTC installed;
* boot with invalid/stopped RTC oscillator;
* first NTP synchronization initializes an invalid RTC;
* subsequent NTP synchronization avoids unnecessary writes;
* manual time setting writes and verifies RTC;
* UTC remains unchanged when the configured time zone changes;
* hard power-off for at least 24 hours preserves time;
* twenty RTC transaction cycles do not hang I2C or I2S;
* active audio defers rather than races an RTC write;
* MAX98357 audio is clean while the RTC switch is open;
* GPIO46 power-saving transitions remain safe;
* normal boot and download modes remain available;
* no measurable RTC pull-up loading remains on GPIO1/2 while isolated;
* Keira and any companion SDK branch pass their exact format, static-analysis,
  localization, and v2 build gates;
* flash, static RAM, persistent heap, task stack, and boot-time deltas are
  measured against a clean baseline.

Repository ownership
--------------------

Keira's RTC service, Clock/NTP integration, manual-time behavior, transaction
coordination, and UI belong to ``feature/rtc-support``.

Any generic change to ``lilka::begin()``, audio pin initialization,
``Board::enablePowerSavingMode()``, or SDK-level I2S ownership belongs in a
separate SDK feature branch.  The branches are merged into staging only after
hardware proof and independent builds are clean.

