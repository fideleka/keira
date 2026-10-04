"""Execute production launch/history code with host RTOS, core and NVS boundaries.
The real verified Nofrendo startup control flow is included, not a success flag
substitute. Hardware/audio and actual ROM parser behavior remain device checks.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]


def method(path, signature):
    source = Path(path).read_text()
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=ROOT / '.pio/libdeps/v2/arduino-nofrendo/src')
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('history_overlay', ROOT / 'tools/nofrendo/apply.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
patch.verify(args.source, 'original')

# Reuse only the existing Arduino/Menu boundary and NVS fixture, then include
# actual production persistence. Its limits/order/dedup remain unchanged.
common = (ROOT / 'tests/navigation/host.cpp').read_text().split('enum class RomSystem')[0]
nvs = (ROOT / 'tests/navigation/nvs_host.cpp').read_text().split('bool failNextLaunch')[0]
nvs = nvs.replace('int begins = 0, firstUseFailures = 0;',
                  'int begins = 0, firstUseFailures = 0, historyWrites = 0;')
nvs = nvs.replace('savedPreferences[key] = value;', '++historyWrites; savedPreferences[key] = value;')
history = (ROOT / 'src/apps/launcher/recentroms.h').read_text().split('enum class RomSystem')[1]
history = 'enum class RomSystem' + history + '\n#define fopen fixtureOpen\n'
history += (ROOT / 'src/apps/launcher/recentroms.cpp').read_text().split('namespace {', 1)[1]
history = history.replace('\nconstexpr size_t kMaxRoms', '\nnamespace {\nconstexpr size_t kMaxRoms', 1)
history += '\n#undef fopen\n'
nvs = nvs.replace('// REAL_HISTORY', history)
nes = (ROOT / 'src/apps/nes/nesapp.cpp').read_text()
nes = '#include "keira/localizations/lang_en.h"\n' + nes[nes.index('NesApp::NesApp'):]
osd = method(ROOT / 'src/apps/nes/osd.cpp', 'void osd_setsound(')
core = ''.join(method(args.source / 'nofrendo.c', signature) for signature in [
    'static int internal_insert(', 'int main_loop(', 'int nofrendo_main('])
core += method(args.source / 'nes/nes.c', 'void nes_emulate(')
gb = method(ROOT / 'src/apps/gameboy/gameboyapp.cpp', 'void GameBoyApp::run()')
# Exercise every startup statement up to gameplay; audio failure is deliberately
# nonfatal in production. No simulated clean exit is needed to persist GB/GBC.
gb = gb[:gb.index('    loadPreferences();')] + '    throw PowerLoss();\n}\n'
fixture = (ROOT / 'tests/emulator_history/host.cpp').read_text()
# Preferences initialization is exercised separately by the menu tests.
nes = 'void EmulatorMenuApp::loadPreferences() {}' + nes
fixture = fixture.replace('// NES_METHODS', nes).replace('// OSD_METHOD', osd)
fixture = fixture.replace('// CORE_METHODS', core).replace('// GB_STARTUP', gb)
with tempfile.TemporaryDirectory(prefix='keira-emulator-history-') as directory:
    tmp = Path(directory)
    (tmp / 'Arduino.h').write_text((ROOT / 'tests/navigation/string.h').read_text())
    (tmp / 'apps/emulator').mkdir(parents=True)
    (tmp / 'apps/emulator/systemmenu.h').write_text('''#pragma once
class EmulatorMenuApp : public App {
public:
    EmulatorMenuApp(const char* name, const String&, const String&) : App(name) {}
    void loadPreferences();
};
''')
    (tmp / 'keira').mkdir()
    (tmp / 'keira/app.h').write_text('#pragma once\n')
    (tmp / 'host.cpp').write_text(common + nvs + fixture)
    for name, flags in [('normal', []), ('sanitized', ['-fsanitize=address,undefined',
                       '-fno-omit-frame-pointer', '-fno-pie', '-no-pie'])]:
        binary = tmp / name
        subprocess.run(['g++', '-std=c++11', '-g', '-Wall', '-Wextra',
                        '-Wno-unused-parameter', '-Wno-sign-compare', *flags,
                        '-I' + str(tmp), '-I' + str(ROOT / 'src'), str(tmp / 'host.cpp'),
                        '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True,
                       env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1'}, timeout=30)
        print('emulator launch/history ' + name + ': PASS')
