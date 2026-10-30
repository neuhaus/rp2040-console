#!/usr/bin/env python3
"""Bounded USB serial capture; logs raw bytes and services NetHack More prompts."""
import argparse
import glob
import fcntl
import os
from pathlib import Path
import re
import select
import termios
import time
import tempfile
import tty

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--send', default='')
p.add_argument('--until')
p.add_argument('--seconds', type=float, default=10)
p.add_argument('--log', type=Path, required=True)
p.add_argument('--save', action='store_true')
p.add_argument('--resume', action='store_true')
p.add_argument('--finish', action='store_true', help='Decline the optional end-of-game disclosure screens')
a = p.parse_args()
ports = glob.glob('/dev/cu.usbmodem*') + glob.glob('/dev/ttyACM*')
if len(ports) != 1:
    raise SystemExit(f'Expected exactly one board serial port: {ports}')
lock = open(Path(tempfile.gettempdir()) / 'rp2040-nethack-serial.lock', 'w')
fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
fd = os.open(ports[0], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
tty.setraw(fd)
attrs = termios.tcgetattr(fd)
attrs[4] = attrs[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, attrs)
out = bytearray()
carry = b''
sent = not bool(a.send or a.save)
confirmed = False
kept = False
disclosed = set()
end = time.monotonic() + a.seconds
start = time.monotonic()
reached = False
with a.log.open('wb', buffering=0) as log:
    while time.monotonic() < end:
        if not sent and time.monotonic() - start > .4:
            os.write(fd, b'\x1bS' if a.save else a.send.encode().decode('unicode_escape').encode('latin1'))
            sent = True
        if not select.select([fd], [], [], .1)[0]:
            continue
        try:
            b = os.read(fd, 65536)
        except OSError:
            break
        if not b:
            break
        log.write(b)
        out.extend(b)
        carry += b
        if b'--More--' in carry:
            os.write(fd, b' ')
            carry = carry.rsplit(b'--More--', 1)[1]
        if a.save and not confirmed and b'Really save?' in out:
            os.write(fd, b'y')
            confirmed = True
        if a.resume and not kept and b'Do you want to keep the save file?' in out:
            os.write(fd, b'n')
            kept = True
        if a.finish:
            for prompt in (b'Do you want your possessions identified?', b'Do you want to see your attributes?', b'Do you want an account of foes vanquished?', b'Do you want a list of species genocided?'):
                if prompt in out and prompt not in disclosed:
                    os.write(fd, b'n')
                    disclosed.add(prompt)
        carry = carry[-100:]
        if any(s in out for s in (b'Oops...', b'*** PANIC ***', b'This game is void', b'the dungeon collapses', b'Saved level is out of date', b'incompatible storage format')):
            break
        if a.until and a.until.encode() in out and not reached:
            reached = True
            end = min(end, time.monotonic() + 1)
os.close(fd)
text = re.sub(rb'\x1b\[[0-?]*[ -/]*[@-~]|\x1b[()][0-9A-Za-z]|\x1b[=>78]', b'', out).decode('utf8', 'replace')
print(text[-10000:])
if a.until and not reached:
    raise SystemExit('Expected target not reached: ' + a.until)
if any(s in out for s in (b'Oops...', b'*** PANIC ***', b'This game is void', b'the dungeon collapses', b'Saved level is out of date', b'incompatible storage format')):
    raise SystemExit('Game failure in serial output')
