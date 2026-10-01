# Verified nofrendo IRQ-source correction (v2)

## Dependency and integration

Keira v2 pins lilka-dev/arduino-nofrendo to
a5a5c1a1fdc603434e09079c817ec99209b1824e (library.properties version 1.2).
No independent local dependency checkout was present. The installed .pio/libdeps
copy is generated and **must not be edited**. v1 is unchanged.

nofrendo_build.py is a post PlatformIO extra script, alongside the existing
targets.py and pre:personal_build.py; neither existing script is replaced.
It constructs a separate $BUILD_DIR/nofrendo-irq-src overlay and redirects the
library's translation units through AddBuildMiddleware. Both the project and
library use the overlay headers, including the extended CPU context layout.
The installed library sources stay byte-for-byte unchanged. No patched upstream
checkout, dependency download, SDK edit, or target build is needed for host tests.

source-manifest.json records SHA-256 for **every** original and corrected source
file, not just the five patched files. apply.py validates the complete file set,
all hashes, every patch hunk at its exact offset/context, and all resulting hashes
before any output is written. Output is staged then renamed. Repeated application
and already-corrected input are idempotent. Mixed/modified source, missing/extra
files, changed context, corrupt patch, and stale output fail closed. There is no
fuzzy patching or permissive version fallback. Future dependency or patch updates
require a reviewed manifest update; an obsolete overlay requires a clean build
output directory, not mutation of the installed dependency.

Host contract tests verify translation-unit redirection and header precedence
using a fake build environment; **actual PlatformIO/SCons/ESP acceptance remains
pending**. A clean v2 build should print the verified-overlay message, compile
nofrendo translation units from the overlay, and retain the original libdeps hashes.
The build script intentionally stops on absent or unverified dependency sources.

The dependency/derived patch is LGPL-2.0; its original COPYING.LGPL is included.

## Correction and deliberately limited scope

The CPU retains int_pending for existing legacy mapper pulse callers. Separately,
irq_sources tracks the wired-OR **frame**, **DMC**, and **MMC3 mapper** level lines.
nes6502_setirq(source, asserted) changes only the chosen bit; it never clears a
foreign source/pulse and never rewrites cached CPU execution registers. Interrupt
entry does not acknowledge a level source. The existing CLI/RTI/timeslice checks
include asserted levels, with instruction-boundary polling also covering PLP and
in-handler writes. Masked assertions persist until individually acknowledged;
unacknowledged assertions can retrigger after RTI.

- Frame sequencer assertion sets only FRAME. $4015 reads return the prior frame
  flag and clear only FRAME. $4017 bit 6 immediately acknowledges FRAME and
  inhibits future frame IRQs. Bit 7 alone does not acknowledge an existing flag.
- The APU's DMC callback asserts only DMC. Existing DMC flag-clear paths (including
  $4010 IRQ disable and $4015 writes) clear only DMC. $4015 reads preserve DMC.
- MMC3's existing hblank assertion asserts only MAPPER. $E000 disables and
  acknowledges only MAPPER, even during a CPU instruction/IRQ handler. Initialization
  clears that line. No banking or counter algorithm is changed.
- Reset clears CPU source state and frame flag/state/period.
- Other mapper algorithms and their legacy pulse delivery are deliberately
  untouched. This is **not** a general mapper accuracy/timing rewrite, cycle-exact
  IRQ-delay fix, or save-state-format migration. Exact IRQ assertion snapshot
  behavior in the legacy SNSS mapper state format is outside this correction.

The old Wacky startup failure was a frame IRQ latched while masked, followed by
$4017 = $40 which failed to acknowledge it. Later CLI entered an uninitialized
indirect handler, eventually JAM at PC $0020 (frame 7). Clearing generic
int_pending would hide the symptom but could discard mapper/DMC interrupts;
this correction uses source-owned acknowledgment instead.

## Host regressions (no packages/network/target/device)

Requires existing Python 3 and GCC. Source-only default:

    python3 tests/nofrendo/run.py
    python3 tests/nofrendo/run.py --sanitize

Optional local ROM smoke/parity test (paths are user-supplied; no commercial ROM
or framebuffer capture is included in these tests):

    python3 tests/nofrendo/run.py --sanitize --frames 1860 \
      --wacky-rom '/path/to/your/Wacky Races.nes' \
      --control-rom data/chase.nes --control-rom data/thewit.nes

Use --source /path/to/verified/src for a separate source checkout and
--output /tmp/new-directory to retain deterministic host artifacts explicitly.
Otherwise the runner reports its new temporary artifact directory. All host
compilation/output lives outside .pio; it does not write images or ROM bytes.
The harness does not destroy the loaded cartridge, avoiding battery-save writes
alongside a user's ROM. ASan/UBSan stop on the first error; full-ROM runs disable
leak checking for the legacy process-owned core lifetime. The source-only fixture
frees its allocations and also passes with leak detection enabled.

The source-only C test compiles the real CPU/APU/PPU/mapper core, exercises the real
frame checker and register handlers, and generates a one-byte DMC sample from
synthetic memory. It covers inhibit/status acknowledgment, masking/CLI/PLP/RTI,
held-level retriggering, frame/DMC/MMC3 overlap, preservation of legacy pulses,
actual MMC3 hblank assertion, $E000 acknowledgment (including inside an executing
IRQ handler), DMC register acknowledgments, and reset. Five Python tests cover
immutability/idempotence, full-source variations, partial patches/stale output,
corrupt patch rejection, and the build middleware/header contract.

The full smoke harness runs actual nes_emulate/CPU/APU/PPU/all mappers, samples
PC/cycles/PPU/frame hashes, and processes 735 audio samples per frame. Scripted
Start: frames 300–309, 600–609, 900–909; Right+A: 1200–1499. Control logs must match
baseline exactly. Wacky must reproduce baseline frame-7 JAM, then run corrected
through the requested >=1800 frames without JAM and with changing nonuniform
output. These are startup/controller/core regressions, **not proof of full playable
correctness**. The later Wacky scene remains static despite a running CPU.

## Acceptance evidence (2026-10-01)

- Normal full real-core tests: source-only suite plus exact chase/thewit parity
  and successful Wacky startup/scripted Start through 1800 frames.
- ASan + UBSan: same complete 1800-frame suite passed, no sanitizer diagnostics.
- Final strengthened ASan + UBSan suite also passed through 1860 frames, including
  exact chase/thewit parity, actual MMC3 assertion, overlapping acknowledgments,
  and in-handler E000. Frame 1860: PC ddab, cycles 55364647, JAM 0, seven colors,
  hash 216a64e3. Source-unit allocation leak detection also passed.
- Wacky frame 1800: PC ddae, cycles 53577806, JAM 0, PPU a8/1a,
  seven colors, framebuffer hash 216a64e3. Baseline JAMs at frame 7/PC 0020.
- Installed dependency source verification remains unchanged.
- Changed C harness files pass the available clang-format check. Repository-wide
  make clang-format is blocked by pre-existing formatting violations in unrelated
  source files; make cppcheck is blocked by unavailable cppcheck. No package
  installation or unrelated reformatting was performed.
- No target compile/package/flash or physical device actions performed. Remaining
  acceptance: clean v2 target build/hook/header verification, resource-size check,
  on-device startup/audio/controller smoke, and longer/reference MMC3 gameplay.
