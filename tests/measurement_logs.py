"""Parser handles compact phases, incomplete/dropped reports and span-based rates."""
import importlib.util,io,contextlib,sys
sys.dont_write_bytecode = True
from pathlib import Path
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('measurement_parser',root/'tools/measurement/analyze.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
log='''measure: app=Game Boy mode=Both window-ms=9000 polls=301 submits=121 changes=6 gap-us=16667/80000 gaps>33/50/100ms=3/1/0
measure-span: app=Game Boy poll-span-us=5000000 submit-span-us=5000000
measure-phase: app=Game Boy input=301,30000,99,300 core-draw=120,240000,2000,3000
measure-phase: app=Game Boy core-skip=180,180000,1000,2000 blit=120,60000,500,1000
measure-log: app=Game Boy enqueue-us=300 dropped-total=1 dropped-this-report=0 emu-stack-free=2000
'''
rows=module.parse(log);assert len(rows)==1 and rows[0]['complete'];assert rows[0]['app']=='Game Boy' and rows[0]['phases']['core-draw']==(120,240000,2000,3000)
out=io.StringIO()
with contextlib.redirect_stdout(out):module.summarize(rows)
assert 'Input polls/s: 60.00' in out.getvalue() and 'Submitted frames/s: 24.00' in out.getvalue();assert 'WARNING' not in out.getvalue()
# Missing next header cannot overwrite the completed previous window.
assert module.parse(log+'measure-phase: app=Game Boy core-draw=1,1,1,1\n')[0]['phases']['core-draw'][0]==120
out=io.StringIO()
with contextlib.redirect_stdout(out):module.summarize(module.parse(log.replace('dropped-this-report=0','dropped-this-report=2')))
assert 'WARNING' in out.getvalue()
assert not module.parse(log.rsplit('measure-log:',1)[0])[0]['complete']
print('PASS: parser phase counts, span-based rates, incomplete/dropped reports and lost-header protection')
