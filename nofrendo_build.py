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
    overlay = patch.prepare(source, patch.versioned_destination(Path(env.subst("$BUILD_DIR"))))
    integration_spec = importlib.util.spec_from_file_location(
        "nofrendo_integration", project / "tools/nofrendo/integration.py")
    integration = importlib.util.module_from_spec(integration_spec)
    integration_spec.loader.exec_module(integration)
    integration.install(env, source, overlay, Path(env.subst("$BUILD_DIR")).resolve(), patch.overlay_key())
    print("Nofrendo IRQ overlay verified: " + patch.overlay_key() + " (namespaced objects/archive)")


configure(env)
