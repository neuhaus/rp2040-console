#!/usr/bin/env python3
"""Destructive RP2040 resource validation for Rogue: a full-depth ascension.

Flashes the diagnostic firmware (ROGUE_DIAG) and a fresh volume, then:

  stairs   ordinary '>' and '<' on a normal character: refused off the
           stairs, refused upward without the Amulet, and real descents by
           walking to the stairs (levels 1 -> 4)
  descent  wizard descent to the deepest level (99), sampling memory on
           every level; on level 26 the generated Amulet of Yendor is walked
           to and picked up, and that level is left by the real stairs
  pack     the pack is filled until the game reports it full
  deep     checkpoint: save, image the FAT volume, fsck, restore, compare
  ascent   wizard ascent to level 2, sampling every level; ordinary '<' is
           checked off the stairs and used on the stairs with the Amulet
  surface  checkpoint on the level-1 up staircase, restored on the
           ORDINARY firmware, where the ordinary '<' wins the game
  scores   a fresh ordinary game is quit and must reach the score file

Wizard mode is only used for travel ('>'/'<' anywhere) and object creation;
it is switched off for every command whose ordinary behaviour is tested.
Results go to --out: raw serial logs, per-level samples, FAT images.
"""
import argparse
import collections
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
p.add_argument('--diag', type=Path, required=True, help='diagnostic build directory')
p.add_argument('--ordinary', type=Path, required=True, help='ordinary build directory')
p.add_argument('--picotool', default='picotool')
p.add_argument('--out', type=Path, required=True)
p.add_argument('--fs-start', default='0x10180000')
p.add_argument('--fs-end', default='0x10200000')
p.add_argument('--resume-after-win', action='store_true',
               help='re-check a finished win from --out and run only the score stage')
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)

LAST_DUNGEON, AMULET_LEVEL = 99, 26
STAIRS, HORWALL, VERTWALL, DOOR, FLOOR, TUNNEL, TRAP, HIDDEN, MONSTER = (
    0o4, 0o10, 0o20, 0o40, 0o100, 0o200, 0o400, 0o1000, 0o2)
AMULET = 0o400
STEPS = {(-1, -1): 'y', (-1, 0): 'k', (-1, 1): 'u', (0, -1): 'h',
         (0, 1): 'l', (1, -1): 'b', (1, 0): 'j', (1, 1): 'n'}
results = {'levels': [], 'checkpoints': {}, 'checks': []}
s = None


def log(*args):
    print(*args, flush=True)


def save_results():
    (a.out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')


def check(name, ok, detail=''):
    results['checks'].append({'check': name, 'ok': bool(ok), 'detail': detail})
    save_results()
    log('PASS' if ok else 'FAIL', name, detail)
    if not ok:
        raise SystemExit(f'check failed: {name}: {detail}')


# --- board control ---------------------------------------------------------

def pico(*args, timeout=120):
    subprocess.run([a.picotool, *map(str, args)], check=True, timeout=timeout,
                   stdout=subprocess.DEVNULL)


def to_bootsel():
    global s
    if s:
        s.close()
        s = None
    subprocess.run([a.picotool, 'reboot', '-u', '-f'], timeout=60,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + 20
    while subprocess.run([a.picotool, 'info'], capture_output=True).returncode:
        if time.monotonic() > deadline:
            raise SystemExit('board did not enter BOOTSEL')
        time.sleep(.5)


def image_volume(name):
    img = a.out / f'{name}.img'
    pico('save', '-r', a.fs_start, a.fs_end, img, '-t', 'bin')
    fsck = subprocess.run(['fsck.fat', '-n', str(img)], capture_output=True, text=True)
    (a.out / f'{name}-fsck.log').write_text(fsck.stdout + fsck.stderr)
    listing = subprocess.check_output(['mdir', '-i', str(img), '-a', '::'], text=True)
    (a.out / f'{name}-files.log').write_text(listing)
    check(f'{name}: FAT clean', fsck.returncode == 0, fsck.stdout.strip().splitlines()[-1])
    return img, listing


def boot(name):
    """Start the application and wait for the game (new or restored)."""
    global s
    pico('reboot')
    s = term.Session(a.out / f'{name}.log')
    end = time.monotonic() + 60
    while time.monotonic() < end:
        s.pump(.2)
        tail = bytes(s.raw[-400:])
        if b'Enter for' in tail and tail.rstrip().endswith(b':'):
            s.send('\r')
            s.raw.clear()
        if b'Who are you?' in tail and tail.rstrip().endswith(b']'):
            s.send('\r')               # keep the remembered name
            s.raw.clear()
        if re.search(r'Level: *\d+', s.screen.line(23)):
            s.settle(.5)
            more()
            return
    raise SystemExit(f'{name}: game did not start\n{s.screen.text()}')


# --- game interaction ------------------------------------------------------

def dead():
    text = s.screen.text()
    return 'REST' in text and 'IN' in text and 'PEACE' in text or 'Killed by' in text


def more():
    for _ in range(20):
        s.settle(.2)
        if dead():
            raise SystemExit('character died\n' + s.screen.text())
        if '-more-' in s.screen.line(0).lower() or '--press space' in s.screen.text():
            s.send(' ')
            continue
        return


def msg():
    return s.screen.line(0).strip()


def cmd(keys, settle=.25):
    more()
    s.send(keys)
    s.settle(settle)


def diag():
    more()
    s.raw.clear()
    s.send('\x1d')
    m = s.expect(r'DIAG (.*?) END', raw=True)
    s.settle(.2)
    return {k: int(v) for k, v in re.findall(r'(\w+)=(-?\d+)', m.group(1).decode())}


def dungeon_map():
    more()
    s.raw.clear()
    s.send('\x1e')
    m = s.expect(r'(?s)MAP pos=(\d+),(\d+) level=(\d+)\r\n(.*?)MAPEND', raw=True)
    s.settle(.2)
    body = m.group(4).decode()
    grid = []
    for r in range(24):
        hexrow = re.search(rf'M{r:02d} ([0-9a-f]{{320}})', body).group(1)
        grid.append([int(hexrow[i:i + 4], 16) for i in range(0, 320, 4)])
    objects = [(int(r), int(c), int(w, 8)) for r, c, w in re.findall(r'OBJ (\d+),(\d+) ([0-7]+)', body)]
    return (int(m.group(1)), int(m.group(2))), grid, objects


def wizard(on):
    if bool(diag()['wizard']) == on:
        return
    cmd('\x17')
    if on:
        s.expect('password')
        cmd('bathtub\r')
        check('wizard on', 'mighty wizard' in msg(), msg())
    else:
        check('wizard off', 'not wizard anymore' in msg(), msg())


def can_move(g, r1, c1, r2, c2, through_monsters):
    """move.c can_move()/is_passable(), refusing traps on the way."""
    if not (1 <= r2 <= 22 and 0 <= c2 <= 79):
        return False
    cell = g[r2][c2]
    if cell & HIDDEN or cell & TRAP:
        return False
    if cell & MONSTER and not through_monsters:
        return False
    if not cell & (FLOOR | TUNNEL | DOOR | STAIRS):
        return False
    if r1 != r2 and c1 != c2:
        if g[r1][c1] & DOOR or cell & DOOR:
            return False
        if not g[r1][c2] or not g[r2][c1]:
            return False
    return True


def path(g, start, goal):
    """Shortest route avoiding monsters; through them (fighting) if need be."""
    return route(g, start, goal, False) or route(g, start, goal, True)


def route(g, start, goal, through_monsters):
    prev = {start: None}
    q = collections.deque([start])
    while q:
        cur = q.popleft()
        if cur == goal:
            break
        for (dr, dc) in STEPS:
            nxt = (cur[0] + dr, cur[1] + dc)
            if nxt not in prev and can_move(g, cur[0], cur[1], *nxt, through_monsters):
                prev[nxt] = cur
                q.append(nxt)
    if goal not in prev:
        return None
    steps = []
    while prev[goal]:
        steps.append(goal)
        goal = prev[goal]
    return steps[::-1]


def heal_if_needed():
    d = diag()
    m = re.search(r'Hp: *(\d+)\((\d+)\)', s.screen.line(23))
    if m and int(m[1]) * 2 < int(m[2]):
        letter = make_and_find('!', '3', 'extra healing')
        cmd('q')
        cmd(letter)
        log('quaffed extra healing at', d['level'])


def walk_to(goal_fn, what):
    """Reveal the level (wizard ^S) and walk to the cell goal_fn() picks."""
    wizard(True)
    cmd('\x13')          # draw_magic_map: reveals and unhides the level
    for attempt in range(400):
        pos, g, objs = dungeon_map()
        goal = goal_fn(g, objs)
        if goal is None and attempt:
            return pos, g              # stepped onto it: picked up
        if goal is None:
            raise SystemExit(f'no {what} on this level')
        if pos == goal:
            return pos, g
        route = path(g, pos, goal)
        if route is None:
            raise SystemExit(f'no route to {what} from {pos} to {goal}')
        step = route[0]
        cmd(STEPS[(step[0] - pos[0], step[1] - pos[1])], settle=.1)
        if attempt % 10 == 9:
            heal_if_needed()
    raise SystemExit(f'could not reach {what}')


def stairs_of(g, objs):
    for r in range(24):
        for c in range(80):
            if g[r][c] & STAIRS:
                return (r, c)
    return None


def amulet_of(g, objs):
    for r, c, what in objs:
        if what == AMULET:
            return (r, c)
    return None


def inventory_letter(pattern):
    cmd('i', settle=.4)
    found = None
    for r in range(24):
        m = re.search(r'([a-z])\) (.*)', s.screen.line(r))
        if m and re.search(pattern, m.group(2)):
            found = m.group(1)
    cmd(' ')
    more()
    return found


def make_and_find(kind, which, pattern):
    cmd('\x03')
    s.expect('type of object')
    cmd(kind)
    if which is not None:
        s.expect('which kind')
        cmd(which + '\r')
    more()
    return inventory_letter(pattern)


def sample(stage):
    d = diag()
    d['stage'] = stage
    results['levels'].append(d)
    save_results()
    return d


def level_change(key, expect_level, stage):
    cmd(key, settle=.4)
    more()
    d = sample(stage)
    check(f'{stage}: {key!r} reaches level {expect_level}', d['level'] == expect_level,
          f"level={d['level']}")
    return d


def checkpoint(name, firmware=None):
    before = diag()
    cmd('S')
    s.expect('file name')
    mark = len(s.raw)
    cmd('\r', settle=1)
    s.expect('Press any key', timeout=30, raw=True, since=mark)
    to_bootsel()
    img, listing = image_volume(name)
    m = re.search(r'ROGUE~\d+\s+SAV\s+(\d+)', listing)
    save_bytes = int(m.group(1).replace(' ', '')) if m else None
    free = int(re.search(r'([\d ]+) bytes free', listing).group(1).replace(' ', ''))
    if firmware:
        pico('load', '-v', firmware)
    boot(name + '-restore')
    if firmware:
        # The ordinary firmware has no DIAG key: read the restored state from
        # the status line and the inventory instead.
        status = re.search(r'Level: *(\d+).*Hp: *(\d+)', s.screen.line(23))
        after = dict(before, level=int(status[1]), hp=int(status[2]),
                     amulet=int(inventory_letter('[Aa]mulet') is not None))
        keys = ('level', 'hp', 'amulet')
    else:
        after = diag()
        keys = ('level', 'max', 'objects', 'monsters', 'pack', 'amulet', 'hp', 'wizard')
    same = {k: (before[k], after[k]) for k in keys if before[k] != after[k]}
    results['checkpoints'][name] = {'before': before, 'after': after,
                                    'save_bytes': save_bytes, 'free_bytes': free}
    check(f'{name}: restore matches save', not same, json.dumps(same) if same else
          f'save={save_bytes} B, free={free} B')
    return after


def decode_scores(data):
    """score.c stores 80-byte records XORed with xxx(), reset once per file."""
    f, k, out = 37, 7, bytearray()
    for b in data:
        r = (f * k + 9337) % 8887
        f, k = k, r
        out.append(b ^ (r & 0xff))
    return bytes(out)


def check_win(raw):
    """win(): the YOU WON banner, the Fighters' Guild text, the treasure sale
    (which includes the Amulet), the score list and a normal exit."""
    ok = all(t in raw for t in (b"admitted  to  the", b"Fighters' Guild",
                                 b'the amulet of Yendor', b'Top  Ten  Rogueists',
                                 b'Press any key for a new game'))
    check("ordinary '<' with the Amulet on level 1 wins", ok,
          'banner, sale, score list and exit seen' if ok else 'see win-screen.txt')


# --- the run ---------------------------------------------------------------

if a.resume_after_win:
    results = json.loads((a.out / 'results.json').read_text())
    results['checks'] = [c for c in results['checks']
                         if not c['check'].startswith("ordinary '<' with the Amulet")]
    check_win((a.out / 'surface-restore.log').read_bytes())
    to_bootsel()
    image_volume('after-win')

if not a.resume_after_win:
    to_bootsel()
    pico('load', '-v', a.diag / 'rogue.uf2')
    pico('load', '-v', a.diag / 'rogue-volume.uf2')
    boot('start')
    d = sample('start')
    check('new game on level 1', d['level'] == 1 and d['wizard'] == 0)

    # stairs: ordinary '>' and '<'
    pos, g, _ = dungeon_map()
    if not g[pos[0]][pos[1]] & STAIRS:
        cmd('>')
        check("'>' off the stairs is refused", 'no way down' in msg(), msg())
    for level in range(1, 4):
        walk_to(stairs_of, 'stairs')
        wizard(False)
        level_change('>', level + 1, 'stairs')
    walk_to(stairs_of, 'stairs')
    wizard(False)
    cmd('<')
    check("'<' without the Amulet is refused", 'magically blocked' in msg(), msg())

    # strengthen the character for the deep walks (wizard object creation)
    wizard(True)
    letter = make_and_find('!', '5', 'raise level')
    for _ in range(12):
        cmd('q')
        cmd(letter)
        more()
        letter = inventory_letter('raise level') or make_and_find('!', '5', 'raise level')
    log('after raise level:', s.screen.line(23).strip())

    # descent: wizard '>' to the Amulet level, the Amulet, real stairs, then down
    while diag()['level'] < AMULET_LEVEL:
        level_change('>', diag()['level'] + 1, 'descent')
    walk_to(amulet_of, 'Amulet')
    more()
    d = sample('amulet')
    check('Amulet of Yendor picked up on its level', d['amulet'] == 1, f"level={d['level']}")
    walk_to(stairs_of, 'stairs')
    wizard(False)
    level_change('>', AMULET_LEVEL + 1, 'stairs-deep')
    wizard(True)
    while diag()['level'] < LAST_DUNGEON:
        level_change('>', diag()['level'] + 1, 'descent')
    level_change('>', LAST_DUNGEON, 'descent')   # the bottom regenerates in place

    # pack: fill until the game refuses
    for i in range(40):
        cmd('\x03')
        if 'pack full' in msg():
            break
        s.expect('type of object')
        cmd(')')
        s.expect('which kind')
        cmd(str(i % 8) + '\r')
        more()
    d = sample('pack')
    check('pack filled to the game limit', 'pack full' in msg(), f"pack entries={d['pack']}")

    checkpoint('deep')

    # ascent: wizard '<' to level 3, ordinary '<' checks, wizard to level 1
    while diag()['level'] > 3:
        level_change('<', diag()['level'] - 1, 'ascent')
    pos, g, _ = dungeon_map()
    wizard(False)
    if not g[pos[0]][pos[1]] & STAIRS:
        cmd('<')
        check("'<' off the stairs is refused", 'no way up' in msg(), msg())
    walk_to(stairs_of, 'stairs')
    wizard(False)
    level_change('<', 2, 'stairs-up')
    wizard(True)
    level_change('<', 1, 'ascent')
    walk_to(stairs_of, 'stairs')
    check('on the level-1 staircase with the Amulet', diag()['amulet'] == 1)

    # surface: restore on the ordinary firmware and leave the dungeon for real
    checkpoint('surface', firmware=a.ordinary / 'rogue.uf2')
    results['ordinary_firmware_sha256'] = subprocess.check_output(
        ['shasum', '-a', '256', str(a.ordinary / 'rogue.uf2')], text=True).split()[0]
    cmd('\x17')                                    # restored wizard flag: turn it off
    check('ordinary firmware: wizard off', 'not wizard anymore' in msg(), msg())
    s.raw.clear()
    cmd('<', settle=1)
    for _ in range(30):
        if b'Press any key' in s.raw:
            break
        s.send(' ')
        s.settle(.5)
    (a.out / 'win-screen.txt').write_text(s.screen.text())
    check_win(bytes(s.raw))
    to_bootsel()
    img, listing = image_volume('after-win')
    # Rogue keeps the save after restoring a wizard-mode game (save.c restore()),
    # so this volume would resume it again; the score stage uses a fresh volume.


# scores: a fresh ordinary game on a fresh volume, quit, must reach rogue.scores
pico('load', '-v', a.ordinary / 'rogue.uf2')
pico('load', '-v', a.ordinary / 'rogue-volume.uf2')
boot('scores')
cmd('Q')
s.expect('really quit')
cmd('y', settle=1)
for _ in range(10):
    if b'Press any key' in s.raw:
        break
    s.send(' ')
    s.settle(.5)
(a.out / 'scores-screen.txt').write_text(s.screen.text())
to_bootsel()
img, listing = image_volume('after-scores')
scores = subprocess.check_output(['mtype', '-i', str(img), '::rogue.scores'])
(a.out / 'rogue.scores').write_bytes(scores)
check('quit game recorded in rogue.scores', b'rogue' in decode_scores(scores)
      and b'quit' in decode_scores(scores), decode_scores(scores)[:80].decode('latin1').strip())
pico('reboot')

peak = lambda k: max(x[k] for x in results['levels'] + [c['after'] for c in results['checkpoints'].values()])
results['summary'] = {
    'levels_sampled': len(results['levels']),
    'max_level': max(x['level'] for x in results['levels']),
    'heap_inuse_peak': peak('heap_inuse_peak'),
    'arena_peak': peak('arena'),
    'heap_free_min': min(x['heap_free_min'] for x in results['levels']),
    'stack_peak': peak('stack_peak'),
    'stack_reserve': results['levels'][0]['stack_reserve'],
    'objects_peak': peak('objects'),
    'monsters_peak': peak('monsters'),
}
save_results()
log('SUMMARY', json.dumps(results['summary']))
