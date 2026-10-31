#!/usr/bin/env python3
"""Fetch a Frotz checkout and build the RP2040 firmware and data volume.

The upstream sources are cloned into the build directory and left untouched;
nothing is patched, so the tree the recipe builds is the tree it fetched.
"""
import argparse
from pathlib import Path
import shlex
import subprocess

HERE = Path(__file__).resolve().parent
CONSOLE = HERE.parent.parent

# "For now": the fork that carries the dumb-interface fixes before they land
# upstream.  Three independent branches, each based on master, are offered as
# merge requests:
#
#   dumb-fix-interior-free  the free() of a stack pointer
#   core-read-timeout       the @read timeout operand default
#   dumb-no-float           the floating point removal
#
# dumb-fixes is a merge of the three, which is what the image is built from so
# that it has all of them.  When they land upstream, point these at
# DavidGriffith/frotz and a release tag or master.
REPO = 'https://gitlab.com/sn_/frotz.git'
BRANCH = 'dumb-fixes'


def run(command, **kwargs):
    print('+', shlex.join(map(str, command)), flush=True)
    return subprocess.run(list(map(str, command)), check=True, **kwargs)


def git(source, *arguments):
    """Run git in a checkout, returning stripped stdout or '' on failure."""
    result = subprocess.run(['git', '-C', str(source), *arguments],
                            capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else ''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path,
                        help='Existing Frotz checkout to build instead of cloning (left unchanged)')
    parser.add_argument('--repo', default=REPO, help='Git repository to clone')
    parser.add_argument('--branch', default=BRANCH, help='Branch to check out')
    parser.add_argument('--pull', action='store_true',
                        help='Fetch and hard-reset the managed checkout to the branch tip')
    parser.add_argument('--build-dir', type=Path, default=CONSOLE / 'build/frotz')
    parser.add_argument('--story', type=Path,
                        help='Z-code story file to install on the volume')
    parser.add_argument('--extra-args', default='',
                        help='Extra frotz arguments, quoted, e.g. "-u 4 -f ansi"')
    parser.add_argument('--heap-end', help='Top of the heap (default 0x2003c000)')
    parser.add_argument('--stack-reserve',
                        help='Minimum space above the heap, in bytes (default 24576)')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')

    work = args.build_dir.resolve()
    # Never unpack upstream sources over an unrelated/user-maintained directory.
    marker = work / '.rp2040-frotz-build'
    if work.exists() and any(work.iterdir()) and not marker.exists():
        parser.error('Build directory is nonempty and is not owned by this recipe')
    work.mkdir(parents=True, exist_ok=True)

    if args.source:
        src = args.source.resolve()
    else:
        src = work / 'upstream'
        if not src.exists():
            run(['git', 'clone', args.repo, src])
        if args.pull:
            run(['git', '-C', src, 'fetch', 'origin', args.branch])
            run(['git', '-C', src, 'checkout', args.branch])
            run(['git', '-C', src, 'reset', '--hard', 'origin/' + args.branch])
        else:
            run(['git', '-C', src, 'checkout', args.branch])

    if not (src / 'src' / 'common').is_dir():
        parser.error('%s does not look like a Frotz checkout' % src)

    marker.write_text('repo %s\nbranch %s\ncommit %s\n'
                      % (args.repo, args.branch,
                         git(src, 'rev-parse', 'HEAD') or 'unknown'))

    cmake = ['cmake', '-S', HERE, '-B', work, '-G', 'Ninja',
             '-DPICO_BOARD=pico', '-DFROTZ_DIR=' + str(src)]
    if args.story:
        cmake.append('-DFROTZ_STORY=' + str(args.story.resolve()))
    if args.extra_args:
        cmake.append('-DFROTZ_EXTRA_ARGS=' + args.extra_args)
    if args.heap_end:
        cmake.append('-DFROTZ_HEAP_END=' + args.heap_end)
    if args.stack_reserve:
        cmake.append('-DFROTZ_STACK_RESERVE=' + args.stack_reserve)

    run(cmake)
    run(['cmake', '--build', work, '--parallel', args.jobs])
    run(['cmake', '--build', work, '--target', 'volume'])
    print('Firmware:', work / 'frotz.uf2')
    print('Data volume:', work / 'frotz-volume.uf2')


if __name__ == '__main__':
    main()
