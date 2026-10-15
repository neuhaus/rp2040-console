#!/usr/bin/env python3
"""Exercise the real command initialization and byte bindings with ARM LTO."""
import argparse
from pathlib import Path
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, required=True)
args = parser.parse_args()
work = args.build_dir.resolve()
out = work / 'tests'
out.mkdir(exist_ok=True)
s = (work / 'source/src/cmd.c').read_text()
func = '\n'.join(re.search(r'(?:boolean|void)\n' + name
                          + r'\([^\n]*\).*?\n\}', s, re.S).group()
                 for name in ['bind_key', 'commands_init'])
pre='''#include <assert.h>
#include <string.h>
#include <stdio.h>
typedef unsigned char uchar;
typedef signed char boolean;
#define TRUE 1
#define FALSE 0
#define C(c) (0x1f & (c))
#define M(c) ((c) - 128)
struct ext_func_tab {uchar key; const char *ef_txt;};
struct cmd {unsigned pad[6];struct ext_func_tab *commands[256];};
struct {volatile unsigned before[256];struct cmd cmd;volatile unsigned after[256];} area;
#define Cmd area.cmd
struct ext_func_tab extcmdlist[]={
{0,"redraw"},{0,"help"},{0,"jump"},{0,"kick"},{0,"loot"},
{0,"annotate"},{0,"name"},{0,"untrap"},{0,"overview"},
{0,"twoweapon"},{0,"wait"},{0,0}};
boolean bind_key(int,const char*);
'''
post='''
int main(void) {
 for(unsigned i=0;i<256;i++)area.before[i]=area.after[i]=0xabadcafe;
 commands_init();
 for(unsigned i=0;i<256;i++)assert(area.before[i]==0xabadcafe&&area.after[i]==0xabadcafe);
 assert(Cmd.commands[238]&&strcmp(Cmd.commands[238]->ef_txt,"name")==0);
 assert(Cmd.commands[206]&&strcmp(Cmd.commands[206]->ef_txt,"name")==0);
 assert(Cmd.commands[207]&&strcmp(Cmd.commands[207]->ef_txt,"overview")==0);
 assert(Cmd.commands[178]&&strcmp(Cmd.commands[178]->ef_txt,"twoweapon")==0);
 for(int k=-128;k<256;k++) {
  struct ext_func_tab *before[256];memcpy(before,Cmd.commands,sizeof before);
  assert(!bind_key(k,"missing-command"));
  assert(!memcmp(before,Cmd.commands,sizeof before));
  assert(bind_key(k,"name"));
  assert(Cmd.commands[(uchar)k]==extcmdlist+6);
  for(unsigned j=0;j<256;j++)if(j!=(uchar)k)assert(Cmd.commands[j]==before[j]);
  assert(bind_key(k,"nothing"));assert(!Cmd.commands[(uchar)k]);
  for(unsigned j=0;j<256;j++)if(j!=(uchar)k)assert(Cmd.commands[j]==before[j]);
 }
 for(unsigned i=0;i<256;i++)assert(area.before[i]==0xabadcafe&&area.after[i]==0xabadcafe);
 puts("PASS signed meta bindings and guards");return 0;
}
'''
c = out / 'bindings.c'
c.write_text(pre + func + post)
for lto in [False, True]:
    exe = out / ('bindings-lto' if lto else 'bindings')
    options = ['-flto', '-fipa-pta', '-flto-partition=one'] if lto else []
    subprocess.run([sys.executable, str(HERE.parent / 'port/host/host32_cc.py'),
                    '-std=gnu11', '-Oz', *options, str(c), '-o', str(exe)], check=True)
    subprocess.run([sys.executable, str(HERE.parent / 'port/host/host32_run.py'),
                    str(exe)], check=True)
