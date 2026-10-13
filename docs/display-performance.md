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

Rebuild and flash Keira with the matching SDK. **Keep ILI9341 SD driver 1.1.0**;
no driver module rebuild/version change is needed for these host-side transport
changes. Do not use the zero-copy 1.2.0 module. Retain 40 MHz and existing settings.
Boot should report `Native LCD pixels: DMA` (or an explicit polling fallback).
The existing 2-second `perf` / `perf-ext` logs remain available. No new logging
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
