#!/usr/bin/env python3
"""Compile a 32-bit ARM semihosted data generator for the measurement builds."""
import os
from pathlib import Path
import subprocess
import sys

compiler = os.environ.get('ARM_CC', 'arm-none-eabi-gcc')
options = ['-mcpu=cortex-m3', '-mthumb', '-fsigned-char', '-Wno-implicit-function-declaration',
           '-Wno-int-conversion']
if '-c' not in sys.argv:
    options += ['--specs=rdimon.specs', '-Wl,-Ttext=0x00010000',
                '-Wl,--section-start=.isr_vector=0x0',
                '-Wl,--undefined=host32_vectors',
                '-Wl,--defsym=__stack=0x203ff000',
                str(Path(__file__).with_name('host32_vectors.c'))]
raise SystemExit(subprocess.run([compiler] + options + sys.argv[1:]).returncode)
