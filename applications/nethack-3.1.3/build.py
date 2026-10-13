#!/usr/bin/env python3
"""Build the pinned NetHack release in a private generated working directory."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
CONSOLE = HERE.parent.parent
COMMIT = 'df89f069705251b66c74322ed01b3d30d670979e'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, help='Existing Vanilla-NetHack Git checkout (left unchanged)')
    parser.add_argument('--build-dir', type=Path, default=CONSOLE / 'build/nethack-3.1.3')
    parser.add_argument('--soft-reboot', action='store_true')
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
        return subprocess.run(list(map(str, command)), cwd=cwd, check=True, **kwargs)

    if args.source is None and not source.exists():
        run(['git', 'clone', 'https://github.com/Vanilla-NetHack/NetHack.git', source], cwd=work)
    run(['git', '-C', source, 'cat-file', '-e', COMMIT + '^{commit}'])
    archive = work / 'upstream.tar'
    with archive.open('wb') as output:
        run(['git', '-C', source, 'archive', COMMIT], stdout=output)
    run(['tar', '-xf', archive, '-C', src])
    archive.unlink()
    run(['patch', '-p1', '-i', HERE / 'port/config.patch'])
    for item in (HERE / 'port').iterdir():
        if item.name != 'config.patch':
            shutil.copy2(item, src / item.name)

    # Compiler and runner are argument lists, never shell-evaluated commands.
    cc = shlex.split(os.environ.get('HOST_CC', 'gcc'))
    flags = shlex.split(os.environ.get('HOST_CFLAGS', '-m32'))
    runner = shlex.split(os.environ.get('HOST_RUNNER', ''))
    env = os.environ.copy()
    if os.environ.get('HOST_LD_LIBRARY_PATH'):
        env['LD_LIBRARY_PATH'] = os.environ['HOST_LD_LIBRARY_PATH']

    def compile_host(inputs, output, cwd=src / 'util'):
        run(cc + flags + ['-std=gnu89', '-w', '-include', src / 'compat.h',
                         '-I', src / 'include'] + inputs + ['-o', output], cwd=cwd)

    def execute(tool, *options, cwd=src / 'util'):
        run(runner + [src / 'util' / tool, *options], cwd=cwd, env=env)

    for prefix in ('lev', 'dgn'):
        run(['bison', '-y', '-d', '-o', prefix + '_yacc.c', prefix + '_comp.y'], cwd=src / 'util')
        shutil.copy2(src / 'util' / (prefix + '_yacc.h'), src / 'include' / (prefix + '_comp.h'))
        run(['flex', '-o', prefix + '_lex.c', prefix + '_comp.l'], cwd=src / 'util')
    compile_host(['makedefs.c', '../src/monst.c', '../src/objects.c'], 'makedefs32')
    for flag in 'opmv':
        execute('makedefs32', '-' + flag)
    compile_host(['lev_yacc.c', 'lev_lex.c', 'lev_main.c', 'panic.c', '../src/alloc.c',
                  '../src/drawing.c', '../src/decl.c', '../src/monst.c', '../src/objects.c'], 'lev_comp32')
    compile_host(['dgn_yacc.c', 'dgn_lex.c', 'dgn_main.c', 'panic.c', '../src/alloc.c'], 'dgn_comp32')
    for flag in 'drqhe':
        execute('makedefs32', '-' + flag)
    for description in sorted((src / 'dat').glob('*.des')):
        execute('lev_comp32', description.name, cwd=src / 'dat')
    execute('dgn_comp32', 'dungeon.pdf', cwd=src / 'dat')
    compile_host(['-c', 'layout.c'], 'layout-native.o', cwd=src)
    run(['arm-none-eabi-gcc', '-mcpu=cortex-m0plus', '-mthumb', '-include', 'compat.h',
         '-Iinclude', '-c', 'layout.c', '-o', 'layout-arm.o'])
    run(['python3', CONSOLE / 'tools/check-layout.py', '--host-object', 'layout-native.o',
         '--target-object', 'layout-arm.o', '--target-nm', 'arm-none-eabi-nm'])
    firmware = work / ('soft' if args.soft_reboot else 'hardware')
    run(['cmake', '-S', src, '-B', firmware, '-G', 'Ninja', '-DPICO_BOARD=pico',
         '-DRP2040_CONSOLE_DIR=' + str(CONSOLE),
         '-DNETHACK_SOFT_REBOOT=' + ('ON' if args.soft_reboot else 'OFF')])
    run(['cmake', '--build', firmware, '--parallel', args.jobs])
    run(['cmake', '--build', firmware, '--target', 'volume'])
    run(['python3', HERE / 'verify-images.py', '--build-dir', firmware] +
        (['--soft-reboot'] if args.soft_reboot else []))
    print('Firmware:', firmware / 'nethack.uf2')
    print('Data volume:', firmware / 'nethack-volume.uf2')


if __name__ == '__main__':
    main()
