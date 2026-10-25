"""No-environment build hooks and production compositor instrumentation removal."""
from pathlib import Path
import os
import re
import runpy
import subprocess
from unittest.mock import patch

root = Path(__file__).resolve().parents[1]


class Environment:
    def Append(self, CPPDEFINES):
        self.values = dict(CPPDEFINES)


for selected in (None, '0', '1', 'invalid'):
    with patch.dict(os.environ, {} if selected is None else {'KEIRA_MEASURE': selected}, clear=True):
        env = Environment()
        try:
            runpy.run_path(str(root/'tools/build_diagnostics.py'),
                           init_globals={'env': env, 'Import': lambda _: None})
        except RuntimeError:
            assert selected == 'invalid'
            continue
        assert selected != 'invalid'
        enabled = int(selected == '1')
        assert env.values == {'KEIRA_MEASURE': enabled, 'LILKA_MEASURE_INPUT': enabled,
                              'LILKA_MEASURE_DISPLAY': enabled, 'LILKA_LOG_LEVEL': 1+enabled,
                              'CORE_DEBUG_LEVEL': 1}

source = (root/'src/keira/appmanager.cpp').read_text()
source = re.sub(r'^#include .*$', '', source, flags=re.M)
for enabled in (0, 1):
    text = subprocess.run(['g++', '-E', '-P', '-x', 'c++', '-DKEIRA_MEASURE='+str(enabled), '-'],
                          input=source, text=True, capture_output=True, check=True).stdout
    assert ('perf-time:' in text) == bool(enabled)
    assert ('esp_timer_get_time' in text) == bool(enabled)
    assert ('FrameMetrics' in text) == bool(enabled)
    # Quiet mode must retain the renderer, frame pacing, idle and overlay work.
    for operation in ('clearOutsideOverlay', 'presentCanvasOutsideOverlay', 'finishSystemOverlay',
                      'devices->capture', 'devices->presentInternal', 'waitForFrame()', 'serviceIdle'):
        assert operation in text

print('PASS: no-env quiet defaults, strict opt-in, no compositor profiling calls, rendering retained')
