#!/usr/bin/env python3
"""Regression-test the port's exact save/restore adapters in a native harness.

All generated sources and executables live in a temporary directory. Requires
Python 3, git, patch, and a native C compiler. This is a codec integration probe,
not an ARM firmware or whole-game test.
"""
import argparse
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile

CONSOLE = Path(__file__).resolve().parents[1]
PORT = CONSOLE / 'applications/nethack-3.6.7/port'

PICO_IO = r'''
#include <pico/stdlib.h>
#include <pico/stdio_usb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
/* In-memory descriptor fixture: exercises the actual adapters without
 * mounting or writing the game's flash filesystem. */
static unsigned char fixture[24000];
static size_t fixture_pos, fixture_len;
static int fixture_mkstemp(char *path) {
    (void)path; fixture_pos = fixture_len = 0; return 3;
}
static int fixture_dup(int fd) { return fd; }
static int fixture_unlink(const char *path) { (void)path; return 0; }
static int fixture_close(int fd) { (void)fd; return 0; }
static ssize_t fixture_read(int fd, void *buf, size_t n) {
    (void)fd;
    if (n > fixture_len - fixture_pos) n = fixture_len - fixture_pos;
    memcpy(buf, fixture + fixture_pos, n); fixture_pos += n; return n;
}
static ssize_t fixture_write(int fd, const void *buf, size_t n) {
    (void)fd;
    if (n > sizeof fixture - fixture_pos) return -1;
    memcpy(fixture + fixture_pos, buf, n); fixture_pos += n;
    if (fixture_pos > fixture_len) fixture_len = fixture_pos;
    return n;
}
static off_t fixture_lseek(int fd, off_t offset, int whence) {
    (void)fd;
    off_t next = offset + (whence == SEEK_END ? fixture_len :
                          whence == SEEK_CUR ? fixture_pos : 0);
    if (next < 0 || (size_t)next > fixture_len) return -1;
    fixture_pos = next; return next;
}
static int fixture_truncate(int fd, off_t size) {
    (void)fd;
    if (size < 0 || (size_t)size > fixture_len) return -1;
    fixture_len = size; return 0;
}
#define read fixture_read
#define write fixture_write
#define close fixture_close
#define lseek fixture_lseek
#define ftruncate fixture_truncate
#define mkstemp fixture_mkstemp
#define dup fixture_dup
#define unlink fixture_unlink
'''

PICO_MAIN = r'''
int main(void) {
    stdio_init_all();
    while (!stdio_usb_connected()) sleep_ms(10);
    sleep_ms(500);
    printf("NetHack Tamp adapter regression on RP2040\n");
    int result = run_codec_tests();
    printf("RP2040 regression %s\n", result ? "FAIL" : "PASS");
    for (;;) sleep_ms(1000);
}
'''

PREAMBLE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdint.h>
#define STATIC_OVL static
#define TRUE 1
#define FALSE 0
typedef int boolean;
typedef void *genericptr_t;
static struct { int mread_flags; } restoreprocs;
static unsigned max_read;
static ssize_t codec_read(int fd, void *buf, size_t n) {
    if (max_read && n > max_read) n = max_read;
    return read(fd, buf, n);
}
static void panic(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    exit(2);
}
#define error panic
#define nhclose close
'''

MAIN = r'''
int main(void) {
    const unsigned lengths[] = {4, 16, 64, 256, 511, 512, 513, 1024, 4096, 20000};
    const unsigned requests[] = {1, 4, 8, 32, 256, 13440, 20000};
    const unsigned read_limits[] = {0, 1, 2, 511};
    unsigned li, ri, pattern, flushes, mode, cases = 0, failures = 0;
    for (mode = 0; mode < sizeof read_limits / sizeof read_limits[0]; ++mode)
    for (flushes = 0; flushes < 2; ++flushes)
    for (pattern = 0; pattern < 4; ++pattern)
    for (li = 0; li < sizeof lengths / sizeof lengths[0]; ++li)
    for (ri = 0; ri < sizeof requests / sizeof requests[0]; ++ri) {
        unsigned n = lengths[li], step = requests[ri], pos, k;
        unsigned char *raw = malloc(n), *got = malloc(n);
        unsigned char *compressed, *reference;
        uint32_t rng = 42;
        char path[] = "/tmp/nethack-codec-XXXXXX";
        int fd = mkstemp(path), wfd = dup(fd), reference_ok;
        long clen;
        TampDecompressor dec;
        unsigned char window[1024];
        size_t written = 0, consumed = 0;
        tamp_res result;
        unlink(path);
        for (pos = 0; pos < n; ++pos) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            raw[pos] = pattern == 0 ? 0 : pattern == 1 ? 'A' :
                       pattern == 2 ? pos % 17 : rng & 255;
        }
        /* Save headers are raw; only the body is Tamp-compressed. */
        zerocomp_bufoff(wfd);
        zerocomp_bwrite(wfd, (genericptr_t) "raw save header", 15);
        zerocomp_bufon(wfd);
        for (pos = 0; pos < n; pos += k) {
            k = n - pos < 73 ? n - pos : 73;
            zerocomp_bwrite(wfd, raw + pos, k);
            if (flushes && pos % 511 == 0) zerocomp_bflush(wfd);
        }
        zerocomp_bflush(wfd);
        zerocomp_bclose(wfd);
        clen = lseek(fd, 0, SEEK_END) - 15;
        if (clen <= 0 || clen > 24000) exit(2);
        compressed = malloc(clen); reference = malloc(n);
        if (!compressed || !reference || !raw || !got) exit(2);
        lseek(fd, 15, SEEK_SET);
        if (read(fd, compressed, clen) != clen) exit(2);
        tamp_decompressor_init(&dec, NULL, window, 10);
        result = tamp_decompressor_decompress(&dec, reference, n, &written,
                                             compressed, clen, &consumed);
        reference_ok = written == n && !memcmp(raw, reference, n);
        if (!reference_ok) {
            printf("REFERENCE FAIL n=%u pattern=%u flushes=%u output=%zu res=%d\n",
                   n, pattern, flushes, written, result); exit(2);
        }
        memset(got, 0xcc, n);
        lseek(fd, 0, SEEK_SET);
        {
            char header[15];
            if (read(fd, header, 15) != 15 || memcmp(header, "raw save header", 15))
                exit(2);
        }
        max_read = read_limits[mode];
        zerocomp_minit(); restoreprocs.mread_flags = 0;
        for (pos = 0; pos < n; pos += k) {
            k = n - pos < step ? n - pos : step;
            zerocomp_mread(fd, got + pos, k);
            if (restoreprocs.mread_flags == -1) break;
        }
        ++cases;
        if (restoreprocs.mread_flags == -1 || memcmp(raw, got, n)) {
            ++failures;
            if (failures <= 12)
                printf("ADAPTER FAIL raw=%u compressed=%ld request=%u pattern=%u "
                       "flushes=%u stopped_at=%u eof=%d pending_bits=%u skip=%u\n",
                       n, clen, step, pattern, flushes, pos,
                       restoreprocs.mread_flags, nhtamp_decomp.bit_buffer_pos,
                       nhtamp_decomp.skip_bytes);
        }
        /* NetHack reads a one-byte level id to detect end of a save. */
        {
            unsigned char extra[1];
            restoreprocs.mread_flags = 0;
            zerocomp_mread(fd, extra, sizeof extra);
            if (restoreprocs.mread_flags != -1) {
                printf("EOF FAIL raw=%u request=%u\n", n, step); ++failures;
            }
        }
        /* A stream cut in half must not report a complete successful read. */
        ftruncate(fd, 15 + clen / 2);
        lseek(fd, 15, SEEK_SET);
        zerocomp_minit(); restoreprocs.mread_flags = 0;
        zerocomp_mread(fd, got, n);
        if (restoreprocs.mread_flags != -1) {
            printf("TRUNCATION FAIL raw=%u compressed=%ld\n", n, clen); ++failures;
        }
        close(fd); free(raw); free(got); free(compressed); free(reference);
    }
    printf("%u cases; %u adapter failures; all direct decoder checks passed\n",
           cases, failures);
    return failures ? 1 : 0;
}
'''


def codec_block(path):
    source = path.read_text()
    start = source.index('#ifdef ZEROCOMP\n/*')
    end = source.index('#endif /* ZEROCOMP */', start)
    return source[start:end].replace('#ifdef ZEROCOMP\n', '', 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=CONSOLE.parent / 'NetHack',
                        help='Local checkout containing the pinned NetHack commit')
    parser.add_argument('--sanitize', action='store_true',
                        help='Enable address and undefined-behavior sanitizers')
    parser.add_argument('--patch', type=Path, default=PORT / 'config.patch',
                        help='Patch to test, for checking a previous revision')
    parser.add_argument('--pico-source', type=Path,
                        help='Generate a standalone Pico SDK USB test project here')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='nethack-evaluation-') as tmp:
        work = Path(tmp)
        archive = subprocess.check_output([
            'git', '-C', str(args.source), 'archive',
            'ed600d9f0f3c37677418f0150f59363ca641f3dc'])
        with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
            # Input is the pinned, local NetHack Git tree.
            tar.extractall(work)
        subprocess.run(['patch', '--batch', '-p1', '-i', str(args.patch.resolve())],
                       cwd=work, check=True, stdout=subprocess.DEVNULL)
        adapters = (codec_block(work / 'src/save.c')
                    + codec_block(work / 'src/restore.c'))
        if args.pico_source:
            output = args.pico_source.resolve()
            output.mkdir(parents=True, exist_ok=True)
            preamble = PREAMBLE.replace('panic(', 'codec_panic(').replace(
                '#define error panic', '#define error codec_panic')
            body = MAIN.replace('int main(void)', 'static int run_codec_tests(void)')
            body = body.replace('        ++cases;',
                                '        ++cases;\n        if (cases % 128 == 0) '
                                'printf("Progress: %u cases\\n", cases);')
            source = (PICO_IO + preamble + '\n#undef read\n#define read codec_read\n'
                      + adapters.replace('panic(', 'codec_panic(')
                      + '\n#undef read\n#define read fixture_read\n' + body
                      + PICO_MAIN)
            (output / 'probe.c').write_text(source)
            quote = lambda path: json.dumps(str(path))
            cmake = f'''cmake_minimum_required(VERSION 3.13)
include({quote(CONSOLE / 'pico_sdk_import.cmake')})
project(nethack_codec_probe C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()
add_executable(nethack_codec_probe probe.c
    {quote(PORT / 'tamp/common.c')}
    {quote(PORT / 'tamp/compressor.c')}
    {quote(PORT / 'tamp/decompressor.c')})
pico_set_binary_type(nethack_codec_probe no_flash)
target_include_directories(nethack_codec_probe PRIVATE {quote(PORT / 'tamp')})
target_compile_definitions(nethack_codec_probe PRIVATE TAMP_STREAM=0 PICO_STACK_SIZE=4096)
target_compile_options(nethack_codec_probe PRIVATE -O2)
target_link_libraries(nethack_codec_probe pico_stdlib)
pico_enable_stdio_usb(nethack_codec_probe 1)
pico_enable_stdio_uart(nethack_codec_probe 0)
pico_add_extra_outputs(nethack_codec_probe)
'''
            (output / 'CMakeLists.txt').write_text(cmake)
            print('Pico regression project:', output)
            return 0
        harness = work / 'probe.c'
        harness.write_text(PREAMBLE + '\n#define read codec_read\n' + adapters
                           + '\n#undef read\n' + MAIN)
        binary = work / 'probe'
        subprocess.run(['cc', '-std=gnu11', '-O2',
                        '-Wno-deprecated-non-prototype', '-DTAMP_STREAM=0',
                        *(['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g']
                          if args.sanitize else []),
                        '-I', str(PORT / 'tamp'), str(harness),
                        str(PORT / 'tamp/common.c'),
                        str(PORT / 'tamp/compressor.c'),
                        str(PORT / 'tamp/decompressor.c'), '-o', str(binary)],
                       check=True)
        return subprocess.run([str(binary)]).returncode


if __name__ == '__main__':
    raise SystemExit(main())
