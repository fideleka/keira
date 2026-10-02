"""Offline Xtensa object/stack gate; never invokes PlatformIO, fetches, or writes .pio."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shlex
import subprocess
import tempfile


def compile_object(entry, project, dependencies, output, source):
    args = shlex.split(entry['command'])
    command = []
    skip = False
    for arg in args:
        if skip:
            skip = False
            continue
        if arg in ('-o', '-MF', '-MT', '-MQ'):
            skip = True
            continue
        if arg in ('-MMD', '-MD', '-MP') or arg == entry['file']:
            continue
        arg = arg.replace('\\"', '"')
        if arg.startswith('-I.pio/'):
            arg = '-I' + str(dependencies / arg[2:])
        command.append(arg)
    assert '-Os' in command and '-std=gnu++11' in command, 'Expected actual v2 -Os/gnu++11 flags'
    if source.suffix == '.c':
        command[0] = command[0].replace('g++', 'gcc')
        command = [('-std=gnu99' if arg == '-std=gnu++11' else arg)
                   for arg in command if arg != '-fno-rtti']
    target = output / (source.stem + '.o')
    command += ['-I' + str(dependencies / '.pio/libdeps/v2/JPEGDEC/src'),
                '-fstack-usage', '-save-temps=obj', '-o', str(target), str(source)]
    result = subprocess.run(command, cwd=project, capture_output=True, text=True)
    (output / (source.stem + '.log')).write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(source.name + ':\n' + result.stderr)
    return {'source': str(source), 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
            'object_sha256': hashlib.sha256(target.read_bytes()).hexdigest(), 'command': command}


def frame(path, pattern, optional=False):
    matches = []
    for line in path.read_text().splitlines():
        fields = line.split('\t')
        if re.search(pattern, fields[0]):
            assert fields[2] == 'static', 'Unbounded/dynamic frame: ' + line
            matches.append(int(fields[1]))
    if optional and not matches:
        return 0
    assert len(matches) == 1, (pattern, matches)
    return matches[0]


def measure(args, output):
    entries = json.loads(args.compile_commands.read_text())
    launcher = next(e for e in entries if e['file'] == 'src/apps/launcher/launcher.cpp')
    framework = Path(launcher['command'].split('/toolchain-xtensa-esp32s3/')[0]) / 'framework-arduinoespressif32'
    sdk = args.sdk.resolve()
    objects = []
    sources = [args.project / 'src/apps/launcher/launcher.cpp',
               args.project / 'src/apps/fmanager/fmanager.cpp',
               args.project / 'src/apps/launcher/recentroms.cpp',
               framework / 'libraries/Preferences/src/Preferences.cpp',
               framework / 'cores/esp32/esp32-hal-uart.c',
               sdk / 'lib/lilka/src/lilka/menu.cpp']
    for source in sources:
        relative = str(source.relative_to(args.project)) if source.is_relative_to(args.project) else ''
        entry = next((e for e in entries if e['file'] == relative), launcher)
        objects.append(compile_object(entry, args.project, args.dependencies, output, source))
    su = output / 'launcher.su'
    frames = {
        'run': frame(su, r'^launcher.cpp:.*virtual void LauncherApp::run\(\)$'),
        'homeScreen': frame(su, r'^launcher.cpp:.*void LauncherApp::homeScreen\(item_t&\)$'),
        'showMenu': frame(su, r'^launcher.cpp:.*void LauncherApp::showMenu\(.*\)$'),
        'refreshLambda': frame(su, r'^launcher.cpp:.*showMenu\(.*::<lambda\(\)>$', optional=True),
        'refreshFolders': frame(su, r'^launcher.cpp:.*void LauncherApp::refreshRecentRomFolders\(.*\)$'),
        'refreshItems': frame(su, r'^launcher.cpp:.*void LauncherApp::refreshRecentRomItems\(.*\)$', optional=True),
        'buildMainMenu': frame(su, r'^launcher.cpp:.*item_t LauncherApp::buildMainMenu\(.*\)$', optional=True),
        'buildApplicationsMenu': frame(su, r'^launcher.cpp:.*std::vector<item_t> LauncherApp::buildApplicationsMenu\(\)$', optional=True),
        'readRecentRoms': frame(output / 'recentroms.su', r'^recentroms.cpp:.*readRecentRoms\(.*\)$'),
        'readSavedList': frame(output / 'recentroms.su', r'^recentroms.cpp:.*readSavedList\(.*\)$', optional=True),
        'Preferences.getString.buffer': frame(output / 'Preferences.su', r'^Preferences.cpp:.*size_t Preferences::getString\(const char\*, char\*, size_t\)$'),
        'parseList': frame(output / 'recentroms.su', r'^recentroms.cpp:.*parseList\(.*\)$'),
        'Preferences.begin': frame(output / 'Preferences.su', r'^Preferences.cpp:.*bool Preferences::begin\(.*\)$'),
        'log_printf': frame(output / 'esp32-hal-uart.su', r':log_printf$'),
        'log_printfv': frame(output / 'esp32-hal-uart.su', r':log_printfv$'),
    }
    string_getter_line = next(line for line in (output / 'Preferences.su').read_text().splitlines()
                              if 'String Preferences::getString(const char*, String)' in line)
    assert string_getter_line.endswith('\tdynamic'), string_getter_line
    target_bin = Path(objects[0]['command'][0]).parent
    imports = subprocess.check_output([str(target_bin / 'xtensa-esp32s3-elf-nm'), '-u',
                                       str(output / 'recentroms.o')], text=True)
    getter_symbols = [line.split()[-1] for line in imports.splitlines() if 'Preferences9getString' in line]
    uses_vla_getter = '_ZN11Preferences9getStringEPKc6String' in getter_symbols
    # Conservative retained prefix: three open menus and both refresh helpers,
    # although folder refresh actually runs only with two menus on this path.
    prefix = sum(frames[k] for k in ['run', 'homeScreen', 'refreshLambda', 'refreshFolders',
                                    'refreshItems', 'readRecentRoms']) + 3 * frames['showMenu']
    prefix += max(frames['Preferences.begin'], frames['readSavedList'] + frames['Preferences.getString.buffer']) if frames['readSavedList'] else frames['Preferences.begin']
    toolchain = Path(objects[0]['command'][0]).parent.parent
    libc = Path(subprocess.check_output([objects[0]['command'][0], '-fno-rtti',
                                         '-print-file-name=libc.a'], text=True).strip()).resolve()
    objdump = toolchain / 'bin/xtensa-esp32s3-elf-objdump'
    disassembly = subprocess.check_output([str(objdump), '-d', str(libc)], text=True)
    format_frames = {}
    for name in ['vsnprintf', '_vsnprintf_r', '_svfprintf_r', '__ssprint_r']:
        match = re.search(r'<' + name + r'>:\n[^\n]*\bentry\s+a1, (0x[0-9a-f]+|[0-9]+)', disassembly)
        assert match, 'Missing formatter frame: ' + name
        format_frames[name] = int(match.group(1), 0)
    logging_subtotal = frames['log_printf'] + frames['log_printfv'] + sum(format_frames.values())
    report = {'compiler': subprocess.check_output([objects[0]['command'][0], '--version'], text=True).splitlines()[0],
              'sdk_sha': subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip(),
              'frames': frames, 'formatter_frames': format_frames,
              'formatter_archive': str(libc),
              'formatter_archive_sha256': hashlib.sha256(libc.read_bytes()).hexdigest(),
              'retained_prefix_bytes': prefix, 'known_logging_subtotal_bytes': logging_subtotal,
              'uses_Preferences_VLA_String_getter': uses_vla_getter,
              'history_getter_imports': getter_symbols,
              'Preferences_String_getter_dynamic_frame': string_getter_line,
              'prefix_plus_known_logging_bytes': prefix + logging_subtotal, 'objects': objects,
              'caveat': 'Static compiler frames, not a runtime watermark or complete callgraph bound. '
                        '4096-byte external-call allowance plus 2048-byte headroom requires board validation.'}
    (output / 'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps({k: v for k, v in report.items() if k != 'objects'}, indent=2))
    if not args.report_only:
        assert frames['run'] <= 1024, 'Initializer-list temporaries retained across menu/NVS calls'
        assert not uses_vla_getter, 'Recent history calls unbounded Preferences String VLA getter'
        assert frames['readSavedList'] > 0
        assert '_ZN11Preferences9getStringEPKcPcj' in getter_symbols
        assert prefix <= 2048, 'Retained prefix consumes external-call allowance/headroom'
        assert frames['run'] + frames['buildMainMenu'] <= 6144, 'Initialization lacks 2KiB allowance'
        assert frames['buildApplicationsMenu'] <= 3072
        assert frames['buildMainMenu'] > 0 and frames['buildApplicationsMenu'] > 0
        print('actual Xtensa object/stack-budget gate: PASS')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    root = Path(__file__).resolve().parents[2]
    parser.add_argument('--project', type=Path, default=root)
    parser.add_argument('--dependencies', type=Path, default=root)
    parser.add_argument('--compile-commands', type=Path, default=root / 'compile_commands.json')
    parser.add_argument('--sdk', type=Path, default=root.parent / 'lilka-sdk')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--report-only', action='store_true')
    args = parser.parse_args()
    args.project = args.project.resolve()
    args.dependencies = args.dependencies.resolve()
    if args.output:
        args.output.mkdir(parents=True, exist_ok=True)
        measure(args, args.output.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='keira-target-stack-') as directory:
            measure(args, Path(directory))
