# Picolibc port scope

Status: not started. This records why picolibc is on the table, what blocks
it, and what a port would involve. Nothing here is implemented; the CI
`picolibc-probe` job tracks how far the source is from building.

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
larger; see [applications/README.md](applications/README.md)), or to port to
picolibc.

## What blocks it

picolibc is a newlib derivative, but it dropped the reentrant `_xxx_r` API in
favour of thread-local storage. Against `picolibc-arm-none-eabi` 1.8.10 the
API the session core uses is simply absent:

| Required | picolibc |
| --- | --- |
| `reent.h`, `sys/reent.h`, `stdio_ext.h` | absent |
| `struct _reent`, `_GLOBAL_REENT`, `_impure_ptr`, `__getreent` | absent |
| `_malloc_r`, `_free_r`, `_calloc_r`, `_realloc_r` | absent (`malloc`/`free`/`__malloc_free`) |
| `_open_r`, `_read_r`, `_write_r`, `_lseek_r`, `_fstat_r` | absent |
| `_setlocale_r`, `_tzset_r` | absent (`setlocale`/`tzset`) |
| `_sbrk` | `sbrk` |

The current `tests/picolibc-probe.sh` output shows the resulting failures:

```
src/soft_reboot_internal.h:5:10: fatal error: reent.h: No such file or directory
src/soft_reboot_libc.c:7:10: fatal error: stdio_ext.h: No such file or directory
src/nethack_pool.h:4:10: fatal error: sys/reent.h: No such file or directory
```

The SDK is not the obstacle. `pico_clib_interface` compiles a probe that looks
for `__PICOLIBC__` and links `pico_picolibc_interface`, and
`pico_thread_local` provides the picolibc TLS entry points (`_tls_size`,
`_init_tls`, `_set_tls`). The work is entirely in this checkout.

## Affected code

| File | Newlib dependency |
| --- | --- |
| `src/soft_reboot_internal.h` | includes `reent.h`; declares the per-session context |
| `src/soft_reboot.c` | `_GLOBAL_REENT`; wraps `_malloc_r`/`_free_r`/`_calloc_r`/`_realloc_r` for program-only reclaim |
| `src/soft_reboot_libc.c` | `_impure_ptr`, `stdio_ext.h`; wraps `_setlocale_r`/`_tzset_r`; rebuilds program stdio |
| `src/nethack_pool.c`, `nethack_pool.h`, `nethack_pooled64.h` | `struct _reent` allocator signatures and the same `_xxx_r` wraps |
| `src/fileio.c` | `_open_r`, `_lseek_r` reentrant entry points |
| `src/heap.c` | strong `_sbrk` enforcing the manifest `HEAP_END` |
| `tests/soft/control.c`, `tests/soft/linux.c`, `tests/soft/run.sh` | the qemu harness mirrors `_impure_ptr`, `_sbrk` and the `_xxx_r` wraps |

## What a port involves

1. **Session boundary.** Replace the per-session `struct _reent` with a
   per-session TLS block: allocate a fresh block, `_set_tls()` to it at the
   start of each session, and reset stdio, locale and timezone state
   explicitly rather than relying on a zeroed `_reent`. The observable
   contract (new pid, fresh streams, restored terminal modes, reset
   environment/locale/umask/cwd) can be preserved; the mechanism cannot.
2. **Allocator reclaim.** picolibc's allocator is not reachable through
   `_malloc_r`, so the attribution of allocations to program versus platform
   has to be re-established through `malloc`/`calloc`/`realloc`/`free` (the
   SDK already wraps these) or `__malloc_malloc`/`__malloc_free`. This is the
   least mechanical part: the current reclaim relies on the reentrant
   signature to know which context an allocation belongs to.
3. **Stdio.** Drop `stdio_ext.h` and the `struct _reent` internals; rebuild
   the program's streams against picolibc stdio the way
   `pico_picolibc_interface` does with `FDEV_SETUP_STREAM`.
4. **Locale and timezone.** Replace the `_setlocale_r`/`_tzset_r` wraps with
   the non-reentrant `setlocale`/`tzset`, or drop them if the TLS swap
   already isolates the state.
5. **Heap.** Provide `sbrk` rather than `_sbrk` so `HEAP_END` stays enforced;
   the check itself is unchanged.
6. **Syscalls.** `src/fileio.c` already implements the POSIX surface; it must
   expose the names picolibc expects instead of the `_xxx_r` entry points.

## Risks

- The port rewrites the soft-reboot session layer, which is the reason this
  project exists. It is a re-design of that layer, not a build-flag switch.
- Board validation has to be repeated. The documented "validated with newlib
  4.5.0" statements, `MEASUREMENTS.json` and per-application size records all
  change.
- TLS is set up for the calling core only; single-threaded core-0 programs are
  the supported profile today, so this matches, but it must be stated.

## Expected benefit

- Smaller firmware: picolibc has no locale category table, no multibyte
  conversion code and a leaner printf/scanf. The magnitude must be measured,
  not assumed.
- A supported distro toolchain without pinning a package set that is being
  withdrawn, and without the newlib-versus-Arm-package size discrepancy.

## Decision

Not scheduled. The CI probe fails as expected while the port is unstarted and
turns into a warning annotation if the session core ever compiles against
picolibc, which is the point at which this document should be rewritten as a
plan.
