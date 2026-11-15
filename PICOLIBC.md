# Picolibc port

Status: in progress on branch `picolibc-port`. Step 1 of the plan below is
done. The CI `picolibc-probe` job tracks how far the source is from building.

## Why it is on the table

The platform is written against newlib, and the tested toolchain is the
distribution's `gcc-arm-none-eabi` package. Current distributions are moving
the arm-none-eabi default C library to picolibc:

- Ubuntu 26.10 and Debian sid no longer ship `libnewlib-arm-none-eabi` or
  `libnewlib-dev`; their `gcc-arm-none-eabi` declares `Breaks:` on those and
  `Recommends: picolibc-arm-none-eabi`.
- Ubuntu 26.04 is therefore a pin, not a steady state.

The alternatives are to keep pinning an old newlib package set, to use Arm's
standalone toolchain (newlib with multibyte/locale support, roughly 34 KB
larger; see [applications/README.md](applications/README.md)), or to port.

## Picolibc's model

Confirmed against `picolibc-arm-none-eabi` 1.8.10:

- **No `struct _reent`.** There is no `reent.h`, `sys/reent.h`, `_impure_ptr`,
  `_GLOBAL_REENT` or `__getreent`.
- **Reentrancy is thread-local storage.** `picotls.h` provides `_tls_size()`,
  `_tls_align()`, `_init_tls(void *)` and `_set_tls(void *)`; `errno` is
  thread-local.
- **Allocator is the plain symbols.** `malloc`, `free`, `calloc`, `realloc`
  and `sbrk`; there is no `_malloc_r` family.
- **`stdin`/`stdout`/`stderr` are not in `libc.a`.** The platform supplies
  them; the SDK's `pico_picolibc_interface` builds them with
  `FDEV_SETUP_STREAM`. (`libsemihost.a` carries them for semihosted builds.)
- `sys/_syslist.h` aliases `_sbrk` to `sbrk`.

The SDK is not the obstacle. `pico_clib_interface` compiles a probe for
`__PICOLIBC__` and links `pico_picolibc_interface`, and `pico_thread_local`
provides the TLS entry points. One SDK caveat: its weak `_sbrk` lives in
`newlib_interface.c`, so it is absent in a picolibc build and the heap hook
must be supplied here (step 1).

## Consequences for the design

- **Session isolation becomes a TLS-block swap**, not a `struct _reent` swap.
  Less state is involved, but the isolation boundary is also narrower: stdio,
  locale and timezone state are no longer carried by the context.
- **Per-session `FILE` objects are not possible.** `stdin`/`stdout`/`stderr`
  are global. The current guarantee -- closing the program's streams cannot
  close the engine's console -- has to be re-established another way
  (intercepting `fclose`, or handing the program duplicate descriptors).
- **Allocation attribution cannot use the context.** Program-versus-platform
  ownership needs an explicit scope flag instead of comparing against
  `&rp2040_program_reent`.

## Plan

1. **Heap hook.** `src/heap.c` defines whichever of `sbrk`/`_sbrk` the C
   library calls. *Done.*
2. **Compile seam.** Introduce a libc-session header with a newlib backend
   (today's code, behaviour unchanged) and a picolibc backend, selected by the
   detected library. Extend `tests/picolibc-probe.sh` to compile the backend.
3. **Session core.** For picolibc, allocate a TLS block of `_tls_size()`,
   `_init_tls()` it, and `_set_tls()` it at the session boundary; state the
   exact set of things a boundary resets.
4. **Stdio.** Provide the program streams over the platform console using the
   SDK's `FDEV_SETUP_STREAM` pattern, and restore the
   console-cannot-be-closed guarantee.
5. **Allocator attribution.** Replace the reent-context check with a scope
   flag around program allocation, keeping the leak reclaim and the pooled
   allocator behaviour.
6. **Syscalls, locale, timezone.** `src/fileio.c` entry points and the
   `setlocale`/`tzset` handling.
7. **Verification.** Port `tests/soft`'s harness (its qemu Linux syscall
   bridge is unchanged; only the libc hooks are), build the integration
   fixture and the hello recipe against picolibc, then repeat board
   validation and update `MEASUREMENTS.json`.

## Local verification

picolibc's specs file assumes an absolute `/usr/lib/picolibc` install. For a
local build from the unpacked package, copy the specs with paths rewritten:

```sh
ROOT=/path/to/unpacked/usr
sed "s#/usr/lib/picolibc#$ROOT/lib/picolibc#g" \
    "$ROOT/lib/picolibc/arm-none-eabi/picolibc.specs" > /tmp/picolibc-local.specs
arm-none-eabi-gcc -specs=/tmp/picolibc-local.specs ...
```

`tests/picolibc-probe.sh` deliberately compiles with `-nostdinc` and the
picolibc include directory alone, so the gap it reports is picolibc's and not
the host toolchain's.

## Risks

- The port rewrites the soft-reboot session layer, which is the reason this
  project exists. It is a re-design of that layer, not a build-flag switch.
- Board validation has to be repeated, and the documented "validated with
  newlib 4.5.0" statements and size records all change.
- TLS is set up for the calling core only; single-threaded core-0 programs are
  the supported profile today, so this matches, but it must be stated.

## Expected benefit

- Smaller firmware: picolibc has no locale category table, no multibyte
  conversion code and a leaner printf/scanf. The magnitude must be measured.
- A supported distro toolchain without pinning a package set that is being
  withdrawn, and without the newlib-versus-Arm-package size discrepancy.
