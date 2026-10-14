# Emulator/input measurement session

Use **feature/display-measurement in both Keira and SDK**. This branch builds on
the faster pre-zero-copy pipeline and native DMA commits, not the rejected
zero-copy architecture. Keep SD driver1.1.0, saved40MHz, audio volume, refresh
selection, Wi-Fi state and the same moving demo/scene unchanged.

## Builds (owner runs locally; no flash performed by the agent)

Periodic profile:

    pio run -e v2-modified-backlight-adc-charge-status-measure -t upload
    pio device monitor -e v2-modified-backlight-adc-charge-status-measure --filter log2file

Buffered-output control:

    pio run -e v2-modified-backlight-adc-charge-status-measure-quiet -t upload
    pio device monitor -e v2-modified-backlight-adc-charge-status-measure-quiet --filter log2file

Normal environments have profiling compiled out. The periodic profile summarizes
at 5-second intervals; existing performance logs also slow from2 to5 seconds.
The quiet profile has identical phase counters, but suppresses periodic perf /
device / measurement output and prints the game summary after exiting normally.
It does not silence SDK/hardware errors or every library diagnostic. It tests
periodic reporting cost (snapshot, formatting, enqueue/drain), not the complete
instrumentation overhead or only the serial wire.

## Run matrix

For each profile, run NES, GB and GBC separately in **Internal, External, Both**.
Use a fixed repeatable scene for60 seconds per case; repeat in reverse profile
order to reduce warmup/order effects. Discard the first two periodic windows.
Do not open menus or change modes during a case. Press/release directions and
A/B at about one-second intervals for part of each run. Exit normally at the
end, keeping capture active until all summary lines finish. Use separate files
named e.g. `nes-both-periodic.txt`, `nes-both-quiet.txt` and `gbc-external-...`.
Record ROM/demo, scene, audio/Wi-Fi/refresh settings; emulator names alone do not
identify GB vs GBC cartridges. No user ROM needs to be shared.

Analyze a capture:

    python tools/measurement/analyze.py path/to/log.txt

Send the logs for comparative analysis. The periodic profile's existing perf /
perf-ext records count actual LCD updates; emulator submissions are a different
metric. Quiet summaries retain timestamp spans for input/submission rates so
menu/exit cleanup after the last sample doesn't lower those rates artificially.
An intentional mid-run menu will still appear as a long input gap: exclude that
run rather than labeling it a scheduling stall.

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
  conversion before queueDraw, or the sum of GB/GBC line-conversion callbacks
  within each drawn frame. The latter uses two timer reads per rendered line;
  it can perturb rendering and is identical in periodic/quiet profiles.
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
separate scrutiny if they become frequent. Compare steady-scene rates and worst
input gaps in periodic versus quiet profiles before attributing stalls to logs.

Actual device measurements and full firmware linking remain pending. Available
nodes were offline; COM3 could not be sampled remotely. Do not report predicted
FPS or nominal input cadence as measured results.
