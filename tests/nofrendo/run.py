"""Host-only real-core regressions; commercial ROMs are optional user paths."""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("irq_overlay", ROOT / "tools/nofrendo/apply.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


def build(source, output, unit=False, sanitize=False):
    files = [ROOT / "tests/nofrendo/host.c", source / "bitmap.c"]
    if unit:
        files.append(ROOT / "tests/nofrendo/irq_test.c")
    else:
        files.append(source / "nes/nes.c")
    files.extend(source / ("nes/" + name + ".c") for name in
                 ("nes_rom", "nes_ppu", "nes_pal", "nes_mmc", "mmclist", "nesinput"))
    files += [source / "cpu/nes6502.c", source / "intro.c"]
    files += sorted((source / "sndhrdw").glob("*.c")) + sorted((source / "mappers").glob("*.c"))
    flags = ["-std=gnu99", "-O1", "-g", "-I" + str(source)]
    if unit:
        flags.append("-DIRQ_UNIT_TEST")
    if sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"]
    command = ["gcc", *flags, *map(str, files), "-lm", "-o", str(output)]
    with output.with_suffix(".build.log").open("w") as log:
        subprocess.run(command, check=True, stdout=log, stderr=log)


def run(binary, work, args=(), sanitize=False):
    work.mkdir()
    environment = os.environ.copy()
    if sanitize:
        environment.update(ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",
                           UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    result = subprocess.run([str(binary), *map(str, args)], cwd=work, env=environment,
                            text=True, capture_output=True, timeout=180)
    (work / "run.log").write_text(result.stdout)
    (work / "core.log").write_text(result.stderr)
    if result.returncode:
        raise RuntimeError(f"{binary.name} exited {result.returncode}: {result.stderr[-4000:]}")
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / ".pio/libdeps/v2/arduino-nofrendo/src")
    parser.add_argument("--wacky-rom", type=Path)
    parser.add_argument("--control-rom", type=Path, action="append", default=[])
    parser.add_argument("--frames", type=int, default=1800)
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--output", type=Path, help="new host scratch directory (never .pio)")
    args = parser.parse_args()
    if args.frames < 1800 and args.wacky_rom:
        parser.error("Wacky smoke requires at least 1800 frames")
    if args.output:
        args.output.mkdir(parents=True, exist_ok=False)
        work = args.output.resolve()
    else:
        work = Path(tempfile.mkdtemp(prefix="nofrendo-regression-"))
    print(f"Host artifacts: {work}", flush=True)
    source = args.source.resolve()
    patch.verify(source, "original")
    fixed = patch.prepare(source, work / "fixed-src")
    subprocess.run(["python3", str(ROOT / "tests/nofrendo/test_overlay.py"), str(source)], check=True)
    subprocess.run(["python3", str(ROOT / "tests/nofrendo/test_diagnostics.py"), str(source)], check=True)
    unit = work / "irq-test"
    build(fixed, unit, unit=True, sanitize=args.sanitize)
    print(run(unit, work / "unit", sanitize=args.sanitize).strip(), flush=True)
    if not args.wacky_rom and not args.control_rom:
        return
    baseline, corrected = work / "baseline", work / "corrected"
    build(source, baseline, sanitize=args.sanitize)
    build(fixed, corrected, sanitize=args.sanitize)
    for index, rom in enumerate(args.control_rom):
        rom = rom.resolve()
        old = run(baseline, work / f"control-{index}-baseline", (rom, args.frames), args.sanitize)
        new = run(corrected, work / f"control-{index}-fixed", (rom, args.frames), args.sanitize)
        assert old == new, f"control parity failed: {rom.name}"
        assert "jam=1" not in new
        print(f"PASS {rom.name}: exact sampled PC/cycles/PPU/framebuffer parity ({args.frames} frames)", flush=True)
    if args.wacky_rom:
        old = run(baseline, work / "wacky-baseline", (args.wacky_rom.resolve(), args.frames), args.sanitize)
        new = run(corrected, work / "wacky-fixed", (args.wacky_rom.resolve(), args.frames), args.sanitize)
        assert "frame=7 pc=0020" in old and "jam=1" in old, "baseline failure no longer reproduced"
        assert "jam=1" not in new, "fixed core jammed"
        assert f"frame={args.frames} " in new
        late = [line for line in new.splitlines() if line.startswith("frame=") and
                int(line.split()[0].split("=")[1]) >= 60]
        assert all(int(line.split("colors=")[1].split()[0]) > 1 for line in late)
        assert len({line.split("hash=")[1].split()[0] for line in late}) > 3
        print(f"PASS Wacky startup + scripted Start: {args.frames} frames, no JAM, changing nonuniform output", flush=True)
        print(new.splitlines()[-1], flush=True)


if __name__ == "__main__":
    main()
