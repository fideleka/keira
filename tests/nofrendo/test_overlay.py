"""Fail-closed patch and PlatformIO middleware contract tests (no target build)."""
import importlib.util
from pathlib import Path
import runpy
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(sys.argv.pop(1)).resolve()
spec = importlib.util.spec_from_file_location("patch_test", ROOT / "tools/nofrendo/apply.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


class OverlayTests(unittest.TestCase):
    def test_idempotent_and_immutable(self):
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "fixed"
            patch.prepare(SOURCE, target)
            patch.prepare(SOURCE, target)
            patch.prepare(target, Path(tmp) / "again")
            patch.verify(SOURCE, "original")
            patch.verify(target, "patched")

    def test_fail_closed_variations(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            original = tmp / "source"
            shutil.copytree(SOURCE, original)
            for name in ("cpu/nes6502.c", "cpu/nes6502.h", "nes/nes.c", "mappers/map004.c", "sndhrdw/nes_apu.c", "nes/nes_ppu.c"):
                path = original / name
                old = path.read_bytes()
                path.write_bytes(old + b"\n/* changed dependency */\n")
                with self.assertRaises(ValueError):
                    patch.prepare(original, tmp / "rejected")
                self.assertFalse((tmp / "rejected").exists())
                path.write_bytes(old)
            (original / "unexpected.c").write_text("bad")
            with self.assertRaises(ValueError):
                patch.prepare(original, tmp / "rejected")
            (original / "unexpected.c").unlink()
            (original / "cpu/nes6502.h").unlink()
            with self.assertRaises(ValueError):
                patch.prepare(original, tmp / "rejected")
            with self.assertRaises(ValueError):
                patch.prepare(SOURCE, SOURCE)

    def test_partial_patch_and_stale_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            fixed = patch.prepare(SOURCE, tmp / "fixed")
            partial = tmp / "partial"
            shutil.copytree(SOURCE, partial)
            shutil.copy2(fixed / "cpu/nes6502.c", partial / "cpu/nes6502.c")
            with self.assertRaises(ValueError):
                patch.prepare(partial, tmp / "rejected")
            (fixed / "cpu/nes6502.h").write_text("stale")
            with self.assertRaises(ValueError):
                patch.prepare(SOURCE, fixed)

    def test_corrupt_patch_rejected_before_any_write(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            metadata = tmp / "metadata"
            metadata.mkdir()
            shutil.copy2(patch.HERE / "source-manifest.json", metadata)
            text = (patch.HERE / "irq-sources.patch").read_text()
            (metadata / "irq-sources.patch").write_text(text.replace("+   cpu.irq_sources = 0;", "+   cpu.irq_sources = 1;", 1))
            previous = patch.HERE
            try:
                patch.HERE = metadata
                with self.assertRaises(ValueError):
                    patch.prepare(SOURCE, tmp / "rejected")
                self.assertFalse((tmp / "rejected").exists())
            finally:
                patch.HERE = previous

    def test_versioned_cache_keeps_obsolete_and_user_directories(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            obsolete = tmp / "nofrendo-irq-src"
            obsolete.mkdir()
            (obsolete / "user-file").write_text("do not remove")
            old_key = patch.overlay_key()
            first = patch.prepare(SOURCE, patch.versioned_destination(tmp))
            metadata = tmp / "metadata"
            metadata.mkdir()
            for name in ("source-manifest.json", "irq-sources.patch"):
                shutil.copy2(patch.HERE / name, metadata)
            with (metadata / "source-manifest.json").open("a") as out:
                out.write(" ")  # valid reviewed manifest byte change -> new version
            previous = patch.HERE
            try:
                patch.HERE = metadata
                self.assertNotEqual(old_key, patch.overlay_key())
                second = patch.prepare(SOURCE, patch.versioned_destination(tmp))
                self.assertNotEqual(first, second)
                patch.verify(second, "patched")
            finally:
                patch.HERE = previous
            patch.verify(first, "patched")
            self.assertEqual((obsolete / "user-file").read_text(), "do not remove")

    def test_platformio_source_and_header_contract(self):
        if importlib.util.find_spec("SCons") is None:
            candidates = list((Path.home() / ".platformio/packages/tool-scons").glob("scons-local-*"))
            if candidates:
                sys.path.insert(0, str(candidates[0]))
        from SCons.Script import Environment
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            source = tmp / "libdeps/v2/arduino-nofrendo/src"
            shutil.copytree(SOURCE, source)
            env = Environment(PROJECT_DIR=str(ROOT), PROJECT_LIBDEPS_DIR=str(tmp / "libdeps"),
                              PIOENV="v2", BUILD_DIR=str(tmp / "build"))
            env.AddMethod(lambda env: False, "IsIntegrationDump")
            env.AddMethod(lambda env, callback: env.Replace(IRQ_MIDDLEWARE=callback), "AddBuildMiddleware")
            runpy.run_path(str(ROOT / "nofrendo_build.py"), init_globals={"Import": lambda _: None, "env": env})
            for path in source.rglob("*"):
                if path.suffix in (".c", ".cpp"):
                    node = env["IRQ_MIDDLEWARE"](env, env.File(str(path)))
                    self.assertEqual(Path(node[0].sources[0].get_abspath()), patch.versioned_destination(tmp / "build") / path.relative_to(source))
            project_node = env.File(str(ROOT / "src/apps/nes/osd.cpp"))
            self.assertIs(env["IRQ_MIDDLEWARE"](env, project_node), project_node)
            self.assertEqual(env["CPPPATH"][0], str(patch.versioned_destination(tmp / "build")))
            patch.verify(source, "original")


if __name__ == "__main__":
    unittest.main()
