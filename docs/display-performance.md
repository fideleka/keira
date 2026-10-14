# Display performance improvements on the pre-zero-copy baseline

Base: `de02c7201a579feafec6def5bba3c48800faafe3` on
`feature/sd-device-drivers`. This does **not** restore the rejected zero-copy
pipeline: exact fused comparison/composition, shared presentation buffers,
partial updates, latest-frame-wins and overrun yielding remain unchanged.

## Changes

1. **NES controls:** poll on the emulator task before each emulated frame,
   including automatic-frameskip frames. Idle waiting does not spin-poll input.
   The quit action stops before another render. Existing menu, save/load,
   Select/Start ownership and turbo code remain; turbo sampling now follows
   emulation cadence rather than display cadence. No concurrent input mutation.
2. **NES renderer:** clear uncovered margins only, then overwrite the entire
   game rectangle. For the usual 256x240 game inside 280x240, avoid 122,880 bytes
   of redundant clearing per submitted frame. Clear margins every time to erase
   retained toast/menu pixels on alternating canvases. Correct source cropping,
   portrait raw strides and 270-degree mapping. Unused large-screen assumptions
   previously read outside the NES bitmap; all sizes now safely center/crop the
   native 256x240 image without inventing an unsupported upscale path.
3. **External DMA:** paired-word RGB565 endian packing when aligned; scalar
   fallback and exact odd tails. Two 4 KB internal staging buffers, one in-flight
   transaction, persistent descriptors. Pack N+1 while N transfers; wait before
   reusing a buffer, preserve blocking completion and DC boundaries. A failure
   after submission quarantines both buffers/descriptors/bus. Extra staging
   memory versus baseline is 4 KB; descriptor metadata also grows. Driver ABI,
   partial rows and the saved **40 MHz** setting are unchanged.
4. **Native LCD DMA:** Keira opts in to the matching SDK PixelDMABus at startup.
   Retains the Arduino SPI3 LCD/SD mutex, clock and command path; does not install
   a competing IDF SPI bus. Two 4,092-byte internal buffers and one descriptor,
   GDMA/interrupt/semaphore state owned for the display singleton lifetime.
   Completion is SPI wire-done, not DMA EOF; waits block instead of busy-polling
   the payload. Setup failure falls back to polling. Uncertain active completion
   restarts without releasing the shared SPI lock. No full-image allocation or
   hashing is added. See the SDK's `docs/native-lcd-dma.md` for platform bounds.

## Integration

Update **both** Keira `feature/sd-device-drivers` and sibling SDK
`feature/native-lcd-dma`. SDK branch is based on the existing consumer SDK
`8a1bbfa74a3e995ec8fd18e62c74f5986385e8cb`; no staging branch is advanced.
The additive SDK API keeps polling as the default for other SDK users.
Keira calls it explicitly. PlatformIO espressif32 6.12.0 / Arduino 2.0.17 /
ESP-IDF 4.4.7 ESP32-S3 is the reviewed target.

Rebuild and flash Keira with the matching SDK. **Keep the compatible ILI9341 SD driver 1.1.x**;
no driver module rebuild/version change is needed for these host-side transport
changes. Do not use the zero-copy 1.2.0 module. Retain 40 MHz and existing settings.
Boot should report `Native LCD pixels: DMA` (or an explicit polling fallback).
The normal 5-second `perf` / `perf-ext` logs remain available. No new logging
stream is added to burden the serial/audio/render hot paths.

## Validation and limitations

- Production NES blit compared to exact logical-pixel reference across four
  rotations, five canvas sizes/crops, repeated buffers and stale border pixels;
  normal and ASan/UBSan pass.
- Patched production emulator loop checks input before skipped/displayed frames,
  idle no-poll and quit-before-render. Full verified-overlay/IRQ suite passes.
- Production device-manager harness checks aligned/halfword source alignment,
  zero/odd/multi-chunk/full frames, wire byte order, buffers immutable in flight,
  packing-ahead overlap, submission recovery, timeout quarantine, allocation
  rollback, 40 MHz NVS, three modes, shared frames and repeated toggles.
- SDK production DMA source executes against host peripheral boundaries:
  wire-end/lock/register-restoration checks, odd chunk tails, initialization
  rollback and active-timeout fail-closed. **Not a hardware register simulation.**
- Existing row comparison (15,360 cases), overlay/menu/game/interlace/expiry,
  frame pacing/metrics and emulator-history checks pass. Localization passes.
- Installed Xtensa C++11 syntax checks pass for changed production consumers.
  Matching IDF archive exports the required GDMA symbols. Changed C++ formatting
  and new transport static checks pass. Full repository format/static checks
  retain unrelated baseline failures (statusbar/serial formatting, host-fixture
  ODR warnings/errors, SDK temporary-lifetime/overlay warnings).
- No full firmware link, package, flash or measured device speedup is claimed.

Compare **the same active moving scene** on baseline and each commit/integration,
Internal / External / Both, same clocks, audio, refresh selection and settings.
Check controller response, frame intervals, submitted frames versus actual LCD
updates, heap/stack, SD reads/writes and audio over a soak. Static scenes correctly
report zero updates; compositor loop FPS is not game FPS. Single Internal keeps
its baseline canvas-lock semantics, so native DMA cannot eliminate every wait
for a canvas swap. CPU time saved is not automatically an FPS increase.

The external 280x240 RGB565 full-payload floor remains 26.88 ms at 40 MHz; native
80 MHz floor is 13.44 ms. Commands, packing, wakeups and scheduling add overhead.
There is no 60 FPS guarantee and no numerical performance forecast until the
paired hardware comparison is available.

## External 80 MHz experimental choice (driver 1.1.1)

The ILI9341 settings now append `50`, `60`, `70` and `80 MHz (test)` after the unchanged10/20/40MHz
choices. Default20MHz, saved choice indices, UUIDs, ABI1 and settings schema1
are unchanged. Keira's SPI host accepts up to80MHz; speeds above80MHz are rejected.
No automatic clock increase is applied: an existing saved40MHz setting stays40.

Update Keira firmware **and** the SD driver (`driver.so` plus matching1.1.1
`manifest.json`), then choose Devices → ILI9341 display → SPI speed →
`80 MHz (test)`. The next `perf-ext` line reports both effective `spi-hz` and selected
`spi-requested-hz`. With this pinned80MHz APB/IDF divider,50/60MHz requests
configure40MHz, while70/80MHz requests configure80MHz. These are not four
distinct hardware clock rates; changing the dropdown alone cannot create them.
Keep40MHz available if the module/wiring produces corruption or flicker.

The SPI full-frame RGB565 payload-only floor becomes13.44ms for280x240,
versus26.88ms at40MHz. This is twice the requested wire bandwidth, not a promise
of twice the emulator FPS or hardware stability. Source/host tests cover passing
80MHz to IDF,all requested50/60/70/80MHz choices, effective-clock rounding and return to40MHz, preserved saved40MHz and upper-bound rejection.
No fresh driver package, full firmware link/flash or physical80MHz test was
performed during this source change. SDK source is unchanged.


## Follow-up: conversion, capture traffic and fractional pacing (2026-10-14)

Source baseline for this follow-up: Keira `2736d91`; SDK remains `bdef714`.
No SDK or SD driver module changes, clock migration, new logging stream or
additional framebuffer. Rebuild/flash firmware only for this follow-up.

- **NES conversion:** contiguous v2 RGB565 rows write two palette-converted
  pixels with one aligned 32-bit store. An unaligned first pixel and odd tail
  use halfwords; rotated/strided paths retain the exact scalar mapping. The
  usual 256x240 image uses 30,720 word stores instead of 61,440 halfword stores.
  Doom's earlier shortcut was direct indexed-to-RGB565 conversion with a
  cached palette, eliminating another color format; NES already had that LUT.
  No 256 KB pair lookup table is introduced.
- **Shared capture:** two 320-row stale-bit maps (80 bytes total) track which
  presentation-buffer rows differ from the latest frame. Unchanged rows skip
  stores only if that destination is already current. Stale rows still copy
  their identical prefix before the changed suffix; a fresh row can omit it.
  Aligned dirty-row suffixes use four-word batches, with clipped/odd fallback.
  Force/geometry/routing changes invalidate both maps. Exact pixel comparison,
  overlay composition, accumulated external/internal damage, latest-wins and
  immutable in-flight external storage remain intact. Fully moving rows still
  require a copy; this is not zero-copy and does not eliminate all scanning.
- **60 Hz scheduling:** v2 compositor and NES timer alternate 16/17-tick
  intervals at 1 kHz, totaling 1000 ticks per 60 intervals. NES timer deadlines
  compensate daemon jitter. Menu resume resets phase/debt. A guarded,
  nonblocking reschedule cannot enqueue after Stop, and a shutdown barrier
  prevents callback access after app destruction. No floating point, per-frame
  allocation or per-frame division. Other emulator clocks are not changed.
  Lowering 62.5 to 60 removes 4% of the unintended emulation ticks; it does not
  promise higher display FPS and correctly reduces nominal polling to 60 Hz.

Checks: expanded production NES differential rendering (all four rotations,
eight sizes including odd alignment/one-pixel), 30,720 row differential cases
plus a read-only fresh destination proving zero stores, real manager ownership/
mode/overlay/DMA suite, production timer 50/60 Hz / wrap / daemon-command jitter /
resume / allocation failures / stop barrier, compositor overrun/wrap pacing,
menu/history, measurement and Nofrendo IRQ suites. Xtensa C++11 syntax checks
and `-Os` kernel disassembly confirm `s32i` pixel-pair writes and `l32i/s32i`
copy batches. Source checks are not firmware-link or hardware verification.
Repository-wide formatting/static checks retain unrelated baseline failures.

Compare the same demo's moving section in Internal, External and Both at the
same effective SPI clock. Primary targets are **blit** (previously ~10 ms) and
**capture** (~10.8 ms), then core draw/skip, input-gap distribution and actual
LCD update rate. Expect ~60 polls/s over a long steady window, not 62.5. Do not
sum concurrent audio/capture/transfer wall times or count unchanged-scene LCD
skips as game slowdowns. Keep heap/stack and audio/menu/SD soak checks; no
numerical FPS gain has been measured or promised for this source update.


### Audio follow-up (regression reported on 01195cd)

The same 60 Hz task delay is **not** suitable for the audio producer. Its batch
uses integer `22050 / 60 = 367` samples, so 60 batches/s only supply 22,020
samples/s to a 22,050 Hz consumer. The earlier 16 ms delay accidentally masked
this with oversupply/backpressure. Fixed-frame pacing slowly drains the bounded
DMA queue, and RTOS jitter can worsen the audible interruption. The supplied
log shows audio calls dropping from ~16 ms (mostly I2S wait) to ~2.7 ms.

Active audio now follows blocking I2S consumption, with a one-tick yield outside
the sound mutex. Paused or failed feed sleeps 10 ms rather than spinning. Partial
writes resubmit the existing PCM tail within a 50 ms batch deadline (plus tick
rounding), without regenerating APU samples or
applying gain twice; errors, zero progress and invalid byte counts stop the
batch. NES emulation/compositor stay at 60 Hz, and pixel/capture optimizations
are unchanged. No added allocation, driver version change or logging stream.

Production producer/task host test simulates >99 seconds of continuous 22,050
Hz consumption, queue occupancy and partial-write sample order; a fixed-60-Hz
negative control drains the queue. Stereo/gain, error/pause yield, menu/history,
timer, source syntax and focused formatting/static checks are separate from
actual device sound verification, which remains pending.

Measured comparable moving-demo windows before versus 01195cd: blit10.005 ->
8.161 ms (~18% reduction), capture10.788 ->10.418 ms (~3% reduction), submitted
~21.8 ->23 FPS (~5% higher). Core draw20.86 ms and native13.88 ms remain large;
this is not a frame-matched A/B result or an audio-fixed build measurement.

## NES CPU/PPU profiling and scanline staging

The normal v2 profile now samples one in every 16 **drawn** and one in every
16 **skipped** NES frames independently. No new build variant is needed.
Five-second `measure-phase` summaries add `nes-cpu-draw`, `nes-ppu-draw`,
`nes-cpu-skip`, and `nes-ppu-skip`; each tuple is
`sampled frames,total microseconds,average microseconds,max microseconds`.
CPU covers `nes6502_execute` calls, including the vblank delay. PPU covers
`ppu_scanline`, including background/sprites, fake sprite-zero work on skipped
frames and the new staging copies. These are **subsets of core-draw/core-skip**:
never add them to those enclosing totals or call their wall times CPU usage.
Mapper hblank, FIQ accounting and end-of-line bookkeeping remain outside the
sampled CPU/PPU sections. Different sample populations and preemption mean that
subtracting averages is only a rough guide, not an exact residual profile.

A full sampled frame adds 1,050 clock calls; unsampled frames add no clock
calls inside the core. Statistics are committed only twice per sampled frame,
not once per scanline. Reporting adds at most two records to the existing
bounded logger burst. Both categories sample separately to avoid systematically
missing skipped frames. Sampling is diagnostic, not a cycle-exact profiler.

The PPU renders supported PSRAM software-bitmap scanlines into a 272-byte aligned
emulator-task stack buffer (8 left guard + 256 pixels + 8 right guard), then
copies the result in bulk. The existing 8-byte guards, fine scrolling, sprite
priority/hits and mapper latch order are retained. Only 16 guard bytes are read
from the prior framebuffer; the background initializes every visible pixel.
Internal-RAM framebuffers, other bitmap layouts and skipped frames retain the
original rendering path. A one-time `measure-nes-memory` report identifies
framebuffer, CPU-RAM and CHR placement without printing raw addresses.
No extra framebuffer, lookup table, allocation, CPU opcode or IRQ changes.
The single-site background tile kernel is forced inline under the GCC v2 build
instead of making up to 7,920 tiny calls per drawn frame at `-Os`.

Source checks: real PPU differential parity over 8,192 seeded cases against the
pinned original, including full bitmap/guard hashes, scrolling, sprite edges,
flip/priority/masks, mapper CHR mutation and skipped lines. Actual frame-loop
profiling is tested with a deterministic clock boundary. Xtensa `-Os` object
inspection confirms the tile kernel is no longer outlined. Component PPU text
is 4,884 -> 5,345 bytes (+461), BSS remains 7,584 bytes. The new renderer frame
uses 384 stack bytes and its PPU caller 32; previously the inlined scanline
renderer used 96 and its tile helper 32 (compiler reports, not a complete call-
chain bound or device watermark).
These are component checks, not a firmware link/flash or device speed result.

For acceptance, rebuild/reflash **Keira only** on `feature/sd-device-drivers`;
SDK and ILI9341 module 1.1.x stay unchanged. Confirm sound remains correct, then
capture 30–60 seconds of the same moving built-in NES demo in Both mode at
actual `spi-hz=40000000`. Compare submitted/displayed FPS, core CPU/PPU rows,
blit/capture times, input-gap tails and `emu-stack-free`. Repeat in External
mode if possible. Do not infer gameplay FPS from static scenes where unchanged
LCD frames are intentionally suppressed. No FPS uplift is claimed until this
same-scene device comparison is available.
