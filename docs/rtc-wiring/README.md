# Lilka RTC modification: BCLK-selected DS3231

**Revision: 2026-10-09. Selected design for documentation and parts ordering; firmware and hardware validation are still pending.**

This replaces the original GPIO46/SLEEP selector in `feature/rtc-support`. It preserves every extension-header signal, the piezo buzzer, buttons, PWM backlight, UART, and direct I2S amplifier wiring. It adds an isolated DS3231 branch to GPIO1/2 and derives the switch selector from GPIO42/BCK through a peak detector and a non-inverting Schmitt buffer.

## Diagrams

Open [the complete standalone diagram pack](rtc-wiring.html) in a browser, or individual editable sheets:

- [Full circuit](rtc-circuit.svg) / [PNG](rtc-circuit.png).
- [Chip and adapter placement](rtc-adapters.svg) / [PNG](rtc-adapters.png).
- [Lilka rear-board connection map](rtc-board-points.svg) / [PNG](rtc-board-points.png).

The board map uses actual Lilka KiCad pad coordinates, horizontally mirrored for the rear/amplifier-side view. It is not an annotation of a current rear-board photo. Pad numbering runs **1 above 7** with the extension header at the top in that view. Follow the amplifier labels and continuity checks, not an assumed viewing direction. The local source is `lilka/hardware/v2/main.kicad_pcb`; Anton's PCB is marked 2.3. The installed amplifier may obscure some joints; physical access must be checked on the actual assembly.

## What to order

| Part | Required | Suggested purchase | Exact requirement |
| --- | ---: | ---: | --- |
| Non-inverting Schmitt buffer | 1 | 2 | **TI SN74LVC1G17DBVR**, DBV / SOT-23-5, 0.95 mm pitch |
| Schottky diode | 1 | 2–3 | **Vishay BAT43**, axial DO-35 / SOD-27 |
| Buffer carrier | 1 separate board | Only if needed | SOT-23-5 breakout, or the pictured **SOT2310 / 0.95 mm** 10-pad carrier after dry-fit |

Already owned: DS3231 module, TS5A23157DGS (`JBR` marking), its SOP10 / 0.5 mm carrier, resistors and capacitors. **Two ICs need two separate physical carriers.** A two-sided 0.5/0.95 mm board can be used for either IC, not both at once, because its header nets are shared.

| Reference | Value | Purpose |
| --- | --- | --- |
| R1 | 1 kΩ, 1% preferred | Series resistor in the detector branch only |
| R2 | 47 kΩ, 1% preferred | ENV discharge |
| R3 | 100 kΩ | SELECT pull-down |
| Cenv | 100 nF X7R, ≥10 V, ±10% preferred | Envelope hold |
| Cbuf | 100 nF ceramic, ≥10 V | Buffer supply decoupling |
| Csw | 100 nF ceramic, ≥10 V | Analog-switch supply decoupling |
| Crtc | 1 µF ceramic, ≥10 V | RTC supply decoupling |
| Optional pull-ups | 2 × 4.7 kΩ | Only if module SDA/SCL pull-ups are absent |

Cenv, Cbuf and Csw are **three separate 100 nF parts**. Keep supply capacitors close to each chip. Also use thin insulated wire, heat-shrink/insulation and strain relief; do not leave floating bare leads inside Lilka.

Do not substitute an inverting SN74LVC1G14 or a plain 1N4148 without reviewing the circuit again.

## Five wires to Lilka

All wires are additional branches at the existing amplifier signal-row joints. **Do not cut or route the amplifier signals through the switch.**

| Lilka solder point | Signal | New connection |
| --- | --- | --- |
| J5 pad 1, LRCK | GPIO1; reused as I2C SCL | U1 pin 10 COM1 |
| J5 pad 2, BCK | GPIO42 | R1 input |
| J5 pad 3, DIN | GPIO2; reused as I2C SDA | U1 pin 6 COM2 |
| J5 pad 6, GND | System ground | U1/U2/RTC ground, R2/R3 and capacitor returns |
| J5 pad 7, VIN | **+3V3 on Lilka** | U1/U2/RTC power and RTC-side pull-ups |

Leave J5 pad 4 GAIN and pad 5 SD/SLEEP untouched. Pad 5's baseboard net is GPIO46; it must not select the RTC because it now carries backlight PWM. Do not undo the earlier amplifier-SD isolation. Speaker OUT− is a driven speaker terminal, **not ground**. Do not power this circuit from USB 5 V or the battery pads.

## Exact pin-by-pin netlist

### U1 — TS5A23157DGS, VSSOP-10, 0.5 mm

All numbers are **IC pins**, not arbitrary carrier positions.

| IC pin | Name | Connect to |
| ---: | --- | --- |
| 1 | IN1 | SELECT; tie to U1 pin 5 and U2 pin 4 |
| 2 | NO1 | Leave open |
| 3 | GND | System GND |
| 4 | NO2 | Leave open |
| 5 | IN2 | SELECT; tie to U1 pin 1 and U2 pin 4 |
| 6 | COM2 | J5 DIN / GPIO2 |
| 7 | NC2 | RTC `D` / SDA |
| 8 | V+ | +3V3 |
| 9 | NC1 | RTC `C` / SCL |
| 10 | COM1 | J5 LRCK / GPIO1 |

Csw: U1 pin 8 to pin 3. R3: SELECT to GND. No GPIO46 wire or original 1 MΩ GPIO46 pull-down is used for this design.

LOW at SELECT connects COM to NC and therefore the RTC. HIGH connects COM to the unused NO contacts and therefore isolates the RTC. Unused NO contacts must **not** be grounded or tied to supply. The two select inputs are tied together; the two COM signals are not.

### U2 — SN74LVC1G17DBVR, SOT-23-5

| IC pin | Name | Connect to | Pictured 0.95 mm carrier pad |
| ---: | --- | --- | ---: |
| 1 | NC | Leave open | 2 |
| 2 | A | ENV | 3 |
| 3 | GND | System GND | 4 |
| 4 | Y | SELECT = U1 pins 1 + 5; R3 to GND | 7 |
| 5 | VCC | +3V3 | 9 |

**Carrier orientation for this mapping:** as in Anton's photo, top row left-to-right `10 9 8 7 6`, bottom row `1 2 3 4 5`. The buffer's three-leg side faces the bottom row; its pin-1 mark faces bottom-left. Use the middle three positions; top centre pad 8 and carrier pads 1/5/6/10 remain unused.

Cbuf: IC pin 5 to pin 3, equivalent to carrier pad 9 to pad 4. Dry-fit the chip: matching 0.95 mm pitch does not prove the row spacing or land lengths fit. Confirm the lead-to-header mapping with continuity, then solder.

### Detector

```text
J5 BCK / GPIO42 → R1 1k → D1 BAT43 anode
D1 cathode (banded end) → ENV
ENV → U2 pin 2 A
ENV → R2 47k → GND
ENV → Cenv 100nF → GND
U2 pin 4 Y → SELECT → U1 pins 1 and 5
SELECT → R3 100k → GND
```

D1's **band faces ENV**, away from the BCK input. The existing BCK connection to the amplifier stays direct. ENV is an analog capacitor voltage; SELECT is a buffered digital logic level. They must not be shorted together.

### RTC module, labelled `+ D C NC −`

| Module label | Connect to |
| --- | --- |
| `+` | +3V3 |
| `D` | U1 pin 7 NC2 / SDA |
| `C` | U1 pin 9 NC1 / SCL |
| `NC` | Leave open |
| `−` | System GND |

Crtc: module `+` to `−`. This module's `NC` means **no connection**, not the analog switch's normally-closed contacts.

The photographed `472` resistors are consistent with 4.7 kΩ pull-ups. With **all supply and backup-cell power removed**, verify D-to-+ and C-to-+ and inspect the nets. In-circuit readings can include parallel paths. Add pull-ups only when their absence is established; keep both on the **RTC side** of U1.

DS3231 itself contains no battery charger. The module can still have external charging circuitry: identify the yellow cell chemistry and check the board before installation. Do not allow charging of a non-rechargeable cell.

## How selection works

| BCK state | Settled SELECT | RTC connection |
| --- | --- | --- |
| Intentionally held HIGH | HIGH | Isolated |
| Running I2S clock | HIGH, if detector validated | Isolated |
| Intentionally held LOW, after ENV discharges | LOW | Connected |
| Boot, reset or a transition | Not assumed | GPIO1/2 must stay quiet until state is established |

The detector is a peak holder, **not a plain RC average**. A 50%-duty average near 1.65 V does not meet the TS5A23157 HIGH requirement at 3.3 V. The non-inverting Schmitt buffer restores clean selector levels.

The nominal simplified model gives ENV ≈2.85 V during continuous clocks and a 4.7 ms discharge time constant. This is not a guaranteed voltage margin across temperature, rail tolerance, real diode drop, buffer input leakage and capacitor tolerance. See [the detailed electrical review](bclk-detector-bom.md).

## Firmware contract — not implemented yet

**Existing firmware must not operate audio with the RTC installed on this shared bus.** The branch currently documents a design, not an implementation.

Before any I2S start or resume:

1. Take a shared RTC/audio transition lock and finish/stop I2C.
2. Stop/detach conflicting peripheral routing; keep GPIO1/2 quiet and safe.
3. Take GPIO42 under GPIO control and hold BCK HIGH.
4. Wait **2 ms provisionally** for detector isolation; hardware measurements must qualify the actual wait.
5. Configure and start I2S. The hand-off must not introduce a LOW interval long enough to release the detector.

Before any RTC transaction:

1. Defer while audio is active unless its owner supports a coordinated stop/resume.
2. Stop and detach I2S on GPIO1/2/42; put GPIO1/2 in a safe high-impedance state.
3. Hold BCK LOW under GPIO control.
4. Wait **20 ms provisionally** for ENV release and RTC-side pull-ups to settle.
5. Start I2C at 100 kHz: **SDA GPIO2, SCL GPIO1**, DS3231 address **0x68**.
6. Perform the bounded transaction, stop I2C, and return GPIO1/2 to a quiet safe state.
7. Re-isolate using the HIGH sequence before restoring audio.

GPIO1/2 being quiet during transitions includes possible pull-up-driven level changes; these must not create an unintended RTC transaction. Reset/startup, driver errors and peripheral hand-offs need explicit validation. The first I2S clock pulses alone are **not** a safe isolation sequence. A held-HIGH BCK is a deliberate guard, not continuous clocking; verify that changing it does not cause speaker clicks.

Every SDK, Lilplayer, guest/direct-I2S, pause/resume, sleep/wake and error path must follow the same contract. GPIO42's reset/JTAG/default state must be audited. None of this is supplied by merely wiring the detector.

RTC policy remains: store UTC; use OSF plus bounded calendar checks for validity; fail open on missing/invalid RTC; initialize system time early; write after explicit manual time changes or first trusted NTP initialization of an invalid RTC. Time-zone changes do not rewrite UTC. Generic SDK audio ownership belongs in a companion SDK feature branch, not only Keira.

## Assembly and bench checks

1. Disconnect USB and Lilka's battery before soldering. Verify the carrier orientation and every power/ground pin first; inspect for bridges under magnification.
2. Assemble the detector and switch on separate carriers. Verify resistor values, diode direction, open unused contacts and supply decoupling before connecting Lilka.
3. Confirm the five Lilka pads by labels/net continuity. Preserve every existing amplifier connection; insulate and mechanically support the new assembly.
4. Validate on a bench setup with RTC disconnected from shared audio lines initially. Use a scope for ENV and BCK analog levels; a logic analyzer alone cannot establish analog voltage margin or signal integrity.
5. Prove SELECT is HIGH **before** the first audio transition; check BCK loading, startup, low-frequency clocks, pause/resume, reset/error paths and routing hand-offs.
6. Prove SELECT is LOW before I2C starts, verify 0x68 at 100 kHz, UTC read/write and backup retention. Monitor both RTC-side lines: no unintended I2S transitions may cross while isolated. Check for audible clicks.
7. Release only after electrical proof and coordinated firmware integration. No firmware build or flash was performed for this documentation update.

## Sources and regeneration

- [TI TS5A23157](https://www.ti.com/lit/ds/symlink/ts5a23157.pdf): DGS pinout, JBR marking, truth table and select thresholds.
- [TI SN74LVC1G17](https://www.ti.com/lit/ds/symlink/sn74lvc1g17.pdf): DBV pinout, Schmitt thresholds and output levels.
- [Vishay BAT42/BAT43](https://www.vishay.com/docs/85660/bat42.pdf): diode specifications and DO-35 package.
- [Analog Devices DS3231](https://www.analog.com/media/en/technical-documentation/data-sheets/ds3231.pdf): supply, pinout, I2C, OSF and backup behavior.
- Lilka `hardware/v2/main.kicad_pcb` at `03a68d6b1690508dcb253b79e740f04e0c669ef8`, schematic and SDK pin mapping; supplied module/carrier photos.

`generate_diagrams.py` uses only Python's standard library and reads the KiCad PCB to place board pads and assert the five selected net names:

```sh
python3 generate_diagrams.py --pcb /path/to/lilka/hardware/v2/main.kicad_pcb
rsvg-convert rtc-circuit.svg -o rtc-circuit.png
rsvg-convert rtc-adapters.svg -o rtc-adapters.png
rsvg-convert rtc-board-points.svg -o rtc-board-points.png
```

SVGs and the standalone HTML contain no remote assets. Printed diagrams show top/component-side chip views and a rear-side board view explicitly; do not silently mirror the chip pinout.
