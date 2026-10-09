# RTC BCLK-detector review and prototype shopping list

**Current wiring/assembly reference:** [complete guide and diagrams](README.md). The BCLK detector is now the selected documentation/ordering design; bench proof and firmware are still pending.

## Review result

**Feasible as a firmware-coordinated bench prototype; not plug-and-play or hardware-qualified.** This arrangement preserves the extension header, piezo buzzer, buttons, backlight PWM and I2S speaker. No firmware implementation or physical installation is authorized by this parts-list review.

Use a **non-inverting Schmitt buffer**, not a bare RC connection to the analog-switch control. TS5A23157 at 3.3 V requires HIGH >= 0.7 × VCC (2.31 V); averaging a 50% clock gives only about 1.65 V. A Schottky peak detector instead charges a holding capacitor, and the buffer restores a clean rail-to-rail selector.

The buffer output has comfortable margins for the switch controls: at a load below 100 µA, TI specifies VOH >= VCC − 0.1 V and VOL <= 0.1 V. The two switch inputs plus the proposed 100 kΩ output pull-down are about 35 µA maximum at 3.3 V.

## Additional parts to get

| Required quantity | Part | Exact type / package | Notes |
| --- | --- | --- | --- |
| 1 | Schmitt buffer | **TI SN74LVC1G17DBVR**, SOT-23-5 / DBV | Non-inverting, powered at 3.3 V. Buy 2 if you want a spare. Do not substitute the inverting 1G14 without redesigning polarity. |
| 1 | Schottky diode | **Vishay BAT43**, axial DO-35 (SOD-27) | Easy through-hole soldering; band marks cathode. Buy 2–3 for spares. Ordinary 1N4148 is not the specified substitution. |
| 1 | Adapter PCB | **SOT-23-5 / 0.95 mm pitch to 2.54 mm**, or pictured SOT2310 carrier | The pictured 0.95 mm 10-pad carrier can use its middle positions after dry-fit; see the adapter diagram. A separate physical board is required for each IC. The 0.5 mm face is for the TS5A23157, not this buffer. |

Thin insulated hookup wire and a small insulating/perfboard mounting area are also needed if not already available. No order or purchase has been placed.

## Resistors and capacitors from your existing stock

| Reference | Quantity | Value / specification | Function |
| --- | --- | --- | --- |
| R1 | 1 | 1 kΩ, 1% preferred | Limits charging load on BCLK; added branch only, not in series with amplifier BCK. |
| R2 | 1 | 47 kΩ, 1% preferred | Discharges detector envelope. |
| R3 | 1 | 100 kΩ | Pulls selector LOW while buffer output is high-impedance. |
| Cenv | 1 | 100 nF / 0.1 µF, X7R ceramic, 10 V or higher, ±10% preferred | Envelope hold capacitor. |
| Cbuf | 1 | 100 nF ceramic, 10 V or higher | Buffer supply decoupling, physically at pins 5 and 3. |

Original RTC parts remain necessary: 100 nF across TS5A23157 V+ / GND, and 1 µF near RTC + / −. Add 4.7 kΩ SDA/SCL pull-ups only if the module's existing 472 resistors are verified not to supply them. The envelope and supply capacitors are separate parts; do not combine their functions.

## Candidate circuit

```text
Lilka GPIO42 / J5 BCK ── R1 1k ── BAT43 ── ENV ── buffer A (pin 2)
                                   band →    │
                                              ├── R2 47k ── GND
                                              └── Cenv 100nF ── GND

SN74LVC1G17DBVR:
  pin 1 NC   = leave open
  pin 2 A    = ENV
  pin 3 GND  = system GND
  pin 4 Y    = TS5A23157 IN1 (pin 1) + IN2 (pin 5)
             = R3 100k to GND
  pin 5 VCC  = 3.3 V, Cbuf 100nF directly to pin 3
```

BAT43 anode faces R1/BCLK; cathode (band) faces ENV. The amplifier's BCK connection remains direct and unchanged. Buffer HIGH selects the switch's unused NO contacts, isolating RTC. Buffer LOW selects NC1/NC2, connecting the RTC, as in the verified component pinout sheet.

## Nominal model, not measured hardware

Using constant diode drop 0.33 V, ideal 3.3 V clock, R1=1 kΩ, R2=47 kΩ and Cenv=100 nF:

- BCLK held HIGH: envelope approaches about 2.91 V.
- Continuous 50%-duty BCLK at 256 kHz through 3.072 MHz: envelope minimum is about 2.85 V.
- Discharge time constant: 4.7 ms.
- After 20 ms LOW, with pessimistic +5 µA buffer-input leakage: about 0.27 V.
- Initial current from BCLK is limited to less than approximately 3.3 mA by R1.

These are simplified model results, not guaranteed bounds across diode temperature behavior, capacitor bias/tolerance, PCB parasitics, actual ESP32 drive strength or 3.3 V Schmitt thresholds. In particular, BAT43's 0.33 V maximum is specified at 2 mA / 25°C; it is not a universal fixed drop. The buffer table gives threshold limits at discrete supply voltages, so do not treat interpolation to 3.3 V as a manufacturer guarantee.

## Required firmware and bench acceptance

Provisional firmware waits: hold BCLK HIGH for **2 ms before starting audio**, and hold BCLK LOW for **20 ms before starting I2C**. GPIO1/2 must stay quiet/safe throughout both waits. Actual measured settling must confirm these choices before release.

Every SDK, Lilplayer, pause/resume, guest and direct-I2S path must honor this sequencing. The first real BCLK edges are **not** sufficient protection: without pre-isolation, several audio transitions could reach the RTC while the envelope charges. During an RTC write, I2S must be fully stopped and GPIO42 under GPIO control, not left routed to the peripheral.

Acceptance needs a scope or suitable logic analyzer: verify BCLK integrity, actual ENV level, buffer/switch selection before the first audio edge, RTC-side silence during playback, quiet GPIO1/2 during reconnection, and startup/pause/resume/error/reset behavior. Check the GPIO42 JTAG/default state and explicitly establish the desired state at boot. Test for speaker clicks. If the high margin or clock loading is inadequate, revise the detector before connecting the RTC. Existing firmware is not ready for this modification.

## Manufacturer sources

- [TS5A23157](https://www.ti.com/lit/ds/symlink/ts5a23157.pdf): select-input thresholds, pinout and truth table.
- [SN74LVC1G17](https://www.ti.com/lit/ds/symlink/sn74lvc1g17.pdf): DBV pinout (1 NC, 2 A, 3 GND, 4 Y, 5 VCC), Schmitt thresholds, output levels and input leakage. DBV has 0.95 mm lead pitch.
- [Vishay BAT42/BAT43](https://www.vishay.com/docs/85660/bat42.pdf): BAT43 forward-drop limits, capacitance and DO-35 dimensions.
