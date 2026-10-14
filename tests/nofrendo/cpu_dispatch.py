"""Compare the real optimized CPU with the reviewed expanded level-IRQ policy."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('patch', ROOT / 'tools/nofrendo/apply.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
# The pre-optimization instruction-boundary policy, not the original buggy IRQ core.
POLICY = '''#define POLL_LEVEL_IRQ() \\
   do { \\
      if (!cpu.jammed && !i_flag && cpu.irq_sources && remaining_cycles > 0) { \\
         IRQ_PROC(); \\
         ADD_CYCLES(INT_CYCLES); \\
      } \\
   } while (0)
'''
with tempfile.TemporaryDirectory(prefix='nes-dispatch-parity-') as tmp:
    work = Path(tmp)
    source = patch.prepare(ROOT / '.pio/libdeps/v2/arduino-nofrendo/src', work / 'core')
    current = (source / 'cpu/nes6502.c').read_text()
    start = current.index('/* Share the cold level-IRQ entry')
    end = current.index('\n#ifdef NES6502_JUMPTABLE\n\n#define OPCODE_BEGIN', start)
    reference = current[:start] + POLICY + current[end:]
    cold = '''service_level_irq:
   IRQ_PROC();
   ADD_CYCLES(INT_CYCLES);
   OPCODE_END

'''
    assert reference.count(cold) == 1
    reference = reference.replace(cold, '', 1)
    (source / 'cpu/reference.c').write_text(reference)
    names = ['setcontext', 'getcontext', 'execute', 'setirq', 'reset', 'irq', 'nmi', 'burn', 'release', 'getbyte', 'getcycles']
    flags = ['-std=gnu99', '-O2', '-g', '-fsanitize=address,undefined', '-fno-pie', '-I'+str(source)]
    subprocess.run(['gcc', *flags, *['-Dnes6502_'+n+'=reference_nes6502_'+n for n in names],
                    '-c', str(source/'cpu/reference.c'), '-o', str(work/'reference.o')], check=True)
    subprocess.run(['gcc', *flags, '-c', str(source/'cpu/nes6502.c'), '-o', str(work/'current.o')], check=True)
    subprocess.run(['gcc', *flags, '-no-pie', str(ROOT/'tests/nofrendo/cpu_dispatch.c'),
                    str(work/'reference.o'), str(work/'current.o'), '-o', str(work/'parity')], check=True)
    environment = os.environ.copy()
    environment.update(ASAN_OPTIONS='detect_leaks=0:halt_on_error=1', UBSAN_OPTIONS='halt_on_error=1')
    subprocess.run([str(work/'parity')], env=environment, check=True)
