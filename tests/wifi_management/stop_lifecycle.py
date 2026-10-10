"""Execute actual task-quiescence + Wi-Fi onStop bodies, no firmware build."""

from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
thread = (root / "src/keira/thread.cpp").read_text()
at = thread.index("void KeiraThread::quiesceForCleanup()")
quiesce = thread[at : thread.index("\n}\n", at) + 3]
source = (root / "src/apps/wificonfig/wificonfig.cpp").read_text()
at = source.index("void WiFiConfigApp::onStop()")
stop = source[at : source.index("void WiFiConfigApp::run()", at)]
fixture = r"""
#include <cassert>
#include <map>
#include <string>
#include <vector>
using TaskHandle_t = void*;
std::vector<int> events;
TaskHandle_t task = reinterpret_cast<void*>(1);
void vTaskSuspend(TaskHandle_t t) { assert(t == task); events.push_back(1); }
class KeiraThread { public: TaskHandle_t getktTaskHandle() { return task; } protected: void quiesceForCleanup(); };
class WiFiConfigApp : public KeiraThread { public: void onStop(); };
struct NetworkService { void pauseAutomaticConnection(bool paused) { assert(!paused); events.push_back(3); } };
struct System { std::map<std::string, NetworkService*> services; } ksystem;
namespace lilka { namespace detail { struct BoundedWiFiScan { static void release() { events.push_back(2); } }; } }
"""
fixture += (
    quiesce
    + stop
    + r"""
int main() {
    NetworkService network;
    ksystem.services["network"] = &network;
    WiFiConfigApp app;
    app.onStop();
    assert(events == std::vector<int>({1, 2, 3})); // Reader frozen before buffer free / service recovery.
    events.clear(); task = nullptr;
    app.onStop();
    assert(events == std::vector<int>({2, 3}));
    events.clear(); task = reinterpret_cast<void*>(1); ksystem.services["network"] = nullptr;
    app.onStop();
    assert(events == std::vector<int>({1, 2}));
}
"""
)
with tempfile.TemporaryDirectory(prefix="wifi-stop-") as directory:
    out = Path(directory)
    (out / "test.cpp").write_text(fixture)
    for sanitized in (False, True):
        flags = ["-fsanitize=address,undefined", "-fno-pie", "-no-pie"] if sanitized else []
        subprocess.run(
            [
                "g++",
                "-std=c++11",
                "-Wall",
                "-Wextra",
                "-Werror",
                *flags,
                str(out / "test.cpp"),
                "-o",
                str(out / "test"),
            ],
            check=True,
        )
        subprocess.run([str(out / "test")], check=True)
print("Actual Wi-Fi stop hook: quiesce reader before scan free and automatic-recovery resume PASS")
