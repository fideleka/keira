# Emulator/input measurement session

Use the normal **Keira feature/sd-device-drivers** and matching **SDK
feature/native-lcd-dma** branches. Measurement is now included in the ordinary
v2 environments, including `v2-modified-backlight-adc-charge-status`; there are
no separate profiling variants. Your compile-all script can stay unchanged.
Keep SD driver1.1.0 and40MHz.

## Run

Build/flash normally, then record the normal serial monitor. First capture NES
in Both mode: warm up10 seconds, then use the same moving scene for60 seconds.
Press/release directions and A/B at approximately one-second intervals for part
of the run. Do not open menus or change modes during a case. Exit normally,
keeping capture active until the final summary finishes. Send the log.

Repeat for Internal and External modes, then GB/GBC if needed. Use separate
files with emulator/mode labels, same audio/refresh/Wi-Fi settings and scene.
No ROM needs to be shared. Analyze locally with:

    python tools/measurement/analyze.py path/to/log.txt

Reports are fixed-size summaries every5 seconds, never per-frame/per-pixel.
Existing FPS/device logs use the same reduced5-second cadence. Counters are
buffered in memory; no new unbounded queues or framebuffer copies are introduced.
GB/GBC is timed once per core frame, **not per scanline**, to keep normal-build
cost down. Logging is queued/nonblocking, but timing calls and counter updates
still cost some CPU time; insignificant hardware overhead is not yet proved.

## Measurements

- `measure-controller`: actual GPIO-scanner gap/work avg/max, including shortcut
  handling. Count is hardware scans, not emulator polls. Existing nominal5ms
  scanner scheduling and10ms debounce policy are unchanged.
- `measure`: emulator input polls, forwarded button-state changes, publication
  count and input gap average/max; counts of gaps >33/50/100ms catch bursty
  catch-up. Changes count event transitions (NES) or changed masks (GB/GBC), not
  a physical keypress counter; turbo/modifiers can change it.
- `measure-span`: first-to-last sample spans; rates use `(count-1)/span`.
- `measure-phase`: compact tuples `count,total-us,average-us,max-us`.
  `input` includes controller read/filter/chord/event application on ordinary
  input returns. `core-draw`/`core-skip` count emulated frames independently of
  displayed frames. NES core time excludes video flush/palette blit; GB/GBC
  core-draw includes canvas preparation and draw callbacks. `blit` is NES
  conversion before queueDraw. GB/GBC conversion is deliberately not timed per
  line; its cost is included in core-draw.
- `publish-wait` is canvas mutex wait; `publish-work` is toolbar/swap work after
  acquiring it, excluding later taskYIELD. `audio` includes audio-generation /
  I2S waits; NES reports on its separate audio task, GB on the emulator task.
- `capture` is shared compositor capture-call duration (including immediate
  failed attempts); `internal` counts internal presentation updates. These
  phases are absent where a mode does not use that path.
- `measure-spi` gives external per-chunk packing and blocking-completion costs;
  command chunks also count in waits. Packing can overlap wire time, so do not
  add it to transfer time. No per-chunk serial output is emitted.
- `measure-log`: formatter/enqueue time, global dropped-record count, drops
  during this report and emulator task stack watermark. It does not measure
  asynchronous queue-draining CPU time. Drops/incomplete records invalidate
  missing-data assumptions. Individual records fit the SDK256-byte formatter;
  compact paired phases avoid flooding its16-record queue in the normal report
  burst, though unrelated concurrent logs can still cause drops.

All durations are **wall-clock**, including task preemption, not CPU-cycle
accounting. Several phases are nested or concurrent and must not be summed.
These data measure input polling and software application timing, **not** actual
physical-button-to-visible-photon latency; that needs an externally observed
button/LED + display trace or high-speed camera with a controlled stimulus.

## Logging audit

The current SDK logger formats records and allocates String queue entries but
never waits for its queue mutex/output; a separate task drains available bytes
once per5ms tick. Queue capacity16 causes dropping rather than unbounded growth.
USB CDC is enabled in this environment: the monitor's9600 is not evidence of a
9600-baud physical UART bottleneck. Direct/per-library diagnostics still need
separate scrutiny if they become frequent. Use measured enqueue times,
dropped-record counts and input gaps before attributing stalls to logs. There is only one normal logging configuration.

Actual device measurements and full firmware linking remain pending. Available
nodes were offline; COM3 could not be sampled remotely. Do not report predicted
FPS or nominal input cadence as measured results.
