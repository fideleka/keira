import os
import re
import subprocess
from pathlib import Path

Import("env")


VERSION_PATTERN = re.compile(
    r"^v?(\d+)\.(\d+)\.(\d+)-(\d+)-g([0-9a-f]+)(-dirty)?$"
)


def describe_repository(path):
    try:
        description = subprocess.check_output(
            ["git", "describe", "--tags", "--long", "--always", "--dirty"],
            cwd=path,
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
        short_commit = subprocess.check_output(
            ["git", "rev-parse", "--short=8", "HEAD"],
            cwd=path,
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return {"major": 0, "minor": 0, "patch": 0, "display": "v0.0.0+unknown"}

    match = VERSION_PATTERN.match(description)
    if match:
        major, minor, patch, commits, commit, dirty = match.groups()
        suffix = f"+{commits}.g{commit}"
        if dirty:
            suffix += ".dirty"
        return {
            "major": int(major),
            "minor": int(minor),
            "patch": int(patch),
            "display": f"v{major}.{minor}.{patch}{suffix}",
        }

    dirty = description.endswith("-dirty")
    suffix = f"+g{short_commit}"
    if dirty:
        suffix += ".dirty"
    return {"major": 0, "minor": 0, "patch": 0, "display": f"v0.0.0{suffix}"}


def find_sdk_repository(project_dir):
    configured_path = os.environ.get("LILKA_SDK_REPO")
    candidates = []
    if configured_path:
        candidates.append(Path(configured_path).expanduser())
    candidates.extend(
        [
            project_dir.parent / "sdk",
            project_dir.parent / "lilka-sdk",
            project_dir.parent.parent / "sdk",
            project_dir.parent.parent / "lilka-sdk",
        ]
    )
    return next((path.resolve() for path in candidates if (path / ".git").exists()), None)


project_dir = Path(env.subst("$PROJECT_DIR")).resolve()
sdk_repository = find_sdk_repository(project_dir)
keira_version = describe_repository(project_dir)
sdk_version = describe_repository(sdk_repository) if sdk_repository else {
    "major": 0,
    "minor": 0,
    "patch": 0,
    "display": "v0.0.0+unknown",
}

env.Append(
    CPPDEFINES=[
        "KEIRA_VERSION_AUTO_GEN_H",
        ("KEIRA_VERSION_MAJOR", keira_version["major"]),
        ("KEIRA_VERSION_MINOR", keira_version["minor"]),
        ("KEIRA_VERSION_PATCH", keira_version["patch"]),
        ("KEIRA_VERSION_TYPE", "KEIRA_VERSION_TYPE_DEV"),
        "VERSION_AUTO_GEN_H",
        ("SDK_VERSION_MAJOR", sdk_version["major"]),
        ("SDK_VERSION_MINOR", sdk_version["minor"]),
        ("SDK_VERSION_PATCH", sdk_version["patch"]),
        ("SDK_VERSION_TYPE", "SDK_VERSION_TYPE_DEV"),
        ("KEIRA_PERSONAL_BUILD", 1),
        ("KEIRA_GIT_VERSION", env.StringifyMacro(keira_version["display"])),
        ("SDK_GIT_VERSION", env.StringifyMacro(sdk_version["display"])),
        ("KEIRA_BUILD_LABEL", env.StringifyMacro("ANTON EDITION")),
        ("KEIRA_BUILD_OWNER", env.StringifyMacro("fideleka build")),
        ("KEIRA_BUILD_FOOTER", env.StringifyMacro("Hand-tuned in Toronto")),
    ]
)

print(f"Personal Keira build: {keira_version['display']}")
print(f"Personal SDK build:   {sdk_version['display']}")
