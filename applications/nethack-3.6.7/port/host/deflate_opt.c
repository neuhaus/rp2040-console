/* Native build tool: emit a bounded-window raw-deflate stream on stdout. */
#include "zopfli.h"
#include "deflate.h"
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    size_t size = 0, capacity = 32768, compressed_size = 0;
    unsigned char *input = malloc(capacity), *compressed = NULL, bits = 0;
    ZopfliOptions options;
    if (!input) return 1;
    for (;;) {
        size_t got = fread(input + size, 1, capacity - size, stdin);
        size += got;
        if (ferror(stdin)) { free(input); return 1; }
        if (feof(stdin)) break;
        if (capacity > (size_t)-1 / 2) { free(input); return 1; }
        capacity *= 2;
        unsigned char *larger = realloc(input, capacity);
        if (!larger) { free(input); return 1; }
        input = larger;
    }
    ZopfliInitOptions(&options);
    options.numiterations = 3;
    ZopfliDeflate(&options, 2, 1, input, size, &bits,
                 &compressed, &compressed_size);
    int ok = fwrite(compressed, 1, compressed_size, stdout) == compressed_size;
    if (fflush(stdout) != 0) ok = 0;
    free(input);
    free(compressed);
    return ok ? 0 : 1;
}
