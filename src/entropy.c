/*
 * getentropy() for the bare-metal Pico build.
 *
 * Why the port has to provide this at all: newlib for RP2040 has neither
 * getentropy() nor <sys/random.h>, so a program seeding itself by feature
 * detection finds nothing and reaches for something like
 *
 *      time(NULL) ^ (getpid() * 0x9e3779b1u)
 *
 * Neither term varies here. time() is the engine's clock, which starts from
 * a build timestamp unless the player sets it, and getpid() is a constant.
 * Two boots within the same second would then generate the same world, and
 * a board that is reset and replayed immediately - the normal way this
 * happens - always does. Nothing looks wrong, because the stream is still
 * random-looking; it is just the same stream.
 *
 * Providing getentropy() lets whatever feature detection the program
 * already has pick it up, so no game source has to know about the Pico.
 *
 * The bytes come from pico_rand, which mixes the ring oscillator, the
 * microsecond timer, the board's unique id and a hash of the top of RAM
 * into a 128-bit generator. Seeding happens on the first call and costs
 * about a millisecond; the microsecond timer keeps the state moving after
 * that.
 *
 * On quality: Raspberry Pi documents the ring oscillator as unsuitable for
 * cryptographic use. Game seeding is not that case. What matters is that
 * two boots differ, and nothing in the game depends on the dungeon being
 * unpredictable to an adversary.
 */

#include <stdint.h>
#include <string.h>

#include "pico/rand.h"
#include "sys/random.h"

int getentropy(void *buffer, size_t length)
{
    uint8_t *out = buffer;

    while (length > 0) {
        uint32_t r = get_rand_32();
        size_t n = (length < sizeof(r)) ? length : sizeof(r);

        memcpy(out, &r, n);
        out += n;
        length -= n;
    }

    /* get_rand_32() has no failure mode, so there is no -1/errno path */
    return 0;
}
