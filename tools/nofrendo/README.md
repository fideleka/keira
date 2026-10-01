# Verified nofrendo IRQ-source correction (v2)

## Dependency and integration

Keira v2 pins lilka-dev/arduino-nofrendo to
a5a5c1a1fdc603434e09079c817ec99209b1824e (library.properties version 1.2).
No independent local dependency checkout was present. The installed .pio/libdeps
copy is generated and **must not be edited**. v1 is unchanged.

nofrendo_build.py is a PRE PlatformIO extra script, alongside the existing
targets.py and pre:personal_build.py; neither existing script is replaced.
It constructs a separate $BUILD_DIR/nofrendo-irq-src-<hash> overlay and redirects the
library's translation units through AddBuildMiddleware. Both the project and
library use the overlay headers, including the extended CPU context layout.
The installed library sources stay byte-for-byte unchanged. No patched upstream
checkout, dependency download, SDK edit, or target build is needed for host tests.

source-manifest.json records SHA-256 for **every** original and corrected source
file, not just the five patched files. apply.py validates the complete file set,
all CRLF-to-LF canonical hashes, every patch hunk at its exact offset/context, and all resulting hashes
before any output is written. Output is staged then renamed. Repeated application
and already-corrected input are idempotent. Mixed/modified source, missing/extra
files, changed context, corrupt patch, and stale output fail closed. There is no
fuzzy patching or permissive version fallback. Future dependency or patch updates
require a reviewed manifest update; the manifest plus exact patch bytes deterministically version the overlay directory.
Obsolete overlays and user directories are retained untouched; a correction uses
a new directory without requiring deletion or mutation of the dependency.

Host contract tests verify translation-unit redirection and header precedence.
Additional lifecycle fixtures use real SCons 4.11.1 environments, clones, Files
and Object builders, but stub the PlatformIO registration/dump boundary;
**native PlatformIO IDE/target acceptance remains pending**. A clean v2 build should print the verified-overlay message, compile
nofrendo translation units from the overlay, and retain the original libdeps hashes.
Real builds intentionally stop on absent or unverified dependency sources.
IDE integration dumps return before dependency access or overlay writes.

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

## IDE-loading repair evidence (2026-10-01)

- The old hook reproducibly raises a source-file-set ValueError in a metadata
  fixture with no downloaded dependency, even when IsIntegrationDump is true.
  The PRE hook now returns immediately for that mode, with no overlay writes.
- Real builds still verify all 98 pinned source files before registering any
  middleware. The Git pin tree has no source files missing from the manifest;
  there are no optional source exceptions. Dependency-root metadata is outside
  the src manifest; files added inside src still reject.
- Real SCons showed PrependUnique does not move an inherited overlay path ahead
  of a subsequently prepended dependency path. The hook now explicitly places
  overlay headers first in each project/library build environment and returns
  Object builder results for every dependency translation unit.
- Three upstream files already contain CRLF (gui_elem.h, sdl/sdl.c, unix/osd.c).
  Canonical manifest hashes were derived from the previously fully verified
  original/patched trees. Only CRLF-to-LF conversion is allowed; semantic changes,
  partial patches and unlisted/missing files still reject before writes.
- Five overlay tests, four real-SCons lifecycle tests and the source-only IRQ
  suite pass; the IRQ suite also passes ASan/UBSan. No ROM runs were requested
  for this repair. No firmware build, download, package or flash was performed.
- Existing SCons was found, but no PlatformIO executable/module/venv was found.
  python3 -m platformio project config --json-output fails with No module named
  platformio. Native idedata cannot be proved locally without unavailable tools.
- Required make clang-format and make cppcheck are blocked by missing executables.
  No C source was changed. git diff --check passes.

Run lifecycle fixtures with existing PlatformIO-bundled SCons (or SCons already
on PYTHONPATH), without executing any generated Object action:

    python3 tests/nofrendo/test_lifecycle.py /path/to/verified/nofrendo/src

The remote VS Code screenshot alone does not establish its precise exception.
If loading still fails after this repair, provide the one full error traceback
from VS Code Output > PlatformIO (the project initialization failure).


## Bounded startup diagnosis (2026-10-01)

This instrumentation is not another claim that the pink-screen device failure is
fixed. It makes the linked CPU and startup boundary observable through standard
ESP_LOGW, tag NES-startup, at the normal default warning-or-higher logging level.
LOG_LOCAL_LEVEL is warning for this translation unit; no dependency on disabled
nofrendo_log_printf and no globally changed log level. A globally silenced serial
transport or runtime log filter still cannot be repaired by these diagnostics.

The app calls nes6502_irq_fix_identity_v1(), exported only by the corrected CPU
translation unit. There is no macro fallback or weak implementation: linking
an old CPU fails with an undefined identity symbol. The first launch line is:

    W (...) NES-startup: launch core=keira-irq-sources-v1/a5a5c1a1/frame-dmc-mmc3
    W (...) NES-startup: ROM format=iNES mapper=4 prg16k_field=8 chr8k_field=16 bytes=262160 stat=0
    W (...) NES-startup: nofrendo_main enter
    W (...) NES-startup: stage=create result=0
    W (...) NES-startup: stage=load result=0
    W (...) NES-startup: stage=video result=0
    W (...) NES-startup: stage=timer result=0
    W (...) NES-startup: stage=emulate-enter result=0
    W (...) NES-startup: render=10 pc=.... cycles=... JAM=0 irq_sources=00 pulse=0
    W (...) NES-startup: render=60 pc=.... cycles=... JAM=0 irq_sources=00 pulse=0

Mapper, sizes, PC, cycles and IRQ state above are illustrative; actual values may
differ. The size fields are the raw header bank-count fields (NES2 extended size
encoding is not interpreted); bytes is stat's complete file size. Only 16 header
bytes are read, no path, ROM contents or framebuffer is logged. Unreadable/invalid
headers report header_bytes and stat result without replacing core validation.
Create/load/video/timer statuses come from actual core calls, not assumed success.
Video's historically ignored return remains ignored; no resource/control/error
semantics changed. On normal exit expect stage=emulate-return result=0 and
nofrendo_main return=0; launch failures report their existing result code.

The two render summaries are at customBlit's synchronous execute-return boundary,
not timer/audio tasks. They are rendered-frame checkpoints (frameskip/pause can
make them differ from emulated frame counts). The app-owned counter saturates at
60 and resets per launch. The small fixed-size read-only CPU snapshot reports PC/cycles,
JAM, independent IRQ source bits (FRAME=01, DMC=02, MMC3=04) and legacy pulse latch.
It neither copies large CPU contexts nor calls nes_getcontext, which also touches
APU state used by the audio task. No cached execution registers are mutated, and
there is no per-frame logging after checkpoint 60, including if JAM persists.

Acceptance remains host-only: six overlay fixtures (including version rollover
while preserving obsolete/user directories), four real-SCons environment/node/
clone fixtures, and two diagnostic fixtures. The latter compile the actual CPU:
corrected identity links/runs; original CPU fails to link that identity. A real
patched nofrendo.c compile checks startup callback call sites. The shared render
checkpoint class passes one million calls and relaunch reset under ASan/UBSan.
The real-core IRQ unit also checks identity and synthetic JAM PC/cycles/source
snapshot. Sanitized Wacky + chase/thewit smoke runs cover 1860 frames with exact
control parity, baseline Wacky frame-7 JAM reproduction, corrected no-JAM output.
No native PlatformIO target/IDE build, resource-size or physical serial/device
result has been verified. No target/package/flash/install/download/SDK/.pio edits.
Required make clang-format and make cppcheck were attempted and blocked by missing
executables; no packages installed. git diff --check passes.

    python3 tests/nofrendo/test_diagnostics.py /path/to/verified/nofrendo/src

Final diagnostic artifacts: /tmp/nofrendo-regression-abglb4m_. Final overlay cache
key: 1104062e8144a4487fd2a6b9e83bec023e893b87a0d176b3c97b6eed21ee1c3f.
Frame 1860 corrected Wacky: PC ddab, cycles 55364647, JAM 0, seven colors,
hash 216a64e3. Scoped .gitattributes permits unified-diff blank-line prefixes;
ordinary source whitespace checking remains enabled.

## Missing snapshot linker investigation (2026-10-01)

The reported Windows undefined nes6502_read_diagnostics is **not yet a verified
native PlatformIO repair**. Both identity and snapshot definitions are present in
this reviewed CPU overlay. An identity-only, obsolete CPU can produce exactly
that missing-snapshot error; the new executed SCons fixture recreates this by
removing only the snapshot function in an isolated prior-version tree, then
successfully upgrades without deleting its object/archive. That controlled
reproduction does not identify which CPU/archive the user's build actually used.
The pmf-conversions and C string-const warnings do not explain this link failure.

A concrete routing defect was reproduced with actual SCons VariantDir nodes:
node.get_abspath() names the build alias, while node.srcnode().get_abspath()
names the original library input. The previous relative-path check silently
returns an unmodified node for that alias. Native PlatformIO source-node creation
and PRE middleware propagation to dependent-library environments cannot be
confirmed here: no installed PlatformIO module/executable/source was found.
The registration boundary in every fixture remains explicitly simulated.
Windows Path.relative_to already uses Windows case-insensitive semantics;
case alone is not an established bug. PureWindowsPath checks cover mixed drive
case, slash forms, UNC case, and rejecting another drive, not a Windows execution.

The integration now resolves srcnode/realpath aliases, constructs explicit
nofrendo-objects-<overlay-key>/<relative>.o targets, and namespaces the core
archive with libnofrendo-<overlay-key>-. Object emitters shared by actual SCons
clones reject original inputs even if middleware is skipped; archive emitters
also reject already-created original objects, obsolete overlays, and unversioned
core objects/archives. Headers are put first during object emission as well as
middleware dispatch. Value dependencies include the reviewed overlay key and
integration module hash for project/core objects and archives. No fallback,
weak snapshot, dropped diagnostic, dependency edit, or cache deletion is used.
A native builder that does not preserve these contracts must fail closed rather
than silently link the wrong CPU. Namespaced archive behavior still needs native
PlatformIO acceptance; this host fixture must not be represented as that API.

Executed host acceptance:

    python3 tests/nofrendo/test_link.py /path/to/verified/nofrendo/src

This invokes existing SCons 4.11.1 with GCC/ar and actual CPU/APU/PPU/mappers,
then links project host/synthetic IRQ tests (including the actual nes.c) against
the archive. It checks both required symbols with nm and executes the real
snapshot/IRQ tests. It checks unchanged repeat builds, a same-build-directory
manifest-key rollover, integration-policy signature rebuild, retaining prior
objects, identity-only obsolete CPU link
failure/upgrade, VariantDir redirection, middleware bypass rejection, and archive
bypass rejection. Overlay, lifecycle, diagnostics, and real-core ASan/UBSan IRQ
regressions pass separately. Required make clang-format/cppcheck remain blocked
by missing executables. No target build/package/flash, install/network, SDK or
.pio writes were performed.

To establish the remote cause and accept integration, provide one complete
verbose NES v2 build log (including initialization and link, not just its last
error lines). It must show the verified-overlay line, nes6502.c compile source
and explicit target, library/archive command and members, project header include
paths, and final linker archive list. Also capture the relevant middleware/PRE
traceback if the fail-closed guard fires. Inspect the linked core archive with
the existing target nm: both identity and snapshot must be defined by its CPU
member. Do not delete the old cache before collecting that evidence: an archive
with identity but without snapshot is direct evidence of an obsolete CPU; both
symbols present in a different archive points instead to archive selection/order.
