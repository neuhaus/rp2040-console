#!/usr/bin/env python3
"""Destructive RP2040 check of Rogue built with ROGUE_SOFT_REBOOT.

Flashes the given firmware (normally ROGUE_DIAG + ROGUE_SOFT_REBOOT) and a
fresh volume, then keeps ONE serial connection open for every session:

  * each session dirties the game's globals (wizard mode, descends with '>')
    and ends by quitting or by saving;
  * the next session must start on the same USB connection, as a fresh game
    on level 1 with wizard mode off - or, after a save, restore that game;
  * heap growth is sampled across sessions (DIAG heap_free_min/arena).

A dropped USB connection is a failure: that is what soft reboot avoids.
"""
import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import term  # noqa: E402

p = argparse.ArgumentParser(description=__doc__,
                            formatter_class=argparse.RawDescriptionHelpFormatter)
p.add_argument('--build', type=Path, required=True, help='soft-reboot diagnostic build dir')
p.add_argument('--picotool', default='picotool')
p.add_argument('--sessions', type=int, default=30)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
results = {'sessions': [], 'checks': []}


def check(name, ok, detail=''):
    results['checks'].append({'check': name, 'ok': bool(ok), 'detail': detail})
    (a.out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print('PASS' if ok else 'FAIL', name, detail, flush=True)
    if not ok:
        raise SystemExit(f'check failed: {name}: {detail}')


def pico(*args):
    subprocess.run([a.picotool, *map(str, args)], check=True, timeout=120,
                   stdout=subprocess.DEVNULL)


subprocess.run([a.picotool, 'reboot', '-u', '-f'], timeout=60,
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(2)
pico('load', '-v', a.build / 'rogue.uf2')
pico('load', '-v', a.build / 'rogue-volume.uf2')
pico('reboot')
s = term.Session(a.out / 'soft.log')
port = s.port


def more():
    for _ in range(30):
        s.settle(.2)
        if '-more-' in s.screen.line(0).lower() or '--press space' in s.screen.text():
            s.send(' ')
            continue
        return


def cmd(keys, settle=.25):
    more()
    s.send(keys)
    s.settle(settle)


def diag():
    more()
    mark = len(s.raw)
    s.send('\x1d')
    m = s.expect(r'DIAG (.*?) END', raw=True, since=mark)
    s.settle(.2)
    return {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', m.group(1).decode())}


def wait_game(mark):
    """Answer the date prompt if it appears and wait for the status line."""
    end = time.monotonic() + 60
    answered = False
    while time.monotonic() < end:
        s.pump(.2)
        tail = bytes(s.raw[mark:])
        if not answered and b'Enter for' in tail and tail.rstrip().endswith(b':'):
            s.send('\r')
            answered = True
        if b'Press any key for a new game' in tail[-200:]:
            s.send(' ')
            mark = len(s.raw)
        if re.search(r'Level: *\d+', s.screen.line(23)):
            more()
            return
    raise SystemExit('game did not start\n' + s.screen.text())


def end_session(how):
    """Quit or save; then press a key at the restart prompt."""
    mark = len(s.raw)
    if how == 'quit':
        cmd('Q')
        s.expect('really quit')
        cmd('y', settle=.5)
    else:
        cmd('S')
        s.expect('file name')
        cmd('\r', settle=.5)
    end = time.monotonic() + 30
    while b'Press any key for a new game' not in s.raw[mark:]:
        if time.monotonic() > end:
            raise SystemExit('no restart prompt\n' + s.screen.text())
        s.send(' ')
        s.settle(.5)
    check(f'{how}: still the same USB connection', term.find_port() == port, port)
    mark = len(s.raw)
    s.send(' ')
    return mark


mark = 0
wait_game(mark)
saved = None
for n in range(1, a.sessions + 1):
    d = diag()
    status = s.screen.line(23).strip()
    if saved:
        check(f'session {n}: restored the saved game',
              all(d[k] == saved[k] for k in ('level', 'max', 'pack', 'hp')),
              f"level={d['level']} pack={d['pack']} hp={d['hp']}")
    else:
        check(f'session {n}: fresh game, globals reset',
              d['level'] == 1 and d['max'] == 1 and d['wizard'] == 0, status)
    # dirty the globals: wizard mode and a few levels down
    cmd('\x17')
    s.expect('password')
    cmd('bathtub\r')
    depth = 2 + n % 7
    for _ in range(depth):
        cmd('>', settle=.3)
    d = diag()
    results['sessions'].append(dict(d, session=n, restored=bool(saved)))
    how = 'save' if n % 3 == 1 else 'quit'
    saved = d if how == 'save' else None
    if how == 'save':
        cmd('\x17')              # wizard off, so the restore deletes the save
    mark = end_session(how)
    wait_game(mark)

first, last = results['sessions'][0], results['sessions'][-1]
check('heap does not grow across sessions',
      last['heap_free_min'] >= first['heap_free_min'] - 4096,
      f"heap_free_min {first['heap_free_min']} -> {last['heap_free_min']}, "
      f"arena {first['arena']} -> {last['arena']}")
s.close()
print('SUMMARY', json.dumps({'sessions': len(results['sessions']),
                             'heap_free_min_first': first['heap_free_min'],
                             'heap_free_min_last': last['heap_free_min'],
                             'arena_first': first['arena'], 'arena_last': last['arena']}))
