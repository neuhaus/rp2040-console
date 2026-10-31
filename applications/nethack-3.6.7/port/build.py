#!/usr/bin/env python3
"""Build the pinned NetHack 3.6.7 release in a private generated directory."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import zlib

HERE = Path(__file__).resolve().parent.parent
CONSOLE = HERE.parent.parent
# tag NetHack-3.6.7_Released
COMMIT = 'ed600d9f0f3c37677418f0150f59363ca641f3dc'
REPO = 'https://github.com/NetHack/NetHack.git'

# The line printed before the game starts, baked into the image as the BANNER
# environment variable.  --banner or $BANNER overrides it.
DEFAULT_BANNER = 'RP2040 Console ready.'

# Files NetHack reads through the data librarian (dlb_*), in the order they
# are packed.  `symbols` is here even though NetHack opens it with plain
# stdio: the port backs that FILE* with the librarian (see port/nhdlb.c
# nhdlb_memopen), so it compresses like the rest.  `tribute` is not packed:
# it is the least of what the library carries and would not fit.
DATA_LIBRARY = (
    'bogusmon', 'cmdhelp', 'data', 'dungeon', 'engrave', 'epitaph', 'help',
    'hh', 'history', 'keyhelp', 'license', 'opthelp', 'options', 'oracles',
    'quest.dat', 'rumors', 'symbols', 'wizhelp',
)


# The data library's raw-deflate windows, in bits.  the build probes these per
# entry and records the winner in the directory; the board's inflater allocates
# a 1<<wbits dictionary (port/nhdlb.c).  Keep the two in step.
NH_WINDOW_MIN = 10
NH_WINDOW_MAX = 12
NH_NAME_MAX = 16


def deflate(data, wbits):
    co = zlib.compressobj(9, zlib.DEFLATED, -wbits)
    return co.compress(data) + co.flush()


def build_data_compressors(work, port, run):
    """Build native host tools; their output needs only the existing inflater."""
    cc = shlex.split(os.environ.get('BUILD_CC', 'cc'))
    vendor = port / 'host' / 'zopfli'
    sources = sorted(vendor.glob('*.c'))
    tools = {}
    for bits in range(NH_WINDOW_MIN, NH_WINDOW_MAX + 1):
        exe = work / ('deflate-opt-%d' % bits)
        run(cc + ['-O2', '-DZOPFLI_WINDOW_SIZE=' + str(1 << bits),
                  '-I' + str(vendor), port / 'host' / 'deflate_opt.c']
            + sources + ['-lm', '-o', exe])
        tools[bits] = exe
    return tools


def pack_nhdata(dat, library, tools):
    """Write dat/nhdata: raw-deflate streams for `library`, in one archive.

    The format matches port/nhdlb.c: a 12-byte header, a 32-byte directory
    entry per file, then the concatenated deflate streams.  Each entry takes the smallest compressed output among windows of at
    most 4 KiB, bounding decoder SRAM even for large text files.
    """
    blob = bytearray()
    directory = bytearray()
    data_start = 12 + 32 * len(library)
    raw = {}
    for name in library:
        path = dat / name
        if not path.exists():
            raise SystemExit('missing data file: %s' % path)
        data = path.read_bytes()
        raw[name] = data
        # Smallest output wins; ties go to the smaller window (less RAM).
        candidates = []
        for w in range(NH_WINDOW_MIN, NH_WINDOW_MAX + 1):
            candidates.append((deflate(data, w), w))
            optimized = subprocess.run([str(tools[w])], input=data,
                                       capture_output=True, check=True).stdout
            candidates.append((optimized, w))
        comp, wbits = min(candidates, key=lambda cw: (len(cw[0]), cw[1]))
        nm = name.encode()[: NH_NAME_MAX - 1]
        directory += nm + b'\0' * (NH_NAME_MAX - len(nm))
        directory += struct.pack('<IIII', data_start + len(blob), len(comp),
                                 len(data), wbits)
        blob += comp
    archive = (b'NHDA' + struct.pack('<II', 1, len(library))
               + bytes(directory) + bytes(blob))
    (dat / 'nhdata').write_bytes(archive)

    # Round-trip every entry with its own window; the board reads this format.
    for i, name in enumerate(library):
        off, clen, ulen, wbits = struct.unpack_from(
            '<IIII', archive, 12 + 32 * i + NH_NAME_MAX)
        dec = zlib.decompressobj(-wbits)
        got = dec.decompress(archive[off:off + clen]) + dec.flush()
        if len(got) != ulen or got != raw[name]:
            raise SystemExit('nhdata round-trip failed for %s' % name)


def main():
    parser = argparse.ArgumentParser(prog='build.sh', description=__doc__)
    parser.add_argument('--source', type=Path,
                        help='Existing NetHack Git checkout (left unchanged)')
    parser.add_argument('--build-dir', type=Path,
                        default=CONSOLE / 'build/nethack-3.6.7')
    parser.add_argument('--no-lto', action='store_true',
                        help='Disable selective LTO; use the smaller 520 KiB volume')
    parser.add_argument('--soft-reboot', action='store_true')
    parser.add_argument('--sram-diagnostics', action='store_true',
                        help='Deterministic wizard probes; not for ordinary play')
    parser.add_argument('--sram-max-dungeon', action='store_true',
                        help='Largest valid branch lengths; requires --sram-diagnostics')
    parser.add_argument('--nethack-options', default='', metavar='OPTS',
                        help='NETHACKOPTIONS for this image, e.g. '
                             '"symset:DECgraphics,number_pad:-1"')
    parser.add_argument('--banner', metavar='TEXT',
                        help='startup banner line (default: $BANNER, else the '
                             'line the recipe ships)')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.sram_max_dungeon and not args.sram_diagnostics:
        parser.error('--sram-max-dungeon requires --sram-diagnostics')
    if args.sram_diagnostics:
        args.nethack_options = "name:wizard,role:Wizard,race:human,gender:male,align:neutral,playmode:debug,!autopickup" + ("," + args.nethack_options if args.nethack_options else "")
    banner = (args.banner if args.banner is not None
              else os.environ.get('BANNER', DEFAULT_BANNER))
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
    if source == src or src in source.parents:
        parser.error('Upstream checkout must be outside the generated source tree')
    if src.is_symlink():
        parser.error('Generated source path must not be a symlink')
    if src.exists():
        shutil.rmtree(src)
    src.mkdir()

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
    # The Tamp and uzlib libraries go to src/tamp/ and src/uzlib/ where their
    # consumers (src/src/dlb.c, save.c/restore.c, and the target's library
    # objects) can reach their headers.
    # shutil.copy (not copy2) so a rebuilt tree notices the change: copy2
    # preserves mtimes and ninja would not.
    port = HERE / 'port'
    for name in ('compat.h', 'layout.c', 'memlog.c',
                 'CMakeLists.txt'):
        shutil.copy(port / name, src / name)
    shutil.copy(port / 'nhdlb.c', src / 'src' / 'dlb.c')
    for lib in ('tamp', 'uzlib'):
        if (src / lib).exists():
            shutil.rmtree(src / lib)
        shutil.copytree(port / lib, src / lib, copy_function=shutil.copy)
    # The whole point of the recipe is this replacement; fail loudly here
    # rather than shipping a binary that still looks for NetHack's nhdat.
    if 'NHDATA_FILE' not in (src / 'src' / 'dlb.c').read_text():
        parser.error('port/nhdlb.c did not replace src/src/dlb.c')

    if args.sram_diagnostics:
        import importlib.util
        spec = importlib.util.spec_from_file_location('sram_diagnostics', port / 'sram_diagnostics.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.prepare(src, port, maximum=args.sram_max_dungeon)

    # Compiler and runner are argument lists, never shell-evaluated commands.
    cc = shlex.split(os.environ.get('HOST_CC', 'gcc'))
    flags = shlex.split(os.environ.get('HOST_CFLAGS', '-m32'))
    runner = shlex.split(os.environ.get('HOST_RUNNER', ''))
    env = os.environ.copy()
    if os.environ.get('HOST_LD_LIBRARY_PATH'):
        env['LD_LIBRARY_PATH'] = os.environ['HOST_LD_LIBRARY_PATH']

    util = src / 'util'

    def compile_host(inputs, output, cwd=util):
        run(cc + flags + ['-std=gnu89', '-w', '-include', src / 'compat.h',
                          '-I', src / 'include'] + inputs + ['-o', output],
            cwd=cwd)

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

    # Collect every file the data librarian serves into one deflate archive.
    dat = src / 'dat'
    library = list(DATA_LIBRARY) + [p.name for p in sorted(dat.glob('*.lev'))]
    pack_nhdata(dat, library, build_data_compressors(work, port, run))

    # Decode the archive with the same inflater the board runs (uzlib), not
    # just the host's zlib, so a decoder mismatch is caught here.  uzlib's
    # unused checksum wrapper pulls in CRC/Adler, which --gc-sections drops.
    run(cc + flags + ['-I', src / 'uzlib', '-O2',
                      '-ffunction-sections', '-fdata-sections',
                      '-Wl,--gc-sections',
                      str(port / 'host' / 'nhdata_check.c'),
                      str(src / 'uzlib' / 'tinflate.c'),
                      '-o', work / 'nhdata_check'])
    run(runner + [str(work / 'nhdata_check'), str(dat / 'nhdata'), str(dat)])

    # Confirm the host's byte layout matches the target's before shipping the
    # generated data files.
    compile_host(['-c', 'layout.c'], 'layout-native.o', cwd=src)
    run(['arm-none-eabi-gcc', '-mcpu=cortex-m0plus', '-mthumb',
         '-include', 'compat.h', '-Iinclude', '-c', 'layout.c',
         '-o', 'layout-arm.o'], cwd=src)
    run(['python3', CONSOLE / 'tools/check-layout.py',
         '--host-object', 'layout-native.o', '--target-object', 'layout-arm.o',
         '--host-nm', os.environ.get('HOST_NM', 'nm'),
         '--target-nm', 'arm-none-eabi-nm'], cwd=src)

    firmware = work / ('soft' if args.soft_reboot else 'hardware')
    run(['cmake', '-S', src, '-B', firmware, '-G', 'Ninja', '-DPICO_BOARD=pico',
         '-DRP2040_CONSOLE_DIR=' + str(CONSOLE),
         '-DNETHACK_OPTIONS=' + args.nethack_options,
         '-DNETHACK_BANNER=' + banner,
         '-DNETHACK_LTO=' + ('OFF' if args.no_lto else 'ON'),
         '-DNETHACK_LTO_JOBS=' + str(args.jobs),
         '-DNETHACK_SOFT_REBOOT=' + ('ON' if args.soft_reboot else 'OFF')])
    run(['cmake', '--build', firmware, '--parallel', args.jobs])
    run(['cmake', '--build', firmware, '--target', 'volume'])
    run(['python3', HERE / 'verify-images.py', '--build-dir', firmware]
        + (['--soft-reboot'] if args.soft_reboot else []))
    print('Firmware:', firmware / 'nethack.uf2')
    print('Data volume:', firmware / 'nethack-volume.uf2')


if __name__ == '__main__':
    main()
