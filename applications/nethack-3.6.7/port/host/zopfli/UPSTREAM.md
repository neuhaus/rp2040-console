# Build-only Zopfli compressor

Source: https://github.com/google/zopfli
Pinned commit: `df1517dd8518113a245e4d203a04b8a0e02ab493`.
License: Apache-2.0; see LICENSE and the source headers.

This is a native host tool, never linked into NetHack or the Pico firmware.
Its compressed output uses the port's existing raw-deflate format. The only
upstream modification wraps ZOPFLI_WINDOW_SIZE in an #ifndef guard, allowing
separate 1, 2 and 4 KiB window builds. The archive chooses the smallest result
from zlib and Zopfli and verifies every entry with the target inflater.
