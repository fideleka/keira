"""Production is quiet; measurement is an explicit pre-build opt-in."""
import os


def configure(env):
    selected = os.environ.get('KEIRA_MEASURE', '0')
    if selected not in ('0', '1'):
        raise RuntimeError('KEIRA_MEASURE must be 0 or 1.')
    enabled = int(selected)
    env.Append(CPPDEFINES=[
        ('KEIRA_MEASURE', enabled),
        ('LILKA_MEASURE_INPUT', enabled),
        ('LILKA_MEASURE_DISPLAY', enabled),
        ('LILKA_LOG_LEVEL', 2 if enabled else 1),
        ('CORE_DEBUG_LEVEL', 1),
    ])


try:
    Import('env')
except NameError:
    pass
else:
    configure(env)
