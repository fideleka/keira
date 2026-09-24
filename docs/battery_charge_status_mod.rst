Lilka v2: charging-state detection through the battery ADC
============================================================

Purpose
-------

This optional hardware modification lets compatible firmware distinguish three
states without consuming any extension GPIOs:

* running from battery / USB absent;
* USB connected and the battery charging;
* USB connected and charge complete.

The existing ``VVAL`` input on ESP32-S3 GPIO3 continues to measure battery
voltage. Two TP4056 open-drain status outputs activate different pull-down
resistors through a dual optocoupler, encoding charger state as three widely
separated ADC bands.

This is a **prototype design**. Its theoretical bands are comfortable, but the
final firmware thresholds must be selected from measurements on an assembled
console.

* `Open the standalone schematic <_static/battery_charge_status_mod/schematic.html>`_.
* :download:`Download the purchase-ready BOM <_static/battery_charge_status_mod/BOM.csv>`.

Why the signal is pulled down
-----------------------------

Lilka v2 already uses the following divider::

    Battery/Q1 output -- R4 33k -- VVAL/GPIO3 -- R2 100k -- GND

The ADC-pin voltage is::

    VVAL = VBAT * 100 / (33 + 100) = VBAT * 0.7519

At 4.20 V, ``VVAL`` is theoretically about 3.16 V. Adding a positive
``+0.5 V`` or ``+1 V`` marker would saturate the ADC and could overvoltage
GPIO3. The safe direction is downward: each charger state temporarily adds a
known resistance between ``VVAL`` and ground.

Recommended circuit
-------------------

Use one EL827S/LTV-827/PC827-compatible dual phototransistor optocoupler.

TP4056/status side::

    VBUS -- 2.2k -- optocoupler LED channel 1 -- TP4056 CHRG
    VBUS -- 2.2k -- optocoupler LED channel 2 -- TP4056 STDBY

``CHRG`` and ``STDBY`` are active-low open-drain outputs. An optocoupler LED
turns on only when USB power is present and the corresponding TP4056 output is
sinking current.

ADC side::

    VVAL -- 33k -- channel 1 collector
                    channel 1 emitter -- GND

    VVAL -- 10k -- channel 2 collector
                    channel 2 emitter -- GND

Channel assignment:

* channel 1 / 33 kOhm: ``CHRG`` (charging);
* channel 2 / 10 kOhm: ``STDBY`` (charge complete).

Use 1% resistors for the two ADC-tag values.

Bill of materials
-----------------

Required electrical parts:

.. list-table::
   :header-rows: 1
   :widths: 10 25 35 30

   * - Quantity
     - Part
     - Specification
     - Notes
   * - 1
     - Dual optocoupler
     - EL827S, LTV-827, PC827 or compatible; SOP-8
     - Verify the delivered part's exact datasheet pinout.
   * - 2
     - Input resistor
     - 2.2 kOhm, 1/8 W or greater
     - 1% preferred; 5% is acceptable.
   * - 1
     - Charging tag resistor
     - 33 kOhm, 1%, 1/8 W or greater
     - Between ``VVAL`` and channel 1 collector.
   * - 1
     - Charged tag resistor
     - 10 kOhm, 1%, 1/8 W or greater
     - Between ``VVAL`` and channel 2 collector.
   * - As needed
     - Fine insulated wire
     - 30 AWG Kynar or enamelled wire
     - VBUS, GND, VVAL, CHRG and STDBY connections.

Recommended for hand assembly:

* SOP-8-to-DIP adapter or small SMD prototyping board;
* Kapton tape;
* fine heat-shrink or electronics-safe silicone for strain relief;
* no-clean flux.

Common EL827 pin arrangement
----------------------------

A common EL827/PC827 dual-channel arrangement is::

           notch / dot
           _________
      A1  1|       |8  C1
      K1  2| EL827 |7  E1
      K2  3|       |6  C2
      A2  4|_______|5  E2

``A/K`` are LED anode/cathode; ``C/E`` are phototransistor
collector/emitter.

**Do not solder from this drawing alone.** Confirm the exact manufacturer's
pinout. Equivalent-part drawings sometimes present channel 2 in a visually
reversed orientation.

Connection points on Lilka v2
-----------------------------

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Signal
     - Practical connection point
   * - ``VBUS``
     - TP4056 module ``IN+``, or Lilka J4 ``IN+``.
   * - ``GND``
     - TP4056 ``IN-``/``OUT-``, or any Lilka ground.
   * - ``VVAL``
     - Junction of R4 (33 kOhm), R2 (100 kOhm), and ESP32 GPIO3.
   * - ``CHRG``
     - Cathode/status side of the TP4056 charging LED, or TP4056 ``CHRG`` pin.
   * - ``STDBY``
     - Cathode/status side of the full-charge LED, or TP4056 ``STDBY`` pin.

The Lilka TP4056 footprint routes only ``IN+``, ``IN-``, ``OUT+``, and
``OUT-``. ``CHRG`` and ``STDBY`` therefore need fine wires to the module's LED
cathodes or controller pins.

Do not assume LED colour or orientation. Identify the nodes electrically:

* while charging, ``CHRG`` is near 0 V;
* after termination, ``STDBY`` is near 0 V;
* an inactive output is high-impedance and pulled upward through its LED
  circuit.

Expected ADC bands
------------------

Ignoring the small optocoupler transistor saturation voltage:

.. list-table::
   :header-rows: 1
   :widths: 25 25 25 25

   * - State
     - Effective lower divider
     - GPIO3 for VBAT 3.0--4.2 V
     - Normal-divider reconstructed value
   * - USB absent / battery
     - 100 kOhm
     - 2.26--3.16 V
     - 3.00--4.20 V
   * - Charging (33 kOhm)
     - 100k || 33k = 24.81 kOhm
     - 1.29--1.80 V
     - 1.71--2.40 V
   * - Charged (10 kOhm)
     - 100k || 10k = 9.09 kOhm
     - 0.65--0.91 V
     - 0.86--1.21 V

Starting classification bands, expressed using the normal-divider
reconstructed-voltage convention::

    below 0.5 V    battery absent / invalid
    0.5--1.45 V    charge complete
    1.45--2.7 V    charging
    above 2.7 V    normal battery divider / USB absent

Replace these theoretical thresholds with measured values after prototyping.

Firmware design
---------------

The charger-state API should be additive so legacy ``Battery::readLevel()``
semantics remain unchanged::

    enum class ChargeState {
        Unknown,
        Battery,
        Charging,
        Charged,
    };

    ChargeState readChargeState();
    float readDecodedVoltage();

Recommended sequence:

#. Take the existing median ADC sample.
#. Classify the analog band before full-level calibration.
#. With no tag, use the calibrated normal-divider voltage unchanged.
#. With ``CHRG`` active, invert the 33 kOhm tagged divider if an approximate
   charging voltage is useful.
#. With ``STDBY`` active, report ``Charged`` and 100%; exact voltage recovery
   is optional because optocoupler ``VCE(sat)`` matters with the 10 kOhm
   shunt.
#. Require several consistent samples before changing the displayed state.

For the user-facing feature, reliable **battery / charging / charged** state is
the goal. This circuit is not a fuel gauge and should not claim charging
percentage or remaining time.

Future automatic full calibration should happen after USB is disconnected and
the console is running from the fully charged battery, so neither status shunt
is active and the measured endpoint matches the normal operating path.

Power impact
------------

At 4.2 V, Lilka's original 133 kOhm divider draws about 31.6 uA. With USB
absent, both optocoupler LEDs and phototransistors are off, so the modification
adds only negligible phototransistor leakage.

While USB is connected, one input LED draws approximately::

    (5.0 V - 1.2 V) / 2.2 kOhm = 1.7 mA

This current comes from USB, not the battery. The additional tagged-divider
current remains below 0.1 mA.

Backward compatibility
----------------------

* Unmodified boards continue to produce the normal ADC range.
* New status methods should be additive.
* Modified hardware with old firmware will appear to have very low battery
  voltage while USB is connected; compatible firmware is required.
* Full-level calibration must be performed with USB disconnected.

Assembly procedure
------------------

#. Disconnect USB and physically disconnect the LiPo battery.
#. Mount the EL827S on an adapter/prototyping board and insulate its underside.
#. Fit the two 2.2 kOhm input resistors and 33 kOhm/10 kOhm tag resistors.
#. Confirm optocoupler orientation with the delivered datasheet and a diode
   test.
#. Connect VBUS and GND.
#. Connect channel 1 input to the verified ``CHRG`` node.
#. Connect channel 2 input to the verified ``STDBY`` node.
#. Connect both output emitters to GND.
#. Connect channel 1 collector to ``VVAL`` through 33 kOhm.
#. Connect channel 2 collector to ``VVAL`` through 10 kOhm.
#. Inspect for bridges and measure resistance from ``VVAL`` to GND before
   reconnecting power.
#. Reconnect the battery, power on without USB, and verify the original voltage
   reading is unchanged.

Prototype acceptance test
-------------------------

Record actual GPIO3 voltage and the raw ADC value in each state:

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Test
     - Expected result
   * - Battery only
     - Original reading unchanged; both optocouplers off.
   * - USB connected, charging
     - ``CHRG`` channel active; ADC enters the middle band.
   * - USB connected, charge complete
     - ``STDBY`` channel active; ADC enters the low band.
   * - USB disconnected again
     - ADC immediately returns to the normal battery band.
   * - Repeated plug/unplug
     - No reset, backfeed, or false charged indication.

Also verify:

* GPIO3 never exceeds its original battery-only voltage;
* VBUS is not measurably back-powered while USB is absent;
* TP4056 LEDs still behave normally;
* each status output remains within the TP4056 sink-current rating;
* state remains stable during Wi-Fi/display load changes.

Risks and limitations
---------------------

* Generic TP4056 modules differ in LED colours, resistors, layouts, and clone
  controller behaviour.
* Optocoupler CTR and ``VCE(sat)`` vary with device and temperature.
* Voltage while charging is an estimate, not fuel-gauge data.
* ``Charged`` means TP4056 charge termination, not measured cell capacity.
* TP4056 has no true power-path management; console load can affect charge
  termination.
* Fine wires to status LED pads are fragile and require strain relief.

Future PCB revision
-------------------

For a native Lilka revision:

* route ``CHRG`` and ``STDBY`` to labelled pads;
* place the dual optocoupler and four resistors on the main PCB;
* add resistor-value labels or assembly-option jumpers;
* keep ``VVAL`` accessible as a test point;
* document tagged ADC bands as part of the hardware revision contract.
