# Navigation regressions

Run python3 tests/navigation/run.py (g++ required). No firmware build,
PlatformIO invocation, downloads, dependency installs, or device writes.
Temporary filesystem fixtures are cleaned up by Python's TemporaryDirectory.

The runner extracts and compiles the current production launcher item
structure, menu loop and recent-folder/list refresh methods, file-manager
parent/cursor helpers, event handler, open handler and directory loader, and
ThreadManager::spawn. It also executes the sibling SDK's actual setCursor and
update implementations, using a mock controller/canvas/task boundary. Override
its source path with LILKA_SDK_MENU_SOURCE if necessary. The SDK is read-only.

The simulated RTOS suspension delays the recency write until the child runs;
callbacks resume only afterward. Real FreeRTOS scheduling, device rendering and
emulator behavior are not covered. The original history fixture injects reads. A second fixture now executes actual
readRecentRoms, parseList and rememberRecentRom with bounded mock Preferences
and fopen routing, including first-use absent namespaces and the nested launcher
refresh path. NVS parsing/validation production code is unchanged. The fixture tests check independent NES,
GB and GBC recency, repeated plays, nested return and owned titles, translated
menu labels, failed launches and removed histories, exact-path parent selection,
B and explicit Back, root boundaries, sorted/hidden entries, missing children,
ordinary entry/refresh behavior, selection mode and off-screen cursor visibility.
Both ordinary and ASan/UBSan host executions are required.

The unchanged SDK PageUp expression may produce a compiler sequence-point
warning; tests do not send PageUp and this task does not modify the SDK.

The runner also compiles and executes the exact production item declaration and
all factories with -std=c++11 -pedantic-errors. A disposable negative control
reintroduces the default-member aggregate defect and must fail compilation.
The full behavior fixture remains C++17 because its test scaffolding uses
std::filesystem and generic lambdas; firmware standard flags are unchanged.
Run python3 tests/navigation/cxx11.py to run just the target-standard regression.


## Offline target stack-budget gate

Run python3 tests/navigation/target_stack.py --output /tmp/keira-stack-results
using an existing v2 compile_commands.json and cached Xtensa compiler/dependencies.
This compiles real launcher, fmanager, recentroms, Preferences, UART logging and
SDK menu translation units at the cached v2 -Os/gnu++11 flags (GNU C99 for the C
UART source), with -fstack-usage. No PlatformIO, target linking, downloads, shared
build cache, .pio writes, generated-header writes, or SDK edits. Output contains
objects, assembly, stack reports, compiler flags and source/object SHA256 identities.
It uses the selected no-rtti libc archive to measure formatter entry frames.

Use --project <worktree> --dependencies <cached-project> --compile-commands <json>
--sdk <paired-sdk> --report-only to measure an older source revision identically.
Omit --report-only to require the budget gate: retained run <=1024 bytes, the
conservative three-menu/NVS prefix <=2048 bytes, construction prefix <=6144 bytes.
The gate reserves 4096 bytes for external call chains plus at least 2048 bytes
headroom in the unchanged 8192-byte task. This is a review allowance, NOT a
proven complete callgraph bound. Allocator, NVS internals, libc callees, FreeRTOS
window spills/exception overhead and device behavior still need runtime validation.

The old run initializer retained a frame of 5024 bytes during menu/NVS calls.
It is now split into explicitly noninlined construction helpers; the temporary
Applications vector is destroyed before homeScreen. Root and Applications menu
initializer content was compared byte-for-byte with 9d8c45b; no entries/actions,
callback paths, colors, or integrations were removed. Owning titles and C++11
factory compatibility are preserved, and the task stack is not increased.


The cached Preferences String getter was also found to use a dynamic stack VLA
(char buf[len]), up to 4000 bytes for a valid maximum history, and unbounded for
malformed/oversized persisted values. readSavedList now uses a 4000-byte heap
buffer and Preferences' caller-buffer overload. The assembly gate rejects calls
to the VLA overload; oversized values return an empty list without erasing NVS.
Fixtures exercise both reads and writes through this real bounded helper.


Measured with Xtensa GCC 8.4.0, -Os/gnu++11, SDK a6c6f942:

- ab5ed10: run 4224, showMenu 240, refreshFolders 224 bytes.
- 9d8c45b: run 5024, showMenu 256, refreshFolders 160, refreshItems 208 bytes.
- This fix: run 128; buildApplicationsMenu 2288 and buildMainMenu 4688 are transient
  construction frames, not retained while homeScreen/menus/NVS run. showMenu stays
  256, homeScreen 400, refresh lambda 112, folder/item refresh 160/208 bytes.
- History read 80, bounded heap-read helper 32, caller-buffer Preferences getter 80,
  parseList 64 bytes. The unused String getter is 64 bytes plus its dynamic VLA.
- Conservative fixed retained prefix 1968 bytes; known logger/formatter subtotal
  1184 (log_printf 80, log_printfv 64, vsnprintf 64, _vsnprintf_r 144,
  _svfprintf_r 784, __ssprint_r 48), giving a known subtotal of 3152 bytes.
  This excludes other callee frames/spills; do not present it as a runtime bound.

A combined firmware link is NOT verified: the cached toolchain/framework/library
sources are present, but the PlatformIO core/executable is missing (python3 -m
platformio reports No module named platformio). No downloads/installs are attempted.
No ELF/bin for this fix has been produced, and existing stale firmware artifacts
must not be used to decode the reported device addresses.
