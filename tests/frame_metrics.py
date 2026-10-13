#!/usr/bin/env python3
"""Check production monitor arithmetic, idle windows and long-frame overflow."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = r'''
#include "keira/frame_metrics.h"
#include <cassert>
#include <cstdint>
int main() {
    FrameMetrics samples;
    assert(!samples.averageUs() && !samples.maxUs && !samples.fpsX10(2000));
    samples.record(15000); samples.record(45000);
    assert(samples.averageUs()==30000 && samples.maxUs==45000);
    assert(samples.fpsX10(2000)==10 && !samples.fpsX10(0));
    samples = FrameMetrics{};
    for (unsigned i=0;i<3;++i) samples.record(UINT32_MAX);
    assert(samples.averageUs()==UINT32_MAX && samples.totalUs==uint64_t(UINT32_MAX)*3);
    samples = FrameMetrics{};
    samples.count=150; assert(samples.fpsX10(2000)==750);
    // Millisecond subtraction remains correct across clock wrap.
    const uint32_t start=UINT32_MAX-999, end=1000;
    assert(end-start==2000 && samples.fpsX10(end-start)==750);
}
'''
with tempfile.TemporaryDirectory(prefix="keira-frame-metrics-") as directory:
    tmp = Path(directory)
    (tmp / "test.cpp").write_text(source)
    subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I" + str(root / "src"),
                    str(tmp / "test.cpp"), "-o", str(tmp / "test")], check=True)
    subprocess.run([str(tmp / "test")], check=True)
print("PASS: idle/FPS/avg/max/reset, 64-bit timing totals and millisecond wrap")
