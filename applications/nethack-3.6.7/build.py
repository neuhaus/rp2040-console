#!/usr/bin/env python3
"""Build the pinned NetHack 3.6.7 release in a private generated directory."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
CONSOLE = HERE.parent.parent
# tag NetHack-3.6.7_Released
COMMIT = 'ed600d9f0f3c37677418f0150f59363ca641f3dc'
REPO = 'https://github.com/NetHack/NetHack.git'

# Files NetHack reads through the data librarian (dlb_*), in the order they
# are packed.  `symbols` is not here: NetHack opens it through
# fopen_datafile(), i.e. plain stdio, so it ships uncompressed.  `tribute`
# is not here either: its compressed form is the difference between the
# volume fitting and not, and it is the least of what the data library
# carries.
DATA_LIBRARY = (
    'bogusmon', 'cmdhelp', 'data', 'dungeon', 'engrave', 'epitaph', 'help',
    'hh', 'history', 'keyhelp', 'license', 'opthelp', 'options', 'oracles',
    'quest.dat', 'rumors', 'wizhelp',
)


def window_bits(path):
    """Window size to compress one data file with.

    The level files are tiny, and a long window costs distance bits without
    ever finding a longer match.  The big text files do benefit.
    """
    return 10 if path.stat().st_size < 8192 else 12


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path,
                        help='Existing NetHack Git checkout (left unchanged)')
    parser.add_argument('--build-dir', type=Path,
                        default=CONSOLE / 'build/nethack-3.6.7')
    parser.add_argument('--soft-reboot', action='store_true')
    parser.add_argument('--nethack-options', default='', metavar='OPTS',
                        help='NETHACKOPTIONS for this image, e.g. '
                             '"symset:DECgraphics,number_pad:-1"')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    work = args.build_dir.resolve()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    # Never unpack upstream sources over an unrelated/user-maintained directory.
    marker = work / '.rp2040-nethack-build'
    if work.exists() and any(work.iterdir()) and not marker.exists():
        parser.error('Build directory is nonempty and is not owned by this recipe')
    work.mkdir(parents=True, exist_ok=True)
    marker.write_text(COMMIT + '\n')
    source = args.source.resolve() if args.source else work / 'upstream'
    src = work / 'source'
    src.mkdir(exist_ok=True)

    def run(command, cwd=src, **kwargs):
        print('+', shlex.join(map(str, command)), flush=True)
        return subprocess.run(list(map(str, command)), cwd=cwd, check=True,
                              **kwargs)

    if args.source is None and not source.exists():
        run(['git', 'clone', REPO, source], cwd=work)
    run(['git', '-C', source, 'cat-file', '-e', COMMIT + '^{commit}'])
    archive = work / 'upstream.tar'
    with archive.open('wb') as output:
        run(['git', '-C', source, 'archive', COMMIT], stdout=output)
    run(['tar', '-xf', archive, '-C', src])
    archive.unlink()
    run(['patch', '-p1', '-i', HERE / 'port/config.patch'])

    # The port's own files.  nhdlb.c is the replacement data librarian and
    # has to replace NetHack's own src/dlb.c (the generated tree is rooted at
    # src/ above the upstream src/ directory, which is why this is src/src).
    # The Tamp library goes to src/tamp/ where both its consumers (src/src/dlb.c
    # and the two library objects the target compiles) can reach its headers.
    # shutil.copy (not copy2) so a rebuilt tree notices the change: copy2
    # preserves mtimes and ninja would not.
    port = HERE / 'port'
    for name in ('compat.h', 'platform.c', 'layout.c', 'CMakeLists.txt'):
        shutil.copy(port / name, src / name)
    shutil.copy(port / 'nhdlb.c', src / 'src' / 'dlb.c')
    if (src / 'tamp').exists():
        shutil.rmtree(src / 'tamp')
    shutil.copytree(port / 'tamp', src / 'tamp', copy_function=shutil.copy)
    # The whole point of the recipe is this replacement; fail loudly here
    # rather than shipping a binary that still looks for NetHack's nhdat.
    if 'NHDATA_FILE' not in (src / 'src' / 'dlb.c').read_text():
        parser.error('port/nhdlb.c did not replace src/src/dlb.c')

    # Compiler and runner are argument lists, never shell-evaluated commands.
    cc = shlex.split(os.environ.get('HOST_CC', 'gcc'))
    flags = shlex.split(os.environ.get('HOST_CFLAGS', '-m32'))
    runner = shlex.split(os.environ.get('HOST_RUNNER', ''))
    env = os.environ.copy()
    if os.environ.get('HOST_LD_LIBRARY_PATH'):
        env['LD_LIBRARY_PATH'] = os.environ['HOST_LD_LIBRARY_PATH']

    util = src / 'util'
    tamp = src / 'tamp'

    def compile_host(inputs, output, cwd=util):
        run(cc + flags + ['-std=gnu89', '-w', '-include', src / 'compat.h',
                          '-I', src / 'include'] + inputs + ['-o', output],
            cwd=cwd)

    # The Tamp tools are ordinary C and are not built with NetHack's
    # compatibility shim.  The packer needs the compressor; the verifier
    # needs the decompressor, which is the code the board will run.
    tamp_common = ['-I', tamp]
    pack_sources = [str(tamp / 'common.c'), str(tamp / 'compressor.c')]
    run(cc + flags + tamp_common + ['-O2', '-DTAMP_STREAM=1',
                                    '-DTAMP_STREAM_STDIO=1',
                                    str(port / 'host' / 'tamp_pack.c')]
        + pack_sources + ['-o', work / 'tamp_pack'])
    run(cc + flags + tamp_common + ['-O2', '-DTAMP_STREAM=0',
                                    str(port / 'host' / 'tamp_check.c'),
                                    str(tamp / 'common.c'),
                                    str(tamp / 'decompressor.c'),
                                    '-o', work / 'tamp_check'])

    def execute(tool, *options, cwd=util):
        run(runner + [util / tool, *options], cwd=cwd, env=env)

    # yacc/lex: the generated parsers stay in util/ (lev_yacc.c includes
    # lev_yacc.h), but the headers go to include/ because lev_lex.c includes
    # "lev_comp.h".
    for prefix in ('lev', 'dgn'):
        run(['bison', '-y', '-d', '-o', prefix + '_yacc.c', prefix + '_comp.y'],
            cwd=util)
        shutil.copy2(util / (prefix + '_yacc.h'),
                     src / 'include' / (prefix + '_comp.h'))
        run(['flex', '-o', prefix + '_lex.c', prefix + '_comp.l'], cwd=util)

    # makedefs only needs monst.c/objects.c, which do not include date.h, so it
    # can be built before any generated header exists.  -o/-p/-z write into
    # include/ and src/ relative to util/.
    compile_host(['makedefs.c', '../src/monst.c', '../src/objects.c'],
                 'makedefs32')
    execute('makedefs32', '-o')
    execute('makedefs32', '-p')
    execute('makedefs32', '-z')
    # -v writes include/date.h and dat/options; run it from dat/ so both land
    # where they belong.
    execute('makedefs32', '-v', cwd=src / 'dat')

    compile_host(['lev_yacc.c', 'lev_lex.c', 'lev_main.c', 'panic.c',
                  '../src/alloc.c', '../src/drawing.c', '../src/decl.c',
                  '../src/monst.c', '../src/objects.c'], 'lev_comp32')
    compile_host(['dgn_yacc.c', 'dgn_lex.c', 'dgn_main.c', 'panic.c',
                  '../src/alloc.c'], 'dgn_comp32')

    execute('makedefs32', '-e', cwd=src / 'dat')
    execute('dgn_comp32', 'dungeon.pdf', cwd=src / 'dat')
    for flag in 'drqhs':
        execute('makedefs32', '-' + flag, cwd=src / 'dat')
    for description in sorted((src / 'dat').glob('*.des')):
        execute('lev_comp32', description.name, cwd=src / 'dat')

    # Collect every file the data librarian serves into one Tamp archive.
    dat = src / 'dat'
    library = list(DATA_LIBRARY) + [p.name for p in sorted(dat.glob('*.lev'))]
    pack_args = [work / 'tamp_pack', dat / 'nhdata']
    for name in library:
        path = dat / name
        if not path.exists():
            parser.error('missing data file: %s' % path)
        pack_args += [name, str(window_bits(path)), path]
    run(pack_args)
    check_args = [work / 'tamp_check', dat / 'nhdata']
    for name in library:
        check_args += [name, dat / name]
    run(check_args)

    # Confirm the host's byte layout matches the target's before shipping the
    # generated data files.
    compile_host(['-c', 'layout.c'], 'layout-native.o', cwd=src)
    run(['arm-none-eabi-gcc', '-mcpu=cortex-m0plus', '-mthumb',
         '-include', 'compat.h', '-Iinclude', '-c', 'layout.c',
         '-o', 'layout-arm.o'], cwd=src)
    run(['python3', CONSOLE / 'tools/check-layout.py',
         '--host-object', 'layout-native.o', '--target-object', 'layout-arm.o',
         '--target-nm', 'arm-none-eabi-nm'], cwd=src)

    firmware = work / ('soft' if args.soft_reboot else 'hardware')
    run(['cmake', '-S', src, '-B', firmware, '-G', 'Ninja', '-DPICO_BOARD=pico',
         '-DRP2040_CONSOLE_DIR=' + str(CONSOLE),
         '-DNETHACK_OPTIONS=' + args.nethack_options,
         '-DNETHACK_SOFT_REBOOT=' + ('ON' if args.soft_reboot else 'OFF')])
    run(['cmake', '--build', firmware, '--parallel', args.jobs])
    run(['cmake', '--build', firmware, '--target', 'volume'])
    run(['python3', HERE / 'verify-images.py', '--build-dir', firmware]
        + (['--soft-reboot'] if args.soft_reboot else []))
    print('Firmware:', firmware / 'nethack.uf2')
    print('Data volume:', firmware / 'nethack-volume.uf2')


if __name__ == '__main__':
    main()
