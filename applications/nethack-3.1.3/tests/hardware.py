#!/usr/bin/env python3
"""Repeatable, destructive opt-in RP2040 ascension validation.

Flash the diagnostic firmware and fresh matching volume before `tour`.
Stages: tour -> checkpoint -> revisit -> planes -> (interactive #offer).
The script controls the sole USB serial device and resets it for checkpoints.
"""
import argparse
import glob
import json
import os
from pathlib import Path
import re
import subprocess
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('stage', choices=['tour', 'checkpoint', 'revisit', 'planes', 'verify'])
p.add_argument('--name', help='Existing checkpoint name for verify')
p.add_argument('--start-plane', choices=['earth', 'air', 'fire', 'water', 'astral'], default='earth')
p.add_argument('--picotool', default=os.environ.get('PICOTOOL', 'picotool'))
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
serial = Path(__file__).with_name('serial.py')


def capture(name, send='', until=None, seconds=1800, **flags):
    log = a.out / (name + '.log')
    cmd = ['python3', str(serial), '--log', str(log), '--seconds', str(seconds)]
    if send:
        cmd += ['--send', send]
    if until:
        cmd += ['--until', until]
    cmd += ['--' + flag for flag, enabled in flags.items() if enabled]
    subprocess.run(cmd, check=True)
    return log.read_bytes()


def pico(*args):
    command = [a.picotool, *map(str, args)]
    try:
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL, timeout=90)
    except subprocess.TimeoutExpired:
        if args != ('reboot', '-u', '-f'):
            raise
        subprocess.run([a.picotool, 'info'], check=True, timeout=10)
    print('PICO', args[0], flush=True)
    if args == ('reboot', '-u', '-f'):
        time.sleep(1)


def state(name):
    data = capture(name, '#sramstat\\n', 'MEASURE stats')
    match = re.search(rb'STATE ([^\r\n]+)', data)
    if not match:
        raise RuntimeError('No state: ' + name)
    return {k.decode(): int(v) for k, v in re.findall(rb'(\w+)=(\d+)', match[1])}


def finish_checkpoint(name, before):
    listing = (a.out / (name + '-files.log')).read_text()
    after = state(name + '-after')
    verified = capture(name + '-verified', '#sramv\\n', 'MEASURE verify_done')
    matched = re.search(rb'VERIFIED_LEVELS (\d+)', verified)
    if not matched or int(matched[1]) != after['cached']:
        raise RuntimeError('Cached level verification failed: ' + name)
    if before != after:
        raise RuntimeError(f'State mismatch {name}: {before} != {after}')
    result_path = a.out / 'checkpoints.json'
    results = json.loads(result_path.read_text()) if result_path.exists() else {}
    results[name] = {'before': before, 'after': after, 'fat_clean': True, 'verified_levels': int(matched[1]),
                     'free_bytes': int(re.search(r'([\d ]+) bytes free', listing)[1].replace(' ', ''))}
    result_path.write_text(json.dumps(results, indent=2) + '\n')
    print('CHECKPOINT PASSED', name, before, flush=True)


def checkpoint(name):
    before = state(name + '-before')
    capture(name + '-save', until='Press any key for a new game', save=True)
    pico('reboot', '-u', '-f')
    image = a.out / (name + '.img')
    pico('save', '-r', '0x10100000', '0x10200000', image, '-t', 'bin')
    fsck = subprocess.run(['fsck.fat', '-n', str(image)], text=True, capture_output=True)
    (a.out / (name + '-fat.log')).write_text(fsck.stdout + fsck.stderr)
    if fsck.returncode:
        raise RuntimeError('Saved FAT failed verification: ' + name)
    listing = subprocess.check_output(['mdir', '-i', str(image), '-s', '::'], text=True)
    (a.out / (name + '-files.log')).write_text(listing)
    pico('reboot')
    deadline = time.monotonic() + 15
    while not (glob.glob('/dev/cu.usbmodem*') or glob.glob('/dev/ttyACM*')):
        if time.monotonic() > deadline:
            raise RuntimeError('Serial did not reappear')
        time.sleep(.1)
    capture(name + '-restore', until='MEASURE restore_done', resume=True)
    finish_checkpoint(name, before)



if a.stage == 'tour':
    capture('pack', '#sramp\\n', 'MEASURE pack_done')
    capture('world', '#sramw\\n', 'MEASURE world_done')
elif a.stage == 'checkpoint':
    checkpoint('world')
elif a.stage == 'revisit':
    capture('revisit', '#sramr\\n', 'MEASURE revisit_done')
    checkpoint('mapped')
elif a.stage == 'planes':
    planes = ['earth', 'air', 'fire', 'water', 'astral']
    for plane in planes[planes.index(a.start_plane):]:
        capture(plane + '-enter', '#sramn\\n', 'MEASURE plane_done')
        if plane == 'astral':
            capture('altar', '#srama\\n', 'MEASURE altar_done')
        checkpoint(plane)

elif a.stage == 'verify':
    if not a.name:
        p.error('verify requires --name')
    data = (a.out / (a.name + '-before.log')).read_bytes()
    match = re.search(rb'STATE ([^\r\n]+)', data)
    before = {k.decode(): int(v) for k, v in re.findall(rb'(\w+)=(\d+)', match[1])}
    finish_checkpoint(a.name, before)
