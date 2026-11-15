# Picolibc port

Status: in progress on branch `picolibc-port`.

**Scope decision: soft reboot stays newlib-only.** It is the project's only
consumer of newlib's reentrancy contexts, so rather than rebuild it on
picolibc, picolibc builds restart the program with the default hardware
reboot. `rp2040_console_game()` rejects `SOFT_REBOOT` when the C library is
not newlib. This keeps the feature on the validated 26.04 toolchain, makes the
port small, and stays reversible: the session layer can be re-implemented on
picolibc later, on its own schedule, using the design sketched under
"Allocator attribution" below.

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

## Consequences if soft reboot is ported

These follow from picolibc's model and matter only if the session layer is
ever brought over; nothing below is implemented.

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

With soft reboot out of scope for picolibc, the port is small. Steps 1 and 2
below are done, and the hello recipe already builds and links under picolibc:

1. **Heap hook.** `src/heap.c` defines whichever of `sbrk`/`_sbrk` the C
   library calls. *Done.*
2. **Separate the libc backend from the core.** `src/soft_reboot.c` is
   C-library agnostic and compiles against picolibc; the newlib allocation
   attribution moved to `src/soft_reboot_newlib.c`. *Done, and now only
   compiled for a newlib build.*
3. **Reject unsupported combinations.** `SOFT_REBOOT` fails configuration
   when the C library is not newlib, and the integration fixture skips its
   soft-reboot app. *Done.*
4. **Build the rest.** Verify every recipe that does not need soft reboot
   (`hello` builds and produces both UF2s; the integration fixture's other
   apps) and record the size change.
5. **Nethack only.** `src/nethack_pool*.{c,h}` hooks newlib's `_malloc_r`,
   and NetHack 3.6.7 always builds with that pooled 64-bit allocator. It does
   *not* need soft reboot: `NETHACK_SOFT_REBOOT` and 3.1.3's `--soft-reboot`
   default off, and in the default hardware-reboot build `nethack_pool.c`
   supplies the `__wrap__*_r` hooks itself. Porting the pool to picolibc is
   therefore independent of soft reboot, and its hook would target the
   `__malloc_malloc`/`__malloc_free` primitives described below.
6. **CI.** Replace the source-level compatibility probe with a build of the
   recipes under the distribution picolibc.

## Allocator attribution (if soft reboot is ever wanted on picolibc)

Recorded for later: picolibc has no contexts, and the SDK already wraps
`malloc`/`calloc`/`realloc`/`free`, so newlib's `--wrap=_malloc_r` has no
picolibc counterpart. Interception is still possible at picolibc's internal
primitives, which the SDK does not touch and which every public entry point
funnels through: `malloc` and `free` reach `__malloc_malloc` / `__malloc_free`,
`calloc` reaches `malloc`, and `realloc` moves through `malloc`/`free` (its
in-place path keeps the pointer). `--wrap=__malloc_malloc` and
`--wrap=__malloc_free` are therefore the analogue of today's `--wrap=_malloc_r`,
with a session-scoped flag standing in for the reentrancy-context comparison.
The ledger's realloc handling stays correct as written, because a moving
realloc records the new pointer through `malloc` and forgets the old one
through `free`.

The caveat is precision: the newlib rule is "allocated on the program's
reent"; the picolibc rule would be "allocated while a session is active", and
those agree only if the platform does not allocate during a session. That
would need to be an explicit, checked invariant.

## Local verification

picolibc's specs file assumes an absolute `/usr/lib/picolibc` install. For a
local build from the unpacked package, copy the specs with paths rewritten:

```sh
ROOT=/path/to/unpacked/usr
sed "s#/usr/lib/picolibc#$ROOT/lib/picolibc#g" \
    "$ROOT/lib/picolibc/arm-none-eabi/picolibc.specs" > /tmp/picolibc-local.specs
cmake -S applications/hello -B build/hello-picolibc -G Ninja \
    -DCMAKE_C_FLAGS="-specs=/tmp/picolibc-local.specs -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft" \
    -DCMAKE_CXX_FLAGS="-specs=/tmp/picolibc-local.specs -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft" \
    -DCMAKE_ASM_FLAGS="-specs=/tmp/picolibc-local.specs -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft"
```

The CPU flags have to be repeated because setting `CMAKE_*_FLAGS` suppresses
the toolchain's `CMAKE_*_FLAGS_INIT`, and the `-specs` must be passed through
the language flags rather than the linker flags, or the specs file is loaded
twice at link time and fails with "attempt to rename spec". A distribution
whose default C library is already picolibc needs none of this: the SDK
reports `C library type is picolibc` on its own, which is how the CI job
builds it.

## Risks

- Soft reboot is not available on picolibc. It is opt-in and off by default
  in every recipe (`ROGUE_SOFT_REBOOT`, `NETHACK_SOFT_REBOOT` and 3.1.3's
  `--soft-reboot` all default off), so nothing in the tree depends on it; only
  a build that explicitly enables it needs newlib.
- picolibc is a different C library, so behaviour that newlib happened to
  provide needs checking on hardware: the timezone and locale defaults, the
  behaviour of `stdio` buffering over the console, and `errno` reporting.
- Board validation has to be repeated for a picolibc build, and the
  documented "validated with newlib 4.5.0" statements and size records all
  change.

## Measured benefit

Firmware size for the same sources (RP2040, Cortex-M0+, `-O3`):

| | newlib 4.5.0 | picolibc 1.8.10 |
| --- | --- | --- |
| `hello` | 99064 B | 66316 B |
| fixture `native_app` | 99048 B | 66284 B |
| fixture `args_app` | 99040 B | 66268 B |
| fixture `generated_app` | 104920 B | 71828 B |

About 33 KB, or a third, comes off every image: newlib's locale category
table, its multibyte conversion code, and its wider printf/scanf. The fixture's
soft-reboot app has no picolibc figure because it is not built under picolibc.
