#!/usr/bin/env python3
"""Add only initial staircase positioning to an owned generated RELEASE tree.

Build ordinary firmware first. Keep that original ELF/UF2 separately. This
fixture does not enable wizard mode; the final checks run on the original
ordinary firmware after restoring a normally saved fixture character.
"""
import argparse
from pathlib import Path
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--source', type=Path, required=True)
a = p.parse_args()
src = a.source.resolve()
if not (src.parent / '.rp2040-nethack-build').exists():
    p.error('Not a generated recipe source tree')
f = src / 'src/allmain.c'
s = f.read_text()
old = '\tu_on_upstairs();'
assert s.count(old) == 1
s = s.replace(old, '''\tu_on_upstairs();
    if (xdnstair) {
        if (m_at(xdnstair, ydnstair)) mnexto(m_at(xdnstair, ydnstair));
        u.ux = xdnstair; u.uy = ydnstair;
    }
''')
f.write_text(s)

f = src / 'sys/unix/unixmain.c'
s = f.read_text()
old = '\tprocess_options(argc, argv);'
assert s.count(old) == 1
s = s.replace(old, old + '\n\tStrcpy(plname, "stairs");\n\tStrcpy(pl_character, "V");\n\tflags.legacy = FALSE;')
f.write_text(s)
