"""Execute real host SCons compile/archive/link; NOT a native PlatformIO build."""
import importlib.util
from pathlib import Path, PureWindowsPath
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(sys.argv.pop(1)).resolve()
SCONS = next((Path.home() / '.platformio/packages/tool-scons').glob('scons-local-*'))
sys.path.insert(0, str(SCONS))
from SCons.Script import Environment
spec = importlib.util.spec_from_file_location('integration', ROOT / 'tools/nofrendo/integration.py')
integration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(integration)

SCONSTRUCT = r'''
import importlib.util
from pathlib import Path
import runpy
root = Path(ARGUMENTS['root'])
work = Path(Dir('#').abspath)
mode = ARGUMENTS.get('mode', 'fixed')
env = Environment(PROJECT_DIR=str(work / 'project'), PROJECT_LIBDEPS_DIR=str(work / 'libdeps'),
                  PIOENV='v2', BUILD_DIR=str(work / 'build'),
                  CCFLAGS=['-std=gnu99', '-O1', '-g', '-ffunction-sections', '-fdata-sections'],
                  LINKFLAGS=['-Wl,--gc-sections'], LIBS=['m'])
env.AddMethod(lambda env: False, 'IsIntegrationDump')
env.AddMethod(lambda env, callback: env.Replace(IRQ_MIDDLEWARE=callback), 'AddBuildMiddleware')
source = work / 'libdeps/v2/arduino-nofrendo/src'
if mode == 'old':
    source = work / 'old-overlay'
    env.Prepend(CPPPATH=[str(source)])
else:
    runpy.run_path(str(work / 'project/nofrendo_build.py'), init_globals={'Import': lambda _: None, 'env': env})
clone = env.Clone()
clone.Prepend(CPPPATH=[str(source)])
# Exercise actual srcnode identity through SCons variant-directory aliases.
VariantDir('variant', str(source), duplicate=0)
files = ['bitmap.c', 'intro.c', 'cpu/nes6502.c']
files += ['nes/' + n + '.c' for n in ('nes_rom', 'nes_ppu', 'nes_pal', 'nes_mmc', 'mmclist', 'nesinput')]
files += [p.relative_to(source).as_posix() for directory in ('sndhrdw', 'mappers')
          for p in sorted((source / directory).glob('*.c'))]
objects = []
for relative in files:
    node = clone.File('variant/' + relative)
    if mode == 'old':
        objects += clone.Object(target='build/old-objects/' + relative + '.o', source=node)
    elif mode == 'bypass':
        objects += clone.Object(target='build/bypassed/' + relative + '.o', source=node)
    else:
        objects += clone['IRQ_MIDDLEWARE'](clone, node)
archive = clone.StaticLibrary(target='build/core', source=objects)
if mode in ('old', 'stale'):
    probe = env.Object(target='build/probe.o', source='probe.c')
    Default(env.Program(target='build/probe', source=probe + archive))
else:
    env.Append(CPPDEFINES=['IRQ_UNIT_TEST'])
    project = []
    for name in ('host.c', 'irq_test.c'):
        node = env.File(str(root / 'tests/nofrendo' / name))
        env['IRQ_MIDDLEWARE'](env, node)
        project += env.Object(target='build/project/' + name + '.o', source=node)
    Default(env.Program(target='build/irq-test', source=project + archive))
'''


class LinkTests(unittest.TestCase):
    def test_windows_paths_and_variant_alias_fail_closed(self):
        self.assertEqual(integration.relative_source(PureWindowsPath('C:/LIBDEPS/CORE/src/cpu/nes6502.c'),
                         PureWindowsPath(r'c:\libdeps\core\src')).as_posix(), 'cpu/nes6502.c')
        self.assertEqual(integration.relative_source(PureWindowsPath('//HOST/share/src/cpu/a.c'),
                         PureWindowsPath('//host/SHARE/src')).as_posix(), 'cpu/a.c')
        self.assertIsNone(integration.relative_source(PureWindowsPath('D:/src/cpu/a.c'),
                          PureWindowsPath('C:/src')))
        # Path on Windows is WindowsPath (case insensitive); Linux must not
        # case-fold genuine different paths. This is not a Windows runner.
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            source = tmp / 'src'
            source.mkdir()
            (source / 'cpu.c').write_text('int core;')
            overlay = tmp / 'build/nofrendo-irq-src-key'
            overlay.mkdir(parents=True)
            (overlay / 'cpu.c').write_text('int fixed;')
            env = Environment()
            env.VariantDir(str(tmp / 'variant'), str(source), duplicate=0)
            node = env.File(str(tmp / 'variant/cpu.c'))
            # Previous get_abspath/relative_to silently misses this real alias.
            self.assertIsNone(integration.relative_source(Path(node.get_abspath()), source))
            self.assertEqual(integration.source_path(node), source / 'cpu.c')
            env.AddMethod(lambda env, callback: env.Replace(IRQ_MIDDLEWARE=callback), 'AddBuildMiddleware')
            integration.install(env, source, overlay, tmp / 'build', 'key')
            clone = env.Clone()
            del clone['IRQ_MIDDLEWARE']  # simulate dependent-lib registration loss
            with self.assertRaisesRegex(ValueError, 'original core'):
                clone.Object(target=str(tmp / 'bad.o'), source=node)
            # An already-created original object must not sneak into an archive.
            old_env = Environment()
            old_object = old_env.Object(target=str(tmp / 'old.o'), source=str(source / 'cpu.c'))
            with self.assertRaisesRegex(ValueError, 'original core reached archive'):
                clone.StaticLibrary(target=str(tmp / 'bad-core'), source=old_object)
            result = env['IRQ_MIDDLEWARE'](clone, node)
            self.assertEqual(Path(result[0].sources[0].get_abspath()), overlay / 'cpu.c')
            alias = tmp / 'source-alias'
            alias.symlink_to(source, target_is_directory=True)
            alias_result = env['IRQ_MIDDLEWARE'](clone, env.File(str(alias / 'cpu.c')))
            self.assertEqual(alias_result[0], result[0])

    def test_full_archive_link_repeat_upgrade_and_bypass(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            source = tmp / 'libdeps/v2/arduino-nofrendo/src'
            shutil.copytree(SOURCE, source)
            shutil.copytree(ROOT / 'tools/nofrendo', tmp / 'project/tools/nofrendo')
            shutil.copy2(ROOT / 'nofrendo_build.py', tmp / 'project/nofrendo_build.py')
            spec = importlib.util.spec_from_file_location('patch', ROOT / 'tools/nofrendo/apply.py')
            patch = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(patch)
            shutil.copytree(source, tmp / 'old-overlay')
            (tmp / 'probe.c').write_text('int main(void) { return 0; }'+chr(10))
            (tmp / 'SConstruct').write_text(SCONSTRUCT)
            def build(mode='fixed', success=True):
                result = subprocess.run([sys.executable, str(SCONS.parent / 'scons.py'), '-Q', '-j2',
                                         'root=' + str(ROOT), 'mode=' + mode], cwd=tmp,
                                        text=True, capture_output=True)
                (tmp / (mode + '.log')).write_text(result.stdout + result.stderr)
                self.assertEqual(result.returncode == 0, success, result.stdout[-2000:] + result.stderr[-4000:])
                return result.stdout + result.stderr
            build('old')
            (tmp / 'probe.c').write_text('extern void nes6502_setirq(unsigned char, unsigned char); int main(void) { nes6502_setirq(1, 0); return 0; }')
            output = build('old', False)
            self.assertIn('undefined reference', output)
            self.assertIn('nes6502_setirq', output)
            before = (tmp / 'build/old-objects/cpu/nes6502.c.o').read_bytes()
            build()
            output = subprocess.check_output([str(tmp / 'build/irq-test')], cwd=tmp, text=True)
            self.assertIn('PASS', output)
            archives = list((tmp / 'build').glob('libnofrendo-*-core.a'))
            self.assertEqual(len(archives), 1)
            symbols = subprocess.check_output(['nm', '-g', str(archives[0])], text=True)
            for symbol in ('nes6502_setirq',):
                self.assertIn(' T ' + symbol, symbols)
            cpu_object = next((tmp / 'build').glob('nofrendo-objects-*/cpu/nes6502.c.o'))
            stamp = cpu_object.stat().st_mtime_ns
            repeat = build()
            self.assertIn('up to date', repeat)
            self.assertEqual(cpu_object.stat().st_mtime_ns, stamp)
            self.assertEqual((tmp / 'build/old-objects/cpu/nes6502.c.o').read_bytes(), before)
            # Changing integration policy is an explicit signature dependency,
            # even when the verified overlay bytes/key stay the same.
            policy = tmp / 'project/tools/nofrendo/integration.py'
            policy.write_bytes(policy.read_bytes() + b'\n# fixture signature change\n')
            policy_change = build()
            self.assertIn('nes6502.c', policy_change)
            self.assertNotEqual(cpu_object.stat().st_mtime_ns, stamp)
            stamp = cpu_object.stat().st_mtime_ns
            self.assertIn('up to date', build())
            # Actual reviewed-key rollover under the SAME SCons signatures/cache.
            manifest = tmp / 'project/tools/nofrendo/source-manifest.json'
            manifest.write_bytes(manifest.read_bytes() + b' ')
            rollover = build()
            self.assertIn('nes6502.c', rollover)
            self.assertEqual(cpu_object.stat().st_mtime_ns, stamp)
            self.assertEqual(len(list((tmp / 'build').glob('libnofrendo-*-core.a'))), 2)
            self.assertIn('PASS', subprocess.check_output([str(tmp / 'build/irq-test')], cwd=tmp, text=True))
            self.assertIn('up to date', build())
            bypass = build('bypass', False)
            self.assertIn('original core reached Object builder', bypass)
            patch.verify(source, 'original')
            print('PASS actual SCons full core archive/project link, semantic IRQ API, incremental, uncorrected-core upgrade, variant bypass gate')


if __name__ == '__main__':
    unittest.main()
