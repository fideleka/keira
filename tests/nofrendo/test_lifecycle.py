"""Real SCons nodes/clones; PlatformIO adapter boundary remains a fixture."""
import importlib.util
from pathlib import Path
import runpy
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(sys.argv.pop(1)).resolve()
# Use existing bundled SCons only; never install tools or invoke target builders.
if importlib.util.find_spec("SCons") is None:
    candidates = list((Path.home() / ".platformio/packages/tool-scons").glob("scons-local-*"))
    if candidates:
        sys.path.insert(0, str(candidates[0]))
from SCons.Script import Environment

spec = importlib.util.spec_from_file_location("lifecycle_patch", ROOT / "tools/nofrendo/apply.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


class LifecycleTests(unittest.TestCase):
    def environment(self, tmp, dump=False):
        env = Environment(PROJECT_DIR=str(ROOT), PROJECT_LIBDEPS_DIR=str(tmp / "libdeps"),
                          PIOENV="v2", BUILD_DIR=str(tmp / "build"), CPPPATH=[str(SOURCE)])
        env.AddMethod(lambda env: dump, "IsIntegrationDump")
        # Match public callback(env, node) registration; not a PlatformIO implementation.
        env.AddMethod(lambda env, callback: env.Replace(IRQ_MIDDLEWARE=callback), "AddBuildMiddleware")
        return env

    def load(self, env):
        runpy.run_path(str(ROOT / "nofrendo_build.py"), init_globals={"Import": lambda _: None, "env": env})

    def test_dump_missing_and_changed_dependency_is_read_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            env = self.environment(tmp, True)
            self.load(env)
            self.assertNotIn("IRQ_MIDDLEWARE", env)
            self.assertFalse((tmp / "build").exists())
            source = tmp / "libdeps/v2/arduino-nofrendo/src"
            source.mkdir(parents=True)
            (source / "nofrendo.c").write_text("unverified")
            self.load(env)
            self.assertFalse((tmp / "build").exists())

    def test_build_missing_and_changed_dependency_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            with self.assertRaises(ValueError):
                self.load(self.environment(tmp))
            source = tmp / "libdeps/v2/arduino-nofrendo/src"
            shutil.copytree(SOURCE, source)
            with (source / "cpu/nes6502.h").open("ab") as out:
                out.write(b"/* changed */")
            with self.assertRaises(ValueError):
                self.load(self.environment(tmp))
            self.assertFalse((tmp / "build").exists())

    def test_real_scons_objects_and_cloned_header_precedence(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            source = tmp / "libdeps/v2/arduino-nofrendo/src"
            shutil.copytree(SOURCE, source)
            env = self.environment(tmp)
            self.load(env)
            clone = env.Clone()
            clone.Prepend(CPPPATH=[str(source)])
            overlay = tmp / "build/nofrendo-irq-src"
            for path in source.rglob("*.c"):
                result = env["IRQ_MIDDLEWARE"](clone, env.File(str(path)))
                self.assertEqual(Path(result[0].sources[0].get_abspath()), overlay / path.relative_to(source))
                self.assertEqual(clone["CPPPATH"][0], str(overlay))
            project_node = env.File(str(ROOT / "src/apps/nes/osd.cpp"))
            self.assertIs(env["IRQ_MIDDLEWARE"](env, project_node), project_node)
            self.assertEqual(env["CPPPATH"][0], str(overlay))
            patch.verify(source, "original")

    def test_crlf_only_normalization_and_semantic_rejection(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            source = tmp / "source"
            shutil.copytree(SOURCE, source)
            for path in source.rglob("*"):
                if path.is_file():
                    path.write_bytes(patch.canonical(path.read_bytes()).replace(b"\n", b"\r\n"))
            overlay = patch.prepare(source, tmp / "fixed")
            patch.verify(overlay, "patched")
            self.assertNotIn(b"\r\n", (overlay / "cpu/nes6502.h").read_bytes())
            path = source / "cpu/nes6502.h"
            path.write_bytes(path.read_bytes() + b" /* semantic change */")
            with self.assertRaises(ValueError):
                patch.prepare(source, tmp / "rejected")


if __name__ == "__main__":
    unittest.main()
