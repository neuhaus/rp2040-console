#!/usr/bin/env python3
"""Test the actual checkpoint manifest and cache lifecycle with real files."""
from pathlib import Path
import argparse,re,subprocess,os
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build-dir',type=Path,required=True);a=p.parse_args()
work=a.build_dir.resolve();src=work/'game/source';out=work/'tests/checkpoint';out.mkdir(parents=True,exist_ok=True)
code=(src/'src/checkpoint.c').read_text().replace('#include "hack.h"',r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <setjmp.h>
#include <stdarg.h>
#define Strcpy strcpy
typedef void *genericptr_t;
#define MAXDUNGEON 4
#define MAXLEVEL 16
#define LFILE_EXISTS 1
#define LEVELPREFIX 0
#define SAVEPREFIX 1
#define FCMASK 0600
#define TRUE 1
static struct {unsigned flags;} level_info[66];
static struct {int preserve_locks;} program_state;
static char lock[128], SAVEF[128];
static int hackpid=1;
static jmp_buf failure;
static char reason[256];
static int armed;
static void panic(const char *fmt,...) {
 va_list ap;va_start(ap,fmt);vsnprintf(reason,sizeof reason,fmt,ap);va_end(ap);
 if(!armed){fprintf(stderr,"unexpected: %s\n",reason);abort();}longjmp(failure,1);
}
#define error panic
static int maxledgerno(void){return 3;}
static int nhclose(int fd){return close(fd);}
static void set_levelfile_name(char *name,int lev){snprintf(name,128,"0game.%d",lev);}
static const char *fqname(const char *name,int prefix,int slot){(void)prefix;(void)slot;return name;}
static void set_savefile_name(int regularize){(void)regularize;strcpy(SAVEF,"save/0game");}
static int open_savefile(void){return open(SAVEF,O_RDONLY);}
static int open_levelfile(int lev,char *why){(void)why;char name[128];snprintf(name,sizeof name,"0game.%d",lev);return open(name,O_RDONLY);}
''')
u=(src/'sys/unix/unixunix.c').read_text()
code+='\n'+re.search(r'static int\neraseoldlocks\(\).*?\n\}',u,re.S).group()
code+='\n'+re.search(r'void\ngetlock\(\).*?\n\}',u,re.S).group()
s=(src/'src/save.c').read_text();commit=re.search(r'    if \(unlink\(fq_save\).*?panic\("cannot commit checkpoint \(%d\)", errno\);',s,re.S).group()
code+='\nstatic void commit(void){const char *fq_save="save/0game";\n'+commit+'\n}\n'
code+=r'''
static void put(const char *name,const void *data,size_t n){int fd=open(name,O_WRONLY|O_CREAT|O_TRUNC,0600);assert(fd>=0);assert(write(fd,data,n)==(ssize_t)n);assert(!close(fd));}
static void expect_bad(const char *expected){
 int fd=open("manifest",O_RDONLY);assert(fd>=0);armed=1;
 if(!setjmp(failure)){nh_checkpoint_read(fd,1);assert(!"corruption accepted");}
 assert(strstr(reason,expected));armed=0;close(fd);
}
int main(void){
 unsigned char a[1000],b[731],manifest[26];for(unsigned i=0;i<sizeof a;i++)a[i]=i*13;for(unsigned i=0;i<sizeof b;i++)b[i]=i*17;
 mkdir("save",0700);put("0game.1",a,sizeof a);put("0game.3",b,sizeof b);
 level_info[1].flags=level_info[2].flags=level_info[3].flags=LFILE_EXISTS;
 int fd=open("manifest",O_RDWR|O_CREAT|O_TRUNC,0600);assert(fd>=0);
 nh_checkpoint_write(fd,2);assert(lseek(fd,0,SEEK_END)==26);assert(lseek(fd,0,SEEK_SET)==0);
 assert(read(fd,manifest,sizeof manifest)==sizeof manifest);lseek(fd,0,SEEK_SET);nh_checkpoint_read(fd,1);assert(lseek(fd,0,SEEK_CUR)==26);close(fd);
 put("vector","123456789",9);fd=open("vector",O_RDONLY);uint32_t n;assert(nh_checkpoint_crc(fd,&n)==0xcbf43926U&&n==9);close(fd);
 a[0]^=1;put("0game.1",a,sizeof a);expect_bad("damaged checkpoint level 1");a[0]^=1;put("0game.1",a,sizeof a);
 assert(!rename("0game.3","hidden"));expect_bad("missing checkpoint level 3");assert(!rename("hidden","0game.3"));
 put("manifest",manifest,9);expect_bad("truncated checkpoint manifest");put("manifest",manifest,sizeof manifest);
 unsigned char duplicate[26];memcpy(duplicate,manifest,26);duplicate[16]=1;duplicate[17]=0;put("manifest",duplicate,26);expect_bad("invalid checkpoint level 1");put("manifest",manifest,26);
 /* A committed companion preserves its sidecars across startup. */
 put("save/0game",manifest,26);getlock();assert(access("0game.1",F_OK)==0&&access("0game.3",F_OK)==0);assert(access("0game.0",F_OK)==0);
 /* A completed temp is still not a checkpoint until publication succeeds. */
 unlink("save/0game");put("0game.tmp",manifest,26);getlock();assert(access("0game.tmp",F_OK)<0);assert(access("0game.1",F_OK)<0&&access("0game.3",F_OK)<0);
 put("0game.tmp",manifest,26);commit();assert(access("0game.tmp",F_OK)<0);fd=open("save/0game",O_RDONLY);assert(fd>=0);unsigned char got[26];assert(read(fd,got,26)==26&&!memcmp(got,manifest,26));close(fd);
 unlink("save/0game");armed=1;if(!setjmp(failure)){commit();assert(!"missing temp committed");}assert(strstr(reason,"cannot commit checkpoint"));armed=0;assert(access("save/0game",F_OK)<0);
 puts("PASS checkpoint: CRC vector, round trip, damaged/missing/truncated/duplicate levels, cache preservation, interrupted-temp cleanup and completed-file publication");
 return 0;
}
'''
c=out/'test.c';c.write_text(code);exe=out/'test'
subprocess.run(['cc','-std=gnu11','-fsanitize=address,undefined','-fno-omit-frame-pointer','-g',str(c),'-o',str(exe)],check=True)
env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0';subprocess.run([str(exe)],cwd=out,env=env,check=True)
