#!/usr/bin/env python3
"""Run an ARM/newlib ILP32 generator with semihosted access to the host cwd."""
import os
import subprocess
import sys

arguments = ['enable=on', 'target=native']
values = ['generator']
for value in sys.argv[2:]:
    # Newlib's semihosted startup has a small command-line buffer. Preserve
    # the caller's cwd and express existing paths relative to it.
    if os.path.isabs(value) and os.path.exists(value):
        value = os.path.relpath(value)
    values.append(value)
arguments += ['arg=' + value for value in values]
command = [os.environ.get('MEASURE_QEMU', 'qemu-system-arm'), '-M',
           'mps2-an385', '-display', 'none', '-serial', 'none',
           '-monitor', 'none', '-semihosting-config',
           ','.join(arguments), '-kernel', sys.argv[1]]
raise SystemExit(subprocess.run(command, timeout=180).returncode)
