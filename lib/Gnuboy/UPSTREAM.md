# Gnuboy source

Source: `https://github.com/ducalex/retro-go`,
`retro-core/components/gnuboy` at revision
`4ced120669750ca7228fd0414211430c1d923166`.
The core is GPL-2.0; see `COPYING` and `CREDITS`. Anton intends this Keira
feature for personal GB/GBC use, not an upstream Keira PR. If distributing
combined firmware, follow the GPLv2 source and notice obligations. Do not
describe the surrounding ESP-box frontend's MIT license as the core's license.

The imported core has a local `GNUBOY_QUIET` switch in `gnuboy.h` to suppress
informational/debug serial output while preserving warnings and errors. Its
state I/O checks the complete block count and reports close failures, so a
partial SD transfer cannot be reported as a successful save/load. Keira's
wrapper uses its public API and keeps
ROM ownership outside the core. A device build and performance test are still
required; the host comparison is not a Lilka benchmark.
