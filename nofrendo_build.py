"""Compile a verified IRQ-corrected overlay, never alter libdeps sources."""
import importlib.util
from pathlib import Path

Import("env")

project = Path(env.subst("$PROJECT_DIR")).resolve()
spec = importlib.util.spec_from_file_location("nofrendo_patch", project / "tools/nofrendo/apply.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
source = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "arduino-nofrendo/src"
source = source.resolve()
overlay = patch.prepare(source, Path(env.subst("$BUILD_DIR")) / "nofrendo-irq-src")
# Project includes must use the same context layout as the compiled core.
env.Prepend(CPPPATH=[str(overlay)])


def use_nofrendo_overlay(build_env, node):
    path = Path(node.get_abspath()).resolve()
    try:
        relative = path.relative_to(source)
    except ValueError:
        return node
    # All library translation units use overlay-relative private headers too.
    build_env.PrependUnique(CPPPATH=[str(overlay)])
    return build_env.File(str(overlay / relative))


env.AddBuildMiddleware(use_nofrendo_overlay)
print("Nofrendo IRQ overlay verified: a5a5c1a1 (frame / DMC / MMC3)")
