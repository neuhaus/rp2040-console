#!/usr/bin/env python3
"""Destructive RP2040 validation for NetHack 3.6.7: a full-dungeon ascension
on a volume that also carries bones files, won on the ORDINARY firmware.

Two builds are needed: the ordinary release image and a diagnostic image
built with --sram-diagnostics --sram-max-dungeon (fixed seed, wizard mode,
maximum branch lengths, MEASURE/RECLAIM serial lines). Stages:

  bones     fresh volume; several wizard characters die on different levels
            of the Dungeons of Doom and leave real bones files
  world     a new game carries a bag of 150 rings, visits every level of
            every branch, then revisits and maps them all; every "Get
            bones?" is declined so the bones files stay on flash. Bones
            files, then the score logs, must be cleared only as free space
            falls below the reserve, and no level write may fail
  deep      checkpoint: save, image the FAT volume, fsck
  restore   restore on the diagnostic firmware and compare #stats counts
  ordinary  the deep checkpoint is restored on the ORDINARY firmware
            (explore mode): real travel and staircase commands move between
            cached levels, '<' on level 1 without the Amulet is refused, and
            the game is saved again
  endgame   diagnostic firmware: the real Amulet is carried (#sramamulet;
            a wish may grant the imitation) and the hero stands on the
            level-1 up staircase; the ORDINARY firmware climbs it into Earth
  earth     each plane is checkpointed and restored, then left through its
            magic portal (#sramportal runs the ordinary trap code); on Astral
            the hero is placed on the co-aligned altar
  ascend    the Astral checkpoint is restored on the ORDINARY firmware,
            which offers the Amulet
  scores    the ordinary firmware plays and quits a normal game; the growth
            of record, logfile and xlogfile is measured

--from-stage resumes a run in --out: deep and earth attach to the game still
running on the board, restore and scores use the volume on the board, and
world, ordinary, endgame and ascend reload the FAT image saved before them.

Results go to --out: raw serial logs, results.json and FAT images.
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
p.add_argument('--diag', type=Path, required=True,
               help='diagnostic hardware build directory (nethack.uf2, nethack-volume.uf2)')
p.add_argument('--ordinary', type=Path, required=True,
               help='ordinary hardware build directory')
p.add_argument('--picotool', default='picotool')
p.add_argument('--out', type=Path, required=True)
p.add_argument('--fs-start', default='0x1017a000')
p.add_argument('--fs-end', default='0x10200000')
p.add_argument('--bones-levels', default='3,4,5,6,7,8,9,10,11,12',
               help='Dungeons of Doom levels to die on for bones files')
p.add_argument('--from-stage', default='bones',
               choices=['bones', 'world', 'deep', 'restore', 'ordinary', 'endgame', 'earth', 'ascend', 'scores'],
               help='resume a run from --out at this stage')
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)

STAGES = ['bones', 'world', 'deep', 'restore', 'ordinary', 'endgame', 'earth', 'ascend', 'scores']
FAIL = (b'*** PANIC ***', b'Oops...', b'This game is void', b'cannot write',
        b'HardFault', b'allocation_failed')
results = {'checks': [], 'measures': [], 'reclaims': [], 'volumes': {}}
if a.from_stage != 'bones':
    results = json.loads((a.out / 'results.json').read_text())
s = None
dying = False            # "Die?" -> y only while planting bones; else survive


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

def pico(*args, timeout=180):
    subprocess.run([a.picotool, *map(str, args)], check=True, timeout=timeout,
                   stdout=subprocess.DEVNULL)


def in_bootsel():
    return subprocess.run([a.picotool, 'info'], capture_output=True).returncode == 0


def to_bootsel():
    global s
    if s:
        s.close()
        s = None
    if in_bootsel():
        return
    subprocess.run([a.picotool, 'reboot', '-u', '-f'], timeout=60,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.monotonic() + 30
    while not in_bootsel():
        if time.monotonic() > deadline:
            raise SystemExit('board did not enter BOOTSEL')
        time.sleep(.5)


def volume_files(img):
    """{name: size} for the root and save/ directories of a FAT image."""
    files = {}
    for d in ('::', '::save'):
        out = subprocess.run(['mdir', '-i', str(img), '-a', d], capture_output=True,
                             text=True).stdout
        for line in out.splitlines():
            line = line.strip()
            m = re.match(r'(\S+)\s+(\S*)\s+(\d[\d ]*)\s+\d{4}-\d\d-\d\d\s+\S+(?:\s+(\S+))?$',
                         line)
            if not m:
                continue
            name = (m.group(4) or (m.group(1) + ('.' + m.group(2) if m.group(2) else ''))).lower()
            files[(d[2:] + '/' if d != '::' else '') + name] = int(m.group(3).replace(' ', ''))
    free = re.search(r'([\d ]+) bytes free', subprocess.run(
        ['mdir', '-i', str(img), '::'], capture_output=True, text=True).stdout)
    return files, int(free.group(1).replace(' ', ''))


def image_volume(name):
    img = a.out / f'{name}.img'
    pico('save', '-r', a.fs_start, a.fs_end, img, '-t', 'bin')
    fsck = subprocess.run(['fsck.fat', '-n', str(img)], capture_output=True, text=True)
    (a.out / f'{name}-fsck.log').write_text(fsck.stdout + fsck.stderr)
    files, free = volume_files(img)
    bones = {k: v for k, v in files.items() if k.startswith('bon')}
    levels = {k: v for k, v in files.items() if re.match(r'0game\.\d+$', k)}
    info = {'free': free, 'files': files, 'bones': bones,
            'level_files': len(levels), 'level_bytes': sum(levels.values()),
            'companion': files.get('save/0game')}
    results['volumes'][name] = info
    check(f'{name}: FAT clean', fsck.returncode == 0,
          f"free={free} B, bones={len(bones)} ({sum(bones.values())} B), "
          f"levels={len(levels)} ({info['level_bytes']} B), companion={info['companion']}")
    return info


def flash(build, volume=False):
    pico('load', '-v', build / 'nethack.uf2')
    if volume:
        pico('load', '-v', build / 'nethack-volume.uf2')


def connect(name):
    global s
    if s:
        s.close()
    s = term.Session(a.out / f'{name}.log')
    s.collected = 0


def boot(name, newname=None):
    """Start the application; answer the boot prompts until the map shows."""
    to_bootsel()
    pico('reboot')
    connect(name)
    end = time.monotonic() + 120
    mark = 0
    while time.monotonic() < end:
        s.pump(.2)
        failed()
        tail = bytes(s.raw[mark:])
        answered = True
        if b'Set date and time' in tail and tail.rstrip().endswith(b':'):
            s.send('\r')
        elif b'Who are you?' in tail:
            s.send((newname or 'tester') + '\r')
        elif b'Shall I pick' in tail or b'Is this ok?' in tail:
            s.send('y')
        elif ready():
            more()
            return
        elif b'--More--' in tail:
            s.send(' ')
        else:
            answered = False
        if answered:
            mark = len(s.raw)
    raise SystemExit(f'{name}: game did not start\n{s.screen.text()}')


def wait_exit(name, since):
    """After the game ends: wait for the restart prompt, read the free space.

    Only output from `since` on is answered. No key may reach the board once
    it waits at "Press any key": that key would let _exit() rewrite .clock
    while to_bootsel() resets the board, tearing the FAT update."""
    end = time.monotonic() + 300
    mark = since
    while time.monotonic() < end:
        try:
            s.pump(.3)
        except ConnectionError:
            break
        raw = bytes(s.raw[since:])
        failed(raw)
        if b'Press any key for a new game' in raw:
            m = re.search(rb'(\d+) bytes free on the volume', raw)
            return int(m.group(1)) if m else None
        tail = bytes(s.raw[mark:])
        for prompt, key in ((b'Die?', 'y' if dying else 'n'), (b'Save bones?', 'y'),
                            (b'possessions identified?', 'q'),
                            (b'what you had when you', 'q'),
                            (b'see your attributes?', 'q'), (b'--More--', ' ')):
            if prompt in tail:
                s.send(key)
                mark = len(s.raw)
                break
    raise SystemExit(f'{name}: no restart prompt\n{s.screen.text()}')


# --- game interaction ------------------------------------------------------

def failed(raw=None):
    raw = bytes(s.raw) if raw is None else raw
    for f in FAIL:
        if f in raw:
            raise SystemExit(f'failure marker {f!r} seen\n{s.screen.text()}')


PLANES = r'(Earth|Air|Fire|Water|Astral Plane|End Game) '


def ready():
    return any(re.search(r'Dlvl:\d+', s.screen.line(r))
               or re.match(PLANES, s.screen.line(r)) for r in (22, 23))


def more(limit=60):
    for _ in range(limit):
        s.settle(.3, limit=120)
        failed()
        text = s.screen.text()
        if '--More--' in text:
            s.send(' ')
        elif 'Die?' in s.screen.line(0):
            s.send('y' if dying else 'n')
        elif 'Get bones?' in s.screen.line(0):
            s.send('n')
        elif '(end)' in text or re.search(r'\(\d+ of \d+\)', text):
            s.send(' ')         # next page, or dismiss the last one
        else:
            return
    raise SystemExit('prompts did not end\n' + s.screen.text())


def msg():
    return s.screen.line(0).strip()


def cmd(keys, settle=.3):
    more()
    s.send(keys)
    s.settle(settle, limit=600)


def ext(name, until=None, timeout=1800):
    """Run an extended command; with `until`, wait for that raw marker."""
    more()
    mark = len(s.raw)
    s.send('#' + name + '\r')
    if until:
        end = time.monotonic() + timeout
        asked = mark
        while time.monotonic() < end:
            s.pump(.3)
            failed()
            if until.encode() in s.raw[mark:]:
                break
            if b'Get bones?' in s.raw[asked:]:
                s.send('n')
                asked = len(s.raw)
                results['bones_declined'] = results.get('bones_declined', 0) + 1
            elif b'--More--' in s.raw[asked:]:
                s.send(' ')
                asked = len(s.raw)
        else:
            raise SystemExit(f'#{name}: {until} not seen\n{s.screen.text()}')
    more()
    collect()
    return bytes(s.raw[mark:])


def collect():
    """Record MEASURE and RECLAIM lines not yet seen this session."""
    if s is None:
        return
    raw = bytes(s.raw[s.collected:]).decode('latin1')
    # only complete lines; the rest waits for the next call
    raw = raw[:raw.rfind('\n') + 1]
    s.collected += len(raw.encode('latin1'))
    for m in re.finditer(r'MEASURE (\S+) (.*)', raw):
        d = {k: int(v, 16) if k == 'peakcall' else int(v)
             for k, v in re.findall(r'(\w+)=([0-9a-f]+)', m.group(2))}
        d['phase'] = m.group(1)
        results['measures'].append(d)
    for m in re.finditer(r'RECLAIM (\S+) free_before=(\d+) free_after=(\d+)', raw):
        before, after = int(m.group(2)), int(m.group(3))
        results['reclaims'].append({'file': m.group(1).lower(), 'size': after - before,
                                    'free_before': before, 'free_after': after})
    save_results()


def status():
    text = s.screen.line(22) + ' ' + s.screen.line(23)
    m = re.search(r'Dlvl:(\d+)', text)
    plane = re.match(PLANES, s.screen.line(23))
    return {'dlvl': int(m.group(1)) if m else None,
            'endgame': bool(plane), 'plane': plane.group(1) if plane else None,
            'astral': 'Astral Plane' in text, 'line': text.strip()}


def wait_for(pred, timeout=120):
    """Answer --More-- until the redrawn status line satisfies pred."""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        s.pump(.5)
        failed()
        if '--More--' in s.screen.text():
            s.send(' ')
        elif ready() and pred(status()):
            more()
            return True
    more()
    return pred(status())


def climb(key, level):
    """An ordinary staircase command; wait for the new level to be drawn."""
    cmd(key, settle=.5)
    return wait_for(lambda st: st['dlvl'] == level)


def take_portal():
    """#sramportal, then wait for the deferred level change to be drawn."""
    old = re.sub(r'T:\d+', '', status()['line'])
    out = ext('sramportal', until='MEASURE portal_taken')
    end = time.monotonic() + 120
    while time.monotonic() < end:
        s.pump(.5)
        failed()
        if '--More--' in s.screen.text():
            s.send(' ')
        elif ready() and re.sub(r'T:\d+', '', status()['line']) != old:
            break
    more()
    collect()
    return out


def stats():
    """#stats object/monster/overview counts (wizard mode only)."""
    more()
    mark = len(s.raw)
    s.send('#stats\r')
    end = time.monotonic() + 60
    while time.monotonic() < end:
        s.pump(.3)
        if b'Grand total' in s.raw[mark:]:
            break
    more()
    text = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', ' ', bytes(s.raw[mark:]).decode('latin1'))
    out = {}
    for k in ('Obj total', 'Mon total', 'Over total', 'contained'):
        m = re.search(re.escape(k) + r'\s+(\d+)\s+\d+', text)
        out[k] = int(m.group(1)) if m else None
    collect()
    return out


def letter_of(pattern, mark):
    text = re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]', '', bytes(s.raw[mark:]).decode('latin1'))
    m = re.search(r'([a-zA-Z]) - [^.\r\n]*' + pattern, text)
    return m.group(1) if m else None


def wish(what):
    cmd('\x17')
    s.expect('For what do you wish', timeout=10)
    mark = len(s.raw)
    s.send(what + '\r')
    s.settle(.5)
    more()
    return letter_of('', mark)     # unidentified items show another name


def level_teleport(level):
    cmd('\x16')
    s.expect('To what level do you want to teleport', timeout=10)
    s.send(f'{level}\r')
    wait_for(lambda st: st['dlvl'] == level)


def wizard_teleport_to(symbol):
    """^T in wizard mode to the next map feature shown as `symbol`."""
    cmd('\x14')
    # carrying the Amulet: "You feel disoriented for a moment." then Override?
    for _ in range(10):
        if 'Where do you want to be teleported' in s.screen.text():
            break
        if '--More--' in s.screen.text():
            s.send(' ')
        elif 'Override?' in msg():
            s.send('y')
        s.settle(.4)
    s.expect('Where do you want to be teleported', timeout=10)
    s.send(symbol)
    s.settle(.3)
    s.send('.')
    s.settle(.5)
    more()


def look_here():
    cmd(':', settle=.5)
    text = s.screen.text()
    more()
    return text


def open_door():
    """Travel stops at a closed door next to the hero: open it, or kick it
    open if it is locked. Doors are never entered diagonally."""
    for d in 'hjkl':
        cmd('o')
        s.send(d)
        s.settle(.5)
        text = msg()
        more()
        if 'door opens' in text:
            return True
        if 'locked' in text:
            for _ in range(40):
                cmd('\x04')
                s.send(d)
                s.settle(.5)
                text = msg()
                more()
                if re.search(r'crashes open|shatters|broken|no door', text):
                    return True
    return False


STEPS = {(-1, -1): 'y', (-1, 0): 'k', (-1, 1): 'u', (0, -1): 'h',
         (0, 1): 'l', (1, -1): 'b', (1, 0): 'j', (1, 1): 'n'}


def hero():
    for r in range(1, 22):
        c = s.screen.line(r).find('@')
        if c >= 0:
            return r, c
    return None


def fight_blocker():
    """Travel does not attack: fight a monster standing next to the hero."""
    pos = hero()
    if not pos:
        return False
    for (dr, dc), key in STEPS.items():
        r, c = pos[0] + dr, pos[1] + dc
        if 1 <= r < 22 and 0 <= c < 80 and s.screen.line(r)[c].isalpha():
            for _ in range(20):
                cmd('F' + key, settle=.5)
                more()
                if not s.screen.line(r)[c].isalpha():
                    break
            return True
    return False


def travel_to(symbol, here_text, tries=20):
    """Ordinary travel command to a staircase; repeats if interrupted."""
    for _ in range(tries):
        if here_text in look_here():
            return True
        start = hero()
        cmd('_')
        s.expect('Where do you want to travel to', timeout=10)
        s.send(symbol)
        s.settle(.3)
        s.send('.')
        s.settle(1, limit=120)
        if 'door is closed' in s.screen.text():
            more()
            open_door()
        more()
        if hero() == start:
            fight_blocker()
    return here_text in look_here()


def save_game(name):
    """S: save, then image and fsck the volume (the board is left in BOOTSEL)."""
    more()
    since = len(s.raw)
    cmd('S')
    if 'Really save?' in msg():
        s.send('y')
    free = wait_exit(name, since)
    collect()
    to_bootsel()
    info = image_volume(name)
    info['exit_free'] = free
    check(f'{name}: checkpoint committed', info['companion'] is not None,
          f"save/0game={info['companion']} B")
    return info


def restore(name, build, wizard_expected):
    flash(build)
    boot(name)
    raw = bytes(s.raw)
    if wizard_expected:
        check(f'{name}: restored', b'MEASURE restore_done' in raw or ready(),
              status()['line'])
    else:
        # unixmain.c wd_message(): wizard mode is refused, explore mode set
        check(f'{name}: restored on the ordinary firmware in explore mode',
              (b'may access debug (wizard) mode' in raw or b'explore' in raw)
              and ready(), status()['line'])
    collect()


def die_for_bones(level):
    boot(f'bones-{level}')
    level_teleport(level)
    st = status()
    check(f'bones: reached Dlvl {level}', st['dlvl'] == level, st['line'])
    wand = wish('wand of death')
    check('bones: wand of death wished', wand is not None, msg())
    more()
    since = len(s.raw)
    cmd('z')
    s.send(wand)
    s.settle(.3)
    s.expect('In what direction', timeout=10)
    s.send('.')
    return wait_exit(f'bones-{level}', since)


# --- the run ---------------------------------------------------------------

def stage(name):
    return STAGES.index(name) >= STAGES.index(a.from_stage)


if stage('bones'):
    to_bootsel()
    flash(a.diag, volume=True)
    dying = True
    for level in map(int, a.bones_levels.split(',')):
        die_for_bones(level)
    dying = False
    to_bootsel()
    info = image_volume('bones')
    results['bones_planted'] = info['bones']
    check('bones: several bones files on the volume', len(info['bones']) >= 4,
          json.dumps(info['bones']))

if stage('world'):
    if a.from_stage == 'world':
        to_bootsel()
        flash(a.diag)
        pico('load', '-v', a.out / 'bones.img', '-t', 'bin', '-o', a.fs_start)
    boot('world')
    ext('srampack', until='MEASURE pack_150_objects')
    ext('sramworld', until='MEASURE world_done', timeout=3600)
    ext('sramrevisit', until='MEASURE revisit_done', timeout=3600)

if stage('deep'):
    if a.from_stage == 'deep':
        connect('deep')            # the world game is still running on the board
        s.send(' ')                # a pending --More--, if any
        s.settle(.5)
        s.send('\x12')             # ^R redraws the screen model
        s.settle(.5)
        more()
    if 'Fort Ludios' in status()['line']:
        # level teleport is refused here; leave through the vault portal
        take_portal()
    if status()['dlvl'] != 2:
        level_teleport(2)
    st = status()
    check('world: back on Dlvl 2 of the Dungeons of Doom', st['dlvl'] == 2, st['line'])
    before = stats()
    results['deep_stats'] = before
    save_game('deep')

if stage('restore'):
    if a.from_stage == 'restore':
        to_bootsel()               # the deep checkpoint is on the board
        image_volume('deep')
    before = results['deep_stats']
    planted = {k.lower(): v for k, v in results['bones_planted'].items()}
    reclaimed = results['reclaims']
    deep = results['volumes']['deep']
    check('deep: checkpoint committed', deep['companion'] is not None,
          f"save/0game={deep['companion']} B")
    left = deep['bones']
    gone = set(planted) - set(left)
    check('world: every reclaim happened below the reserve',
          all(r['free_before'] < 48 * 1024 for r in reclaimed),
          json.dumps(reclaimed))
    bones_gone = [r for r in reclaimed if r['file'].startswith('bon')]
    logs_gone = [r for r in reclaimed if not r['file'].startswith('bon')]
    check('world: the reclaimed bones files are gone from the volume',
          len(gone) == len(bones_gone), f'{len(gone)} files gone, {len(bones_gone)} reclaims')
    check('world: score logs emptied only after every bones file',
          not logs_gone or (not left and reclaimed.index(logs_gone[0]) == len(bones_gone)),
          json.dumps([r['file'] for r in reclaimed]))
    check('world: emptied score logs stay on the volume, empty',
          all(deep['files'].get(r['file']) == 0 for r in logs_gone),
          ', '.join(r['file'] for r in logs_gone) or 'none needed')
    results['deep_without_reclaim'] = deep['free'] - sum(r['size'] for r in reclaimed)
    restore('deep-restore', a.diag, True)
    after = stats()
    check('deep: restore matches save', before == after, json.dumps([before, after]))

    save_game('deep-resave')

if stage('ordinary'):
    # ordinary firmware: restore the full dungeon and use the real stairs
    if a.from_stage == 'ordinary':
        to_bootsel()
        pico('load', '-v', a.out / 'deep-resave.img', '-t', 'bin', '-o', a.fs_start)
    restore('ordinary', a.ordinary, False)
    check('ordinary: on Dlvl 2', status()['dlvl'] == 2, status()['line'])
    check('ordinary: travel to the down staircase',
          travel_to('>', 'staircase down'), msg())
    check("ordinary: '>' reaches cached Dlvl 3", climb('>', 3), status()['line'])
    check('ordinary: travel to the up staircase', travel_to('<', 'staircase up'), msg())
    check("ordinary: '<' reaches Dlvl 2", climb('<', 2), status()['line'])
    check('ordinary: travel to the up staircase', travel_to('<', 'staircase up'), msg())
    check("ordinary: '<' reaches Dlvl 1", climb('<', 1), status()['line'])
    check('ordinary: travel to the level-1 up staircase',
          travel_to('<', 'staircase up'), msg())
    cmd('<')
    asked = 'no return' in msg()
    s.send('n')
    s.settle(.5)
    more()
    check("ordinary: '<' on level 1 without the Amulet asks and stays",
          asked and status()['dlvl'] == 1, status()['line'])
    save_game('ordinary-save')

if stage('endgame'):
    if a.from_stage == 'endgame':
        to_bootsel()
        pico('load', '-v', a.out / 'ordinary-save.img', '-t', 'bin', '-o', a.fs_start)
    restore('endgame', a.diag, True)
    # a wish could grant the imitation, which shares the description
    out = ext('sramamulet', until='AMULET carried=')
    m = re.search(rb'AMULET carried=(\d) letter=(\S)', out)
    check('endgame: the real Amulet of Yendor in the pack',
          m and m.group(1) == b'1', m.group(0).decode() if m else '')
    results['amulet_letter'] = m.group(2).decode()
    wizard_teleport_to('<')
    check('endgame: on the level-1 up staircase', 'staircase up' in look_here(), msg())
    save_game('amulet')

    # The climb out of level 1 is played on the ordinary firmware: in wizard
    # mode goto_level() skips its assignment of Earth as the destination.
    restore('climb', a.ordinary, False)
    check('climb: on the level-1 up staircase', 'staircase up' in look_here(), msg())
    cmd('<')
    s.expect('no return', timeout=10)
    s.send('y')
    wait_for(lambda st: st['endgame'], timeout=300)

if stage('earth'):
    if a.from_stage == 'earth':
        connect('earth')           # the ordinary game is running on Earth
        s.send('\x12')
        s.settle(.5)
        more()
    st = status()
    check("climb: ordinary '<' with the Amulet enters the Planes",
          st['plane'] == 'Earth', st['line'])
    save_game('earth-ordinary')
    restore('planes', a.diag, True)
    for plane in ('earth', 'air', 'fire', 'water'):
        before = stats()
        info = save_game(plane)
        info['stats'] = before
        restore(f'{plane}-restore', a.diag, True)
        after = stats()
        check(f'{plane}: restore matches save', before == after, json.dumps([before, after]))
        out = take_portal()
        st = status()
        check(f'{plane}: the magic portal leads on',
              b'PORTAL_TAKE' in out and st['endgame'] and st['plane'] != plane.title(),
              st['line'])
    st = status()
    check('endgame: on the Astral Plane', st['astral'], st['line'])
    ext('sramaltar', until='ASCENSION_ALTAR')
    check('endgame: standing on the co-aligned altar', 'altar' in look_here(), msg())
    results['astral_stats'] = stats()
    save_game('astral')

if stage('ascend'):
    if a.from_stage == 'ascend':
        to_bootsel()
        pico('load', '-v', a.out / 'astral.img', '-t', 'bin', '-o', a.fs_start)
    restore('ascend', a.ordinary, False)
    check('ascend: Astral Plane on the ordinary firmware', status()['astral'],
          status()['line'])
    check('ascend: on the altar', 'altar' in look_here(), msg())
    mark = len(s.raw)
    cmd('#offer\r')
    s.expect('What do you want to sacrifice', timeout=10)
    s.send(results['amulet_letter'])
    free = wait_exit('ascend', mark)
    raw = bytes(s.raw[mark:])
    (a.out / 'ascension-screen.txt').write_text(s.screen.text())
    check('ascend: the ordinary firmware ascends',
          b'You ascend to the status of' in raw or b'ascended' in raw,
          re.search(rb'You ascend[^\r\n]*', raw).group(0).decode() if b'You ascend' in raw else '')
    results['ascend_exit_free'] = free
    to_bootsel()
    info = image_volume('after-ascension')
    check('ascend: no checkpoint or cached level left',
          info['companion'] is None and info['level_files'] == 0,
          f"companion={info['companion']}, level files={info['level_files']}")

if stage('scores'):
    if a.from_stage == 'scores':
        to_bootsel()
    flash(a.ordinary)
    before = image_volume('before-scores')
    boot('scores', newname='scorer')
    more()
    since = len(s.raw)
    cmd('#quit\r')
    s.expect('Really quit', timeout=10)
    s.send('y')
    wait_exit('scores', since)
    to_bootsel()
    after = image_volume('after-scores')
    growth = {f: after['files'].get(f, 0) - before['files'].get(f, 0)
              for f in ('record', 'logfile', 'xlogfile')}
    results['score_growth_per_game'] = growth
    check('scores: the quit game is logged', growth['xlogfile'] > 0 and growth['logfile'] > 0,
          json.dumps(growth))
    pico('reboot')

m = results['measures']
results['summary'] = {
    'peak_live_heap': max(x.get('peak', 0) for x in m),
    'min_heap_growth_left': min(x.get('growth', 1 << 30) for x in m),
    'peak_stack': max(x.get('stack', 0) for x in m),
    'failed_allocations': max(x.get('failed', 0) for x in m),
    'guards_intact': all(x.get('guard', 1) == 1 for x in m),
    'min_volume_free': min(x.get('volfree', 1 << 30) for x in m),
    'files_reclaimed': [r['file'] for r in results['reclaims']],
    'bytes_reclaimed': sum(r['size'] for r in results['reclaims']),
}
save_results()
log('SUMMARY', json.dumps(results['summary']))
