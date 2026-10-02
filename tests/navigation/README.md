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
emulator behavior are not covered. History reads are injected fixtures (existing
NVS parsing/validation is unchanged). The fixture tests check independent NES,
GB and GBC recency, repeated plays, nested return and owned titles, translated
menu labels, failed launches and removed histories, exact-path parent selection,
B and explicit Back, root boundaries, sorted/hidden entries, missing children,
ordinary entry/refresh behavior, selection mode and off-screen cursor visibility.
Both ordinary and ASan/UBSan host executions are required.

The unchanged SDK PageUp expression may produce a compiler sequence-point
warning; tests do not send PageUp and this task does not modify the SDK.
