#!/usr/bin/env python3
"""Check upstream terminfo colour generation and single-checkpoint naming."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, required=True)
args = parser.parse_args()
source = args.build_dir.resolve() / 'source'
console = HERE.parents[2]
assert re.search(r'^#define TERMINFO\b', (source / 'include/unixconf.h').read_text(), re.M)
terminal = (source / 'win/tty/termcap.c').read_text()
# Select the upstream TERMINFO implementation, before the legacy termcap branch.
terminal = terminal[terminal.index('/* Mapping data for the six terminfo colors'):]
terminal = terminal[:terminal.index('#else /* UNIX && TERMINFO */')]
init = re.search(r'static void\ninit_hilite\(\).*?\n\}', terminal, re.S).group()
cleanup = re.search(r'static void\nkill_hilite\(\).*?\n\}', terminal, re.S).group()
table = terminal[:terminal.index('static void')]
code = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curses.h>
#include <termcap.h>
#include "color.h"
#define Strcpy strcpy
#define Strcat strcat
static unsigned live;
static void *alloc(size_t n) {void *p=malloc(n);assert(p);live++;return p;}
static void release(void *p) {if(p){assert(live);live--;free(p);}}
#define free release
static char *hilites[CLR_MAX];
static char *MD, *nh_HI;
static struct {int wc2_darkgray;} iflags;
''' + table + init + cleanup + r'''
int main(void) {
 MD=tgetstr("md",NULL);nh_HI=tgetstr("so",NULL);
 const int colors[]={CLR_RED,CLR_GREEN,CLR_BROWN,CLR_BLUE,CLR_MAGENTA,CLR_CYAN};
 for(int dark=0;dark<2;dark++) {
  iflags.wc2_darkgray=dark;init_hilite();
  assert(!strcmp(hilites[CLR_BLACK],dark?"\033[1m\033[30m":"\033[34m"));
  assert(!strcmp(hilites[CLR_WHITE],"\033[1m\033[37m"));
  assert(!*hilites[CLR_GRAY]&&!*hilites[NO_COLOR]);
  for(int i=0;i<6;i++) {
   char expected[]="\033[31m";expected[3]+=i;
   assert(!strcmp(hilites[colors[i]],expected));
   assert(!strncmp(hilites[colors[i]|BRIGHT],"\033[1m",4));
  }
  for(int i=0;i<CLR_MAX;i++)assert(!strstr(hilites[i],"[7m"));
  kill_hilite();assert(!live);
 }
 puts("PASS upstream terminfo: all colours, dark-gray/blue black fallback, no inverse floors, cleanup");
 return 0;
}
'''
unix = (source / 'sys/unix/unixmain.c').read_text()
resume = re.search(r'static boolean\nresume_name\(\).*?\n\}', unix, re.S).group()
fallback = re.search(r'    if \(!resuming && temporary_name\) \{.*?\n    \}', unix, re.S).group()
naming = r'''
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define TRUE 1
#define FALSE 0
#define Strcpy strcpy
typedef int boolean;
static char plname[32];
static int lastfd;
static void set_savefile_name(int regularize) {(void)regularize;}
static int open_savefile(void) {return lastfd=open("checkpoint",O_RDONLY);}
#define nhclose close
''' + resume + '\nstatic void rejected(int resuming, int *pending) {\nint temporary_name=*pending;\n' + fallback + '\n*pending=temporary_name;\n}\n' + r'''
int main(void) {
 assert(!resume_name()&&!*plname);
 FILE *f=fopen("checkpoint","wb");assert(f);fclose(f);
 assert(resume_name()&&!strcmp(plname,"resume"));
 assert(fcntl(lastfd,F_GETFD)==-1); /* probe releases its descriptor */
 strcpy(plname,"SavedHero");
 int pending=1;rejected(1,&pending);assert(!strcmp(plname,"SavedHero"));
 rejected(0,&pending);assert(!*plname&&!pending);
 strcpy(plname,"NewHero");rejected(0,&pending);assert(!strcmp(plname,"NewHero"));
 assert(!resume_name()&&!strcmp(plname,"NewHero"));
 puts("PASS startup naming: new-game prompt, checkpoint placeholder, saved/explicit names, rejected-save retry");
 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    for name, text, extra in [('colours', code, [console/'src/termcap_shim.c']),
                              ('naming', naming, [])]:
        c = work / (name + '.c')
        c.write_text(text)
        exe = work / name
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-I'+str(console/'include'), '-I'+str(source/'include'),
                        str(c), *map(str,extra), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], cwd=work, check=True)
