# Experimental compressed external-display link

Decision record: 2026-10-15. **Documentation only; no implementation or measured
link performance yet.** This is a separate educational experiment, not a
replacement for the existing external-display driver.

## Purpose and scope

Anton wants a fun embedded-systems learning project: explore high-speed data
reception, DMA, framebuffers, compression, transport reliability and external
screen output. Minimizing controller count is not the primary objective.

The architectural direction discussed is:

```text
Lilka ESP32-S3
    | raw pixels, short local connection
    v
Encoder MCU beside Lilka
    | compressed transport (link A)
    v
Decoder MCU beside the screen
    | short local display connection (link B)
    v
External LCD
```

The extra encoder moves compression work off Lilka. It does not remove the
bandwidth requirement of the uncompressed Lilka-to-encoder connection. The
same-resolution use case is intentional; local scaling of a smaller native game
frame is an optional later experiment, not a prerequisite.

**Immediate agreed next step:** connect the available RP2350 to Lilka's extension
header and measure reliable raw reception into RAM. Do not start with compression
or the LCD attached to the data path: isolate the first link's actual limits.

## Available hardware and provisional roles

| Board | Initial role | Relevant constraints |
|---|---|---|
| WeAct Studio RP2350A_V10, reported 16 MB version | First receiver experiment; later encoder candidate | RP2350 has 520 KB internal SRAM; external flash is not framebuffer RAM |
| Purple RP2040 Pico-compatible board, described as “16M” | Later decoder candidate | RP2040 has 264 KB internal SRAM; verify whether the board's flash label means megabytes or megabits |

No additional controller purchase is required for the first experiment. A second
RP2350 was suggested as a later buffering/headroom upgrade, not a decided
purchase. STM32H7 and ESP32-S3 were discussed as alternatives, not selected.

At 320 × 240 RGB565, a frame occupies 153,600 bytes (150 KiB). Two frames occupy
300 KiB. The RP2040 cannot keep two complete frames in internal SRAM; one frame
plus small receive/tile buffers is the starting decoder concept. RP2350 SRAM can
hold two such frames with space left, but actual firmware, DMA and compression
working-memory budgets must still be established.

## First experiment: exact wiring

Lilka is the SPI master; RP2350 receives as slave. The following pins expose
RP2350 **SPI0**. PIO is an alternative implementation to evaluate later; merely
having PIO does not prove reliable 80 MHz externally clocked reception.

| Lilka extension J2 contact | Lilka signal | WeAct RP2350 pin | Direction |
|---|---|---|---|
| 10 | GPIO12 / SCK | GP2 / SPI0_SCK | Lilka → RP2350 |
| 9 | GPIO13 / MOSI | GP4 / SPI0_RX | Lilka → RP2350 |
| 8 | GPIO14 / CS | GP5 / SPI0_CSn | Lilka → RP2350 |
| 1 | GND | GND between GP2 and GP1 | Common reference |

The one-row Lilka extension contact sequence, counting from its square PCB pad:

```text
Contact:  1    2     3   4   5   6   7   8   9  10   11    12
Signal:  GND  3V3   RX  TX  48  47  21  14  13  12  3V3  GND
```

Contact numbers and GPIO numbers are different. Identify the square pad before
counting; the module mating view is mirrored.

For the supplied WeAct pinout image, with USB at the bottom, use the lower-right
edge:

```text
Right edge, top to bottom in this area:

GP6   o
GND   o
GP5   o  <- Lilka GPIO14 / J2 contact 8  (CS)
GP4   o  <- Lilka GPIO13 / J2 contact 9  (data)
GP3   o     unused
GP2   o  <- Lilka GPIO12 / J2 contact 10 (clock)
GND   o  <- Lilka GND    / J2 contact 1
GP1   o
GP0   o
       USB
```

Power both boards independently through USB for the initial test. Connect common
GND, but **do not join 3V3, 5V, VIN or VBUS**. All signal levels are 3.3 V. Keep the
local wires short and give clock/data a close ground return. Do not connect the
FM module or other devices that use these same GPIOs during this test.

An extra receiver-to-Lilka READY GPIO was suggested for backpressure. Its exact
pins and handshake are **not assigned yet**. No MISO connection is required for
the initial one-way payload test; choose a return/status mechanism before
claiming automatic end-to-end error reporting.

## Measurement plan before compression

1. Establish reception of a single small known block at a low clock.
2. Transmit numbered blocks with deterministic patterns and a CRC; check missing,
   duplicated, reordered, truncated and corrupt blocks at the receiver.
3. Sweep requested clocks such as 1, 5, 10, 20 and 40 MHz, then higher only if
   supported and stable. Record actual clock, not just the requested setting.
4. Exercise patterns such as all zero, all one, alternating bits, incrementing
   bytes and a reproducible pseudorandom stream. Test framing across blocks and
   DMA-buffer boundaries, not just isolated successful transfers.
5. Measure sustained useful payload bytes/s, error counts, test duration and
   receiver buffer overruns. Record wiring length, block sizes and implementation
   (hardware SPI or PIO). An achieved clock alone is not an FPS measurement.
6. Introduce a slow receiver and verify bounded behavior. Use pacing for the
   bring-up test; define flow control or explicit drops before sustained
   streaming can overrun buffers.
7. Only after raw reception is reliable, add compression and measure encoding,
   transport, decoding and LCD output independently, followed by end-to-end
   latency and displayed frame rate.

SPI mode, transaction framing, DMA strategy, block sizes, CRC variant, test
length/pass criteria, READY protocol and tooling remain implementation choices.
No particular frequency has passed a hardware test in this project yet.

## Throughput constraints and experimental targets

80 MHz refers to the serial bus clock, **not 80 FPS**. For one data bit per SPI
clock, 80 MHz provides a theoretical 10 MB/s before overhead.

For complete 320 × 240 RGB565 images:

| Quantity | Ideal value, excluding overhead |
|---|---:|
| Bytes per frame | 153,600 |
| 30 FPS payload | 4.608 MB/s |
| 60 FPS payload | 9.216 MB/s |
| Full-frame ceiling at 80 MHz single-line SPI | 65.1 FPS |

60 FPS leaves little headroom on an 80 MHz single-line raw connection. Compression
on the middle link cannot repair a slow raw input connection. A fast decoder LCD
connection also cannot create new source frames faster than they arrive. Panel
refresh/scanout and tearing impose additional constraints beyond transfer rate.

For the compressed middle link, ideal minimum compression ratios at 60 FPS are:

| Link A clock, single-line SPI if selected | Raw link bandwidth | Minimum ratio |
|---|---:|---:|
| 10 MHz | 1.25 MB/s | 7.37:1 |
| 20 MHz | 2.5 MB/s | 3.69:1 |
| 40 MHz | 5 MB/s | 1.84:1 |

Real requirements are higher because of protocol/transfer overhead. These numbers
are arithmetic ceilings, not forecasts of compression ratios or hardware results.
A 30 FPS baseline followed by pursuit of 60 FPS was suggested; Anton has not set a
hard guaranteed FPS/resolution requirement. 320 × 240 RGB565 is the reference
workload for planning, not a finalized product specification.

Short local connections at the encoder and LCD are desirable. They do not by
themselves guarantee 80 MHz operation: peripheral slave timing, signal integrity,
PIO sampling, DMA and the actual LCD controller/module must be validated.

## Compression and transport ideas — not finalized

The preferred initial software experiment discussed was changed tiles, e.g.
16 × 16 pixels, with a per-tile choice of solid colour, palette, RLE or raw data.
The decoder retains a framebuffer. Raw fallback prevents relying on every tile
being compressible; it does not make an incompressible full frame fit a slow link.

- Compare menus/pixel art against scrolling games and detailed moving scenes.
- LZ4 is a later lossless candidate, not a selected dependency.
- JPEG was not preferred for the initial pixel-art/text workload.
- Measure comparison/encoding CPU cost and memory as well as transmitted bytes.
- Decide whether to send partial LCD regions or full reconstructed frames; this
  changes the decoder-to-LCD bandwidth demand.
- Define bounded buffers, frame sequencing, CRC, resynchronization and full-frame
  refresh/keyframe behavior. Lost deltas must not silently corrupt future frames;
  drops require coherent recovery and never reuse an in-flight DMA buffer.
- Overlap stages only after ownership and backpressure are explicit. Buffering
  can smooth timing but may increase latency.

Compression is a bandwidth technique, not an error-correction mechanism. A
reliable system still needs an explicitly designed framing/recovery protocol.

## Status and authorization boundary

This branch records decisions and separates them from suggestions and unknowns.
No Keira/SDK/RP2350/RP2040 firmware was changed, built, packaged or flashed for
this record. No dependencies were installed and no controller/LCD throughput was
measured. Existing firmware branches, staging, display settings and FM wiring are
unchanged. Firmware work and physical measurements are the next separate task.
