"""Compile a verified IRQ-corrected overlay, never alter libdeps sources."""
import importlib.util
from pathlib import Path

Import("env")


def configure(env):
    # IDE dumps must not require downloaded dependencies or write overlays.
    if env.IsIntegrationDump():
        return

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
        # Apply to project clones as well: CPU context layout must agree everywhere.
        build_env.Replace(CPPPATH=[str(overlay)] + [
            path for path in build_env.get("CPPPATH", []) if str(path) != str(overlay)
        ])
        path = Path(node.get_abspath()).resolve()
        try:
            relative = path.relative_to(source)
        except ValueError:
            return node
        # Return an object builder result, the supported middleware replacement.
        return build_env.Object(str(overlay / relative))


    env.AddBuildMiddleware(use_nofrendo_overlay)
    print("Nofrendo IRQ overlay verified: a5a5c1a1 (frame / DMC / MMC3)")


configure(env)
