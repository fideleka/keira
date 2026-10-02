"""Host regression: execute extracted production menu/navigation methods, no firmware build."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

def method(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

header = (ROOT / 'src/apps/launcher/launcher.h').read_text()
items = header[header.index('enum class LauncherMenuKind'):header.index('class LauncherApp')]
production = ''.join(method('src/apps/launcher/launcher.cpp', s) for s in [
    'void LauncherApp::showMenu(', 'void LauncherApp::refreshRecentRomItems(',
    'void LauncherApp::refreshRecentRomFolders('])
production += ''.join(method('src/apps/fmanager/fmanager.cpp', s) for s in [
    'bool FileManagerApp::navigateToParent(', 'int FileManagerApp::parentReturnCursor() const',
    'void FileManagerApp::onFileListMenuItem()', 'void FileManagerApp::openCurrentEntry()',
    'bool FileManagerApp::fileListMenuLoadDir()'])
# Actual spawn implementation, with only RTOS/manager boundary mocked.
production += method('src/keira/threadmanager.cpp', 'void ThreadManager::spawn(')
with tempfile.TemporaryDirectory(prefix='keira-navigation-') as tmp:
    tmp = Path(tmp)
    (tmp / 'Arduino.h').write_text((ROOT / 'tests/navigation/string.h').read_text())
    sdk = Path(os.environ.get('LILKA_SDK_MENU_SOURCE',
               str(ROOT.parent / 'lilka-sdk/lib/lilka/src/lilka/menu.cpp')))
    sdk_methods = method(sdk, 'void Menu::setCursor(') + method(sdk, 'void Menu::update(').replace('void Menu::update(', 'void Menu::sdkUpdate(', 1)
    source = (ROOT / 'tests/navigation/host.cpp').read_text().replace('// PRODUCTION_ITEMS', items).replace('// PRODUCTION_METHODS', production).replace('// SDK_METHODS', sdk_methods)
    (tmp / 'host.cpp').write_text(source)
    for name, flags in [('normal', []), ('sanitized', ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie'])]:
        binary = tmp / name
        subprocess.run(['g++', '-std=c++17', '-g', '-Wall', '-Wextra', '-Wno-sign-compare',
                        *flags, '-I' + str(tmp), '-I' + str(ROOT / 'src'), str(tmp / 'host.cpp'), '-o', str(binary)], check=True)
        subprocess.run([str(binary), str(tmp)], check=True, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1'})
        print(name + ': PASS')
