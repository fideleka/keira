"""Host identity link gate and bounded render diagnostics; no target builders."""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(sys.argv.pop(1)).resolve()
spec = importlib.util.spec_from_file_location("diag_patch", ROOT / "tools/nofrendo/apply.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


class DiagnosticsTests(unittest.TestCase):
    def test_identity_requires_corrected_cpu_translation_unit(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            fixed = patch.prepare(SOURCE, tmp / "fixed")
            probe = tmp / "probe.c"
            probe.write_text('extern const char *nes6502_irq_fix_identity_v1(void);\n'
                             'int main(void) { return puts(nes6502_irq_fix_identity_v1()) < 0; }\n')
            for name, source in (("fixed", fixed), ("old", SOURCE)):
                command = ["gcc", "-std=gnu99", "-include", "stdio.h", "-ffunction-sections",
                           "-fdata-sections", "-Wl,--gc-sections", "-I" + str(source),
                           str(probe), str(source / "cpu/nes6502.c"), "-o", str(tmp / (name + "-probe"))]
                result = subprocess.run(command, capture_output=True, text=True)
                if name == "fixed":
                    self.assertEqual(result.returncode, 0, result.stderr)
                    output = subprocess.check_output([str(tmp / (name + "-probe"))], text=True).strip()
                    self.assertEqual(output, "keira-irq-sources-v1/a5a5c1a1/frame-dmc-mmc3")
                else:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("undefined reference", result.stderr)
                    self.assertIn("nes6502_irq_fix_identity_v1", result.stderr)
            # Startup callback declarations and patched call sites compile in the real core.
            subprocess.run(["gcc", "-std=gnu99", "-I" + str(fixed), "-c",
                            str(fixed / "nofrendo.c"), "-o", str(tmp / "nofrendo.o")], check=True)

    def test_render_checkpoints_saturate_and_reset(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            probe = tmp / "probe.cpp"
            probe.write_text('''#include <assert.h>
#include "startupdiagnostics.h"
int main() {
    NesStartupDiagnostics state;
    unsigned reports = 0;
    for (unsigned i = 1; i <= 1000000; ++i) {
        bool report = state.nextRender();
        assert(report == (i == 10 || i == 60));
        if (report) ++reports;
    }
    assert(reports == 2 && state.renderCount() == 60);
    state = NesStartupDiagnostics();
    for (unsigned i = 1; i <= 60; ++i)
        assert(state.nextRender() == (i == 10 || i == 60));
}
''')
            subprocess.run(["g++", "-std=c++11", "-fsanitize=address,undefined", "-fno-pie", "-no-pie",
                            "-I" + str(ROOT / "src/apps/nes"), str(probe), "-o", str(tmp / "probe")], check=True)
            subprocess.run([str(tmp / "probe")], check=True)


if __name__ == "__main__":
    unittest.main()
