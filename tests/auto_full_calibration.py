#!/usr/bin/env python3
"""Check real transition helper and service polling without firmware compilation."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
service = (ROOT / "src/services/battery/battery.cpp").read_text()
start = service.index("void BatteryCalibrationService::run()")
run = service[start:]
launcher = (ROOT / "src/apps/launcher/launcher.cpp").read_text()
assert "K_S_LAUNCHER_BATTERY_SET_FULL" not in launcher
assert "K_S_LAUNCHER_BATTERY_RESET_FULL" not in launcher
assert "K_S_LAUNCHER_BATTERY_DISCHARGE_PROFILE" in launcher
ksystem = (ROOT / "src/keira/ksystem.cpp").read_text()
spawn = "#if defined(KEIRA_ADC_CHARGE_STATUS) && KEIRA_ADC_CHARGE_STATUS && LILKA_VERSION >= 2\n    services.spawn(new BatteryCalibrationService());\n#endif"
assert spawn in ksystem
assert "calibrateFullLevel" not in (ROOT / "src/apps/statusbar/statusbar.cpp").read_text()

HARNESS = r"""
#include "services/battery/auto_full_calibration.h"
#include <cassert>
#include <cstdint>
uint32_t hostNow = 0;
int locks = 0, polls = 0, attempts = 0, saved = 0, failSave = 0;
bool reconnect = false;
struct Done {};
uint32_t millis() { return hostNow; }
#define pdMS_TO_TICKS(x) (x)
#define NVS_LOCK ++locks
#define NVS_UNLOCK --locks
void vTaskDelay(int delay) {
  assert(delay == 1000 && locks == 0);
  hostNow += delay;
  if (hostNow == 70000) throw Done{};
}
namespace lilka {
struct Battery {
  float readRawVoltage() {
    assert(locks == 0); ++polls;
    if (hostNow < 3000) return 1.1f;
    if (reconnect && hostNow >= 20000 && hostNow < 21000) return 2.1f;
    return 4.08f;
  }
  bool calibrateFullLevel() {
    assert(locks == 1 && hostNow == 35000);
    ++attempts;
    if (failSave) return false;
    ++saved; return true;
  }
} battery;
struct Serial {
  void log(const char *) { assert(locks == 0); }
  void err(const char *) { assert(locks == 0); }
} serial;
}
class BatteryCalibrationService {
public:
  void run();
private:
  keira::AutoFullCalibration calibration;
};
__RUN__
int main() {
  for (int scenario = 0; scenario < 3; ++scenario) {
    hostNow = 0; polls = attempts = saved = locks = 0;
    reconnect = scenario == 1;
    failSave = scenario == 2;
    BatteryCalibrationService service;
    try { service.run(); assert(false); } catch (Done &) {}
    assert(polls == 70); // Runs regardless of hidden/off battery UI.
    assert(attempts == (scenario == 1 ? 0 : 1));
    assert(saved == (scenario == 0 ? 1 : 0));
  }
}
""".replace(
    "__RUN__", run
)
with tempfile.TemporaryDirectory(prefix="keira-auto-full-") as directory:
    tmp = Path(directory)
    (tmp / "service.cpp").write_text(HARNESS)
    for sanitized in (False, True):
        flags = ["-fsanitize=address,undefined", "-fno-pie", "-no-pie"] if sanitized else []
        for source in (ROOT / "tests/auto_full_calibration.cpp", tmp / "service.cpp"):
            output = tmp / source.stem
            subprocess.run(
                [
                    os.environ.get("CXX", "g++"),
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    *flags,
                    "-I" + str(ROOT / "src"),
                    str(source),
                    "-o",
                    str(output),
                ],
                check=True,
            )
            subprocess.run([str(output)], check=True)
print(
    "PASS: actual 30s transition helper + service loop, reconnection, one save attempt/cycle, no drawing-time NVS and profile-only menu (normal + ASan/UBSan)"
)
