"""Compile the exact production item declaration/factories at the target C++ standard."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PREFIX = '''#include <Arduino.h>
#include <cassert>
#include <cstdint>
#include <functional>
#include <vector>
using menu_icon_t = int;
const menu_icon_t app_img = 1;
namespace lilka { namespace colors { const uint16_t White = 0; } }
'''
SUFFIX = '''
int main() {
    int calls = 0;
    auto action = ITEM::MENU("Menu", [&calls]() { ++calls; });
    auto app = ITEM::APP("App", [&calls]() { ++calls; });
    auto ordinary = ITEM::SUBMENU("Ordinary", {action, app});
    auto recent = ITEM::SUBMENU("Recent", {app}, nullptr, 0, LauncherMenuKind::NES);
    assert(action.kind == LauncherMenuKind::Static);
    assert(app.kind == LauncherMenuKind::Static);
    assert(ordinary.kind == LauncherMenuKind::Static);
    assert(recent.kind == LauncherMenuKind::NES);
    int updates = 0;
    auto dynamic = ITEM::SUBMENU("Dynamic", {action}, nullptr, 0, LauncherMenuKind::Static,
                                 [&updates](void*) { ++updates; });
    assert(!ordinary.update && dynamic.update);
    dynamic.update(nullptr);
    assert(updates == 1 && dynamic.submenu.size() == 1);
    String title = "Owned title";
    auto owned = ITEM::APP(title.c_str(), nullptr);
    title = "Changed";
    assert(owned.name == "Owned title");
    action.callback();
    app.callback();
    assert(calls == 2 && ordinary.submenu.size() == 2);
}
'''


def run():
    header = (ROOT / 'src/apps/launcher/launcher.h').read_text()
    items = header[header.index('enum class LauncherMenuKind'):header.index('class LauncherApp')]
    compiler = os.environ.get('CXX', 'g++')
    with tempfile.TemporaryDirectory(prefix='keira-cxx11-') as directory:
        tmp = Path(directory)
        (tmp / 'Arduino.h').write_text((ROOT / 'tests/navigation/string.h').read_text())
        source = tmp / 'items.cpp'
        source.write_text(PREFIX + items + SUFFIX)
        command = [compiler, '-std=c++11', '-pedantic-errors', '-Wall', '-Wextra',
                   '-I' + directory, str(source), '-o', str(tmp / 'items')]
        subprocess.run(command, check=True)
        subprocess.run([str(tmp / 'items')], check=True)
        # Reintroduce f3153c6's default-member aggregate defect in a disposable
        # negative-control source. The production declaration above stays exact.
        broken = items.replace('    LauncherMenuKind kind;',
                               '    LauncherMenuKind kind = LauncherMenuKind::Static;', 1)
        broken = broken.replace('            LauncherMenuKind::Static,\n', '', 1)
        assert broken != items
        source.write_text(PREFIX + broken + SUFFIX)
        failure = subprocess.run(command, capture_output=True, text=True)
        assert failure.returncode != 0, 'C++11 negative control unexpectedly compiled'
        assert 'no matching function' in failure.stderr and 'item_t' in failure.stderr, failure.stderr
        print('C++11 production factories: PASS; old aggregate defect rejected')


if __name__ == '__main__':
    run()
