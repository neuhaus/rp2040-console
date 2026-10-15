#!/usr/bin/env python3
"""Apply the experimental SRAM patch in a private console snapshot and build it."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
CONSOLE = HERE.parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir', type=Path, default=CONSOLE / 'build/nethack367-sram')
p.add_argument('--qemu-generators', action='store_true',
               help='Use ARM/newlib generators under QEMU (for ARM hosts such as Apple Silicon)')
args, recipe_args = p.parse_known_args()
work = args.build_dir.resolve()
if work == CONSOLE or work in CONSOLE.parents or work == HERE or HERE in work.parents:
    p.error('Choose a separate build directory')
marker = work / '.nethack367-sram-artifact'
if work.exists() and any(work.iterdir()) and not marker.exists():
    p.error('Nonempty build directory is not owned by this script')
work.mkdir(parents=True, exist_ok=True)
marker.write_text('NetHack 3.6.7 combined SRAM trial\n')
snapshot = work / 'console'
if snapshot.exists():
    if snapshot.is_symlink(): p.error("Snapshot path must not be a symlink")
    shutil.rmtree(snapshot)
snapshot.mkdir()
def run(cmd, **kwargs):
    print('+', shlex.join(map(str, cmd)), flush=True)
    return subprocess.run(list(map(str, cmd)), check=True, **kwargs)
# Archive committed sources only; the user's checkout is never patched.
archive = work / 'console.tar'
with archive.open('wb') as f:
    run(['git', '-C', CONSOLE, 'archive', 'HEAD'], stdout=f)
run(['tar', '-xf', archive, '-C', snapshot])
archive.unlink()
patch = HERE / 'combined-sram.patch'
run(['git', 'apply', '--whitespace=nowarn', '--check', patch], cwd=snapshot)
run(['git', 'apply', '--whitespace=nowarn', patch], cwd=snapshot)
env = os.environ.copy()
if args.qemu_generators:
    env['HOST_CC'] = shlex.join([sys.executable, str(HERE / 'host32_cc.py')])
    env['HOST_CFLAGS'] = ''
    env['HOST_RUNNER'] = shlex.join([sys.executable, str(HERE / 'host32_run.py')])
    env['HOST_NM'] = 'arm-none-eabi-nm'
run([sys.executable, snapshot / 'applications/nethack-3.6.7/build.py',
     '--build-dir', work / 'game', *recipe_args], env=env)
