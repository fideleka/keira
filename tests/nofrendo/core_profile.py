"""Real PPU differential parity; pinned original renderer vs current overlay.
Pass --baseline pointing to a prior reviewed overlay; no dependency edits/builds.
"""
import argparse
import os
import importlib.util
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('patch', ROOT / 'tools/nofrendo/apply.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


def build(source, binary, sanitize, profile=False):
    files = [ROOT / 'tests/nofrendo/host.c', ROOT / 'tests/nofrendo/ppu_scanline.c',
             source / 'bitmap.c', source / 'nes/nes.c', source / 'cpu/nes6502.c', source / 'intro.c']
    files += [source / ('nes/' + n + '.c') for n in ('nes_rom', 'nes_pal', 'nes_mmc', 'mmclist', 'nesinput')]
    files += sorted((source / 'sndhrdw').glob('*.c')) + sorted((source / 'mappers').glob('*.c'))
    if profile:
        files = [ROOT / 'tests/nofrendo/host.c', ROOT / 'tests/nofrendo/cpu_profile.c'] + files[2:] + [source / 'nes/nes_ppu.c']
        files.remove(source / 'nes/nes.c')
    flags = ['-std=gnu99', '-O2', '-DIRQ_UNIT_TEST', '-DESP32', '-I' + str(ROOT / 'tests/nofrendo/hal'), '-I' + str(source)]
    if profile:
        flags.append('-DKEIRA_MEASURE=1')
    if sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    with binary.with_suffix('.build.log').open('w') as log:
        subprocess.run(['gcc', *flags, *map(str, files), '-lm', '-o', str(binary)],
                       stdout=log, stderr=log, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, default=ROOT / '.pio/libdeps/v2/arduino-nofrendo/src')
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    environment = os.environ.copy()
    environment.update(ASAN_OPTIONS='detect_leaks=0:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
    with tempfile.TemporaryDirectory(prefix='nes-ppu-parity-') as tmp:
        work = Path(tmp)
        current = patch.prepare(ROOT / '.pio/libdeps/v2/arduino-nofrendo/src', work / 'core')
        outputs = []
        for index, source in enumerate((args.baseline, current)):
            binary = work / str(index)
            build(source.resolve(), binary, args.sanitize)
            result = subprocess.run([str(binary)], text=True, capture_output=True, check=True, env=environment)
            outputs.append(result.stdout)
        assert outputs[0] == outputs[1], 'PPU pixels/guards/state/latch-order regression'
        assert len(outputs[1].splitlines()) == 8192
        binary = work / 'profile'
        build(current, binary, args.sanitize, profile=True)
        subprocess.run([str(binary)], check=True, env=environment)
    print('PASS: 8192 real PPU cases, all scroll offsets, sprite priorities/flips/edges, masks, skipped lines, mapper latches and full bitmap guards')


if __name__ == '__main__':
    main()
