"""Summarize Keira measurement logs. Phase times are overlapping wall times, NOT CPU time."""
import argparse
import re
from collections import defaultdict
from pathlib import Path


def parse(text):
    windows = []
    current = None
    for line in text.splitlines():
        # Remove terminal color escapes and monitor control-character renderings.
        line = re.sub(r'\x1b\[[0-9;]*m', '', line)
        if 'measure: app=' in line:
            header = re.search(r'measure: app=(.*?) mode=(\w+) window-ms=(\d+) polls=(\d+) submits=(\d+) changes=(\d+) gap-us=(\d+)/(\d+) gaps>33/50/100ms=(\d+)/(\d+)/(\d+)', line)
            if not header:
                continue
            values = header.groups()
            current = dict(app=values[0], mode=values[1], ms=int(values[2]), polls=int(values[3]), submits=int(values[4]), changes=int(values[5]), gapavg=int(values[6]), gapmax=int(values[7]), over33=int(values[8]), over50=int(values[9]), over100=int(values[10]), phases={}, complete=False)
            windows.append(current)
        elif current and not current['complete'] and 'measure-span:' in line:
            m = re.search(r'poll-span-us=(\d+) submit-span-us=(\d+)', line)
            if m:
                current['pollspan'], current['submitspan'] = map(int, m.groups())
        elif current and not current['complete'] and 'measure-phase:' in line:
            for name, count, total, average, maximum in re.findall(r'([a-z-]+)=(\d+),(\d+),(\d+),(\d+)', line):
                if name != 'none':
                    current['phases'][name] = tuple(map(int, (count, total, average, maximum)))
        elif current and not current['complete'] and 'measure-log:' in line:
            current['complete'] = True
            drop = re.search(r'dropped-this-report=(\d+)', line)
            current['drops'] = int(drop[1]) if drop else -1
            total = re.search(r'dropped-total=(\d+)', line)
            current['dropstotal'] = int(total[1]) if total else -1
    return windows


def summarize(windows):
    groups = defaultdict(list)
    for window in windows:
        groups[(window['app'], window['mode'])].append(window)
    for (app, mode), rows in groups.items():
        print(f'\n{app} / {mode}: {len(rows)} windows')
        if any(not r['complete'] or r.get('drops', -1) for r in rows) or len({r.get('dropstotal', -1) for r in rows}) > 1:
            print('WARNING: incomplete/dropped report(s); missing phase data must not be treated as zero.')
        for countkey, spankey, label in [('polls', 'pollspan', 'Input polls/s'), ('submits', 'submitspan', 'Submitted frames/s')]:
            usable = [r for r in rows if r.get(spankey, 0) and r[countkey] > 1]
            rate = sum(r[countkey] - 1 for r in usable) * 1e6 / sum(r[spankey] for r in usable) if usable else 0
            print(f'{label}: {rate:.2f}')
        print('Worst input gap: %.2f ms; gaps >33/>50/>100 ms: %s' % (max(r['gapmax'] for r in rows) / 1000, '/'.join(str(sum(r[k] for r in rows)) for k in ('over33', 'over50', 'over100'))))
        print('Phase                 samples     avg ms     max ms')
        phases = defaultdict(lambda: [0, 0, 0])
        for row in rows:
            for name, (count, total, average, maximum) in row['phases'].items():
                item = phases[name]
                item[0] += count
                item[1] += total
                item[2] = max(item[2], maximum)
        for name, (count, total, maximum) in phases.items():
            print(f'{name:20s} {count:8d} {total/count/1000 if count else 0:10.3f} {maximum/1000:10.3f}')
    print('\nNested phases must NOT be added. Clock durations include preemption. Polling is not physical-button-to-photon latency.')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('log', type=Path)
    args = p.parse_args()
    windows = parse(args.log.read_text(encoding='utf-8', errors='replace'))
    if not windows:
        p.error('No measurement windows found; use a -measure build and capture through its summary.')
    summarize(windows)
