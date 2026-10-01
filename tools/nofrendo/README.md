# Production nofrendo IRQ correction

## Why

The pinned original core latches a frame IRQ while interrupts are masked. Wacky
Races writes $4017=$40, but the old core does not acknowledge that pending frame
IRQ. Its later CLI enters an indirect IRQ handler before initialization, then
JAMs at PC $0020 (frame 7). This chain was reproduced in the actual host core.
Clearing the generic pending latch is not a proper repair: it can discard a
mapper or DMC interrupt. Independent FRAME, DMC and MMC3 level sources provide
source-owned acknowledgment instead.

A separate real-SCons reproduction showed get_abspath() can identify a VariantDir
build alias, not the dependency source. That can silently bypass overlay routing
and compile an old core. srcnode()/realpath routing and versioned objects/archive
are therefore retained as production build correctness, not runtime diagnostics.
The user confirmed Wacky works on the debug e009db9 device build. That result
does not isolate the physical cause, and is not device verification of this
clean fix/nes-irq branch. Debug history remains separate and unchanged.

## Firmware scope

Only five dependency files are patched: cpu/nes6502.c, cpu/nes6502.h, nes/nes.c,
sndhrdw/nes_apu.c and mappers/map004.c. Keira app/driver files remain identical
to features/stage. No startup callbacks, log-level/tag toggles, CPU snapshot API,
identity function, runtime probe or diagnostic fixture is shipped.

- FRAME assertion persists while masked. $4015 reads return the prior frame flag
  and acknowledge FRAME only. $4017 bit 6 acknowledges and inhibits FRAME; bit 7
  alone does not acknowledge an existing flag.
- DMC assertion/acknowledgment is independent. $4010 IRQ disable and existing
  $4015 write/reload clear paths acknowledge DMC. $4015 reads preserve DMC.
- MMC3 hblank asserts MAPPER; $E000 disables/acknowledges only that source,
  including inside an executing handler. Mapper initialization clears its line.
- CPU CLI/RTI/timeslice checks and instruction-boundary polling include asserted
  levels, including PLP unmasking. Interrupt entry does not acknowledge a level.
  nes6502_setirq changes source bits without rewriting cached execution registers.
- Reset clears source state and frame flag/state/period. Legacy mapper pulses
  and other mapper algorithms remain unchanged.

This is not a cycle-exact IRQ-delay/MMC3-counter rewrite, general mapper accuracy
fix or save-state migration; legacy SNSS IRQ-state restoration is out of scope.

## Durable build routing

platformio.ini pins v2 arduino-nofrendo 1.2 to commit
a5a5c1a1fdc603434e09079c817ec99209b1824e; v1 is unchanged. The PRE hook preserves
existing scripts and returns immediately for IDE integration dumps, without
requiring downloaded dependencies. Real builds verify all 98 original source
files, then stage a separate overlay under BUILD_DIR. Installed dependencies
and SDK sources are never edited.

The manifest preserves all original SHA-256 hashes and records all resulting
hashes. Only CRLF-to-LF normalization is tolerated. Unexpected/missing files,
symlinks, semantic modifications, mixed patches and stale outputs fail closed.
Exact patch context and every result are verified before writing; preparation is
idempotent. Manifest and patch bytes version the overlay, object targets and
archive name. Old directories are retained, never deleted.

Routing resolves srcnode()/realpath and uses explicit namespaced Object targets.
Shared SCons object/archive emitters reject original inputs, obsolete overlays
and unversioned core objects/archives even on clones missing middleware. Overlay
headers are first for project/library consumers. Value dependencies include the
overlay key and integration module hash, rebuilding consumers when policy changes.
These build-graph guards replace runtime identity probes. The semantic
nes6502_setirq symbol is checked with nm and required by real host project/core
link regressions, not by a pointless app call.

Native PlatformIO is unavailable here. The adapter registration boundary in host
tests is simulated; actual SCons nodes/builders, GCC compilation, ar archives and
final project links are real. This is not native PlatformIO acceptance.

## Tests (host only; no packages, network or target builds)

    python3 tests/nofrendo/run.py --sanitize --frames 1860 \
      --wacky-rom /path/to/your/Wacky-Races.nes \
      --control-rom data/chase.nes --control-rom data/thewit.nes
    python3 tests/nofrendo/test_lifecycle.py /path/to/verified/src
    python3 tests/nofrendo/test_link.py /path/to/verified/src

Omit ROM arguments for source-only synthetic-memory IRQ tests. Optional commercial
ROMs are user-supplied paths and are never committed/copied by the harness. All
compilation and logs live in new host scratch directories, never .pio. No
framebuffer image files are generated. Cartridge teardown is intentionally
omitted to avoid battery-save writes beside user ROMs; full runs disable leak
checking for process-owned core allocations. ASan/UBSan stop on first error.

The real-core suite exercises $4015/$4017, masking/CLI/PLP/RTI, held-source
retriggering, FRAME/DMC/MMC3 overlap, actual MMC3 hblank assertion, in-handler
$E000 acknowledgment, DMC clears, legacy pulse preservation, reset and JAM.
Overlay/lifecycle tests cover full-source rejection, idempotence, corrupt/stale
outputs, CRLF, IDE dumps, source routing and cloned header precedence.
GCC/SCons tests compile and link the real core archive plus synthetic IRQ project,
check the semantic symbol, reject an original-core link, upgrade without deleting
old objects, retain incremental repeat builds, rebuild on policy change, roll
manifest keys in the same cache and reject object/archive middleware bypasses.

The ROM harness runs actual CPU/APU/PPU/all mappers with 735 audio samples per
frame, scripted Start at 300–309/600–609/900–909 and Right+A at 1200–1499. Controls
require exact sampled PC/cycles/PPU/framebuffer parity. Wacky must reproduce the
baseline frame-7 JAM then reach 1860 corrected frames without JAM, with changing
nonuniform output. This does not prove full playable correctness; the later
scene is static despite a running CPU.

## Acceptance and remaining gates

Clean-branch acceptance (2026-10-01): six overlay tests, four lifecycle tests,
two real GCC/SCons archive/project-link tests and the real-core IRQ suite pass.
ASan/UBSan passes Wacky plus exact chase/thewit parity through 1860 frames.
Corrected Wacky frame 1860: PC ddab, cycles 55364647, JAM 0, seven colors,
frame hash 216a64e3; original core reproduces frame-7 PC 0020 JAM.
All 98 original manifest hashes and the dependency license remain unchanged.
make clang-format and make cppcheck were attempted but their executables are
unavailable; no packages were installed. Earlier debug host evidence and the
user-confirmed e009db9 device success remain separate evidence.
Remaining: clean v2 native build/IDE acceptance, inspect overlay compile targets,
header paths and final linked archive, verify unchanged dependency hashes and
resource sizes, then physical startup/audio/controller smoke and longer/reference
MMC3 gameplay on this clean branch. Do not merge stage without permission.

## License

The dependency and derived patch are GNU Library GPL version 2 (LGPL-2.0); the
original COPYING.LGPL is retained byte-for-byte. Original source copyright/license
notices are preserved. Distributing linked firmware must satisfy that license,
including corresponding modified library source and applicable relinking
requirements; keeping a patch alone is not a blanket distribution compliance
claim. No dependency relicensing is attempted.
