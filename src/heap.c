#include <errno.h>
#include <stdint.h>

/* The SDK's weak _sbrk bounds growth by __StackLimit, which can be above a
 * manifest HEAP_END. Enforce the actual heap limit without resetting malloc's
 * shared allocator state between sessions. */
void *_sbrk(int increment)
{
    extern char end, __HeapLimit;
    static uintptr_t heap_end;
    const uintptr_t base = (uintptr_t)&end;
    const uintptr_t limit = (uintptr_t)&__HeapLimit;
    if (!heap_end) heap_end = base;
    if (heap_end < base || heap_end > limit) { errno = ENOMEM; return (void *)-1; }
    uintptr_t previous = heap_end;
    if (increment >= 0) {
        if ((uintptr_t)increment > limit - heap_end) { errno = ENOMEM; return (void *)-1; }
        heap_end += (uintptr_t)increment;
    } else {
        uintptr_t amount = (uintptr_t)(-(int64_t)increment);
        if (amount > heap_end - base) { errno = ENOMEM; return (void *)-1; }
        heap_end -= amount;
    }
    return (void *)previous;
}
