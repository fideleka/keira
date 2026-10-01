"""Verified, idempotent nofrendo source overlay. Never edits the dependency."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tempfile

HERE = Path(__file__).resolve().parent


def canonical(data):
    # Tolerate only Git CRLF conversion, not other source changes.
    return data.replace(b"\r\n", b"\n")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def verify(source, kind):
    manifest = json.loads((HERE / "source-manifest.json").read_text())
    expected = manifest["files"]
    actual = {p.relative_to(source).as_posix() for p in source.rglob("*") if p.is_file()}
    if actual != set(expected) or any(p.is_symlink() for p in source.rglob("*")):
        raise ValueError("nofrendo source file set differs from verified dependency")
    for name, hashes in expected.items():
        if digest(canonical((source / name).read_bytes())) != hashes[kind]:
            raise ValueError(f"nofrendo {kind} source mismatch: {name}")
    return manifest


def patch_files():
    lines = (HERE / "irq-sources.patch").read_text().splitlines(keepends=True)
    result = {}
    index = 0
    while index < len(lines):
        if not lines[index].startswith("--- a/"):
            raise ValueError("invalid patch file header")
        name = lines[index][6:].rstrip("\n")
        if lines[index + 1] != "+++ b/" + name + "\n":
            raise ValueError("invalid patch destination")
        if Path(name).is_absolute() or ".." in Path(name).parts:
            raise ValueError("unsafe patch path")
        index += 2
        hunks = []
        while index < len(lines) and lines[index].startswith("@@"):
            match = re.fullmatch(r"@@ -(\d+),(\d+) \+(\d+),(\d+) @@\n", lines[index])
            if not match:
                raise ValueError("invalid hunk header")
            start, old_count, new_start, new_count = map(int, match.groups())
            index += 1
            old, new = [], []
            while index < len(lines) and lines[index][0] in " +-" and not lines[index].startswith("--- a/"):
                line = lines[index]
                if line[0] in " -":
                    old.append(line[1:])
                if line[0] in " +":
                    new.append(line[1:])
                index += 1
            if len(old) != old_count or len(new) != new_count:
                raise ValueError("invalid hunk length")
            hunks.append((start - 1, new_start - 1, old, new))
        if name in result or not hunks:
            raise ValueError("duplicate or empty patch file")
        result[name] = hunks
    return result


def prepare(source, destination):
    source, destination = Path(source).resolve(), Path(destination).resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        raise ValueError("overlay must be separate from dependency")
    try:
        manifest = verify(source, "original")
        kind = "original"
    except ValueError:
        manifest = verify(source, "patched")
        kind = "patched"
    # Calculate and verify ALL outputs before writing anything.
    outputs = {name: canonical((source / name).read_bytes()) for name in manifest["files"]}
    for name, hunks in patch_files().items():
        if kind == "patched":
            continue
        original = outputs[name].decode().splitlines(keepends=True)
        output, cursor = [], 0
        for start, new_start, old, new in hunks:
            if start < cursor or original[start:start + len(old)] != old:
                raise ValueError(f"exact patch context mismatch: {name}:{start + 1}")
            output.extend(original[cursor:start])
            if len(output) != new_start:
                raise ValueError("patch destination offset mismatch")
            output.extend(new)
            cursor = start + len(old)
        output.extend(original[cursor:])
        outputs[name] = "".join(output).encode()
    for name, data in outputs.items():
        if digest(data) != manifest["files"][name]["patched"]:
            raise ValueError(f"patched output verification failed: {name}")
    if destination.exists():
        verify(destination, "patched")
        return destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="nofrendo-overlay-", dir=destination.parent) as temp:
        tree = Path(temp) / "src"
        for name, data in outputs.items():
            path = tree / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        verify(tree, "patched")
        tree.rename(destination)
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="verified dependency src directory")
    parser.add_argument("destination", type=Path, help="separate patched src overlay")
    args = parser.parse_args()
    print(prepare(args.source, args.destination))
