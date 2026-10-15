#!/usr/bin/env python3
"""Check compact save records and pooled soft-session ownership using ARM/newlib."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir',type=Path,required=True,help='Application build directory')
a=p.parse_args();work=a.build_dir.resolve();src=work/'source';console=HERE.parents[2];out=work/'tests';out.mkdir(exist_ok=True)
def run(code,name,extra=()):
 c=out/(name+'.c');c.write_text(code);exe=out/name
 subprocess.run([sys.executable,str(HERE.parent/'port/host/host32_cc.py'),'--specs=nano.specs','-DRP2040_NETHACK_POOL64=1','-DRP2040_CONSOLE_SOFT_REBOOT=1','-I'+str(console/'src'),'-I'+str(console/'include'),str(c),*map(str,extra),'-o',str(exe)],check=True)
 subprocess.run([sys.executable,str(HERE.parent/'port/host/host32_run.py'),str(exe)],check=True)
h=(src/'include/mkroom.h').read_text();c=(src/'src/mkroom.c').read_text();dh=(src/'include/dungeon.h').read_text()
room=re.search(r'struct mkroom \{.*?\n\};',h,re.S).group()
child=re.search(r'#define ROOM_CHILD[^\n]+',h).group()
funcs='\n'.join(re.search(r'STATIC_OVL void\n'+name+r'\(fd, r\).*?\n\}',c,re.S).group() for name in ['save_room','rest_room'])
macros='\n'.join(re.findall(r'^#define MSROOM[^\n]+',dh,re.M))
code=r'''#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STATIC_OVL static
#define MAXNROFROOMS 40
#define MAX_SUBROOMS 24
typedef signed char schar;
typedef unsigned char boolean;
typedef void *genericptr_t;
struct monst;
'''+room+'\n'+child+'\n'+macros+r'''
#define ROOM_DISK_BYTES offsetof(struct mkroom, sbrooms)
static struct mkroom rooms[82], *subrooms=rooms+41;
static int nsubroom;
static unsigned char disk[4096];
static size_t count,pos;
static void panic(const char *m) {puts(m);abort();}
static void bwrite(int fd,void*p,size_t n) {(void)fd;assert(count+n<=sizeof disk);memcpy(disk+count,p,n);count+=n;}
static void mread(int fd,void*p,size_t n) {(void)fd;assert(pos+n<=count);memcpy(p,disk+pos,n);pos+=n;}
'''+funcs+r'''
int main(void) {
 assert(sizeof(struct mkroom)==44&&ROOM_DISK_BYTES==13);
 rooms[0].lx=2;rooms[0].hx=70;rooms[0].nsubrooms=24;
 for(int i=0;i<24;i++){rooms[0].sbrooms[i]=i+1;subrooms[i].lx=i;subrooms[i].hx=i+1;}
 /* Nest another child below the first, to exercise recursive index allocation. */
 subrooms[0].nsubrooms=1;subrooms[0].sbrooms[0]=25;subrooms[24].lx=39;
 save_room(0,rooms);assert(count==26*13);
 memset(rooms,0,sizeof rooms);rest_room(0,rooms);
 assert(pos==count&&nsubroom==25&&rooms[0].lx==2);
 assert(ROOM_CHILD(ROOM_CHILD(rooms,0),0)->lx==39);
 for(int i=0;i<24;i++){assert(ROOM_CHILD(rooms,i)->lx==i);assert(ROOM_CHILD(rooms,i)->hx==i+1);}
 struct {unsigned char msrooms[((MAXNROFROOMS+1)*4+7)/8];} map={0};
 for(int i=0;i<MSROOM_COUNT;i++){MSROOM_SET(&map,i,0,i&1);MSROOM_SET(&map,i,1,(i>>1)&1);}
 for(int i=0;i<MSROOM_COUNT;i++){assert(MSROOM_GET(&map,i,0)==(i&1));assert(MSROOM_GET(&map,i,1)==((i>>1)&1));}
 printf("PASS compact rooms: 26 nested records, 338 bytes; overview flags %u bytes\n",(unsigned)sizeof map);
 return 0;
}
'''
run(code,'compact-save')
code=r'''#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <reent.h>
#include "rp2040-soft-reboot.h"
#include "soft_reboot_internal.h"
#include "nethack_pool.h"
static unsigned live,restores,handlers;
struct _reent rp2040_program_reent;
void *__real__malloc_r(struct _reent*r,size_t n){(void)r;void*p=malloc(n?n:1);if(p)live++;return p;}
void *__real__calloc_r(struct _reent*r,size_t n,size_t s){(void)r;void*p=calloc(n,s);if(p)live++;return p;}
void __real__free_r(struct _reent*r,void*p){(void)r;if(p){assert(live);live--;free(p);}}
void *__real__realloc_r(struct _reent*r,void*p,size_t n){(void)r;if(!p)return __real__malloc_r(r,n);if(!n){__real__free_r(r,p);return NULL;}return realloc(p,n);}
extern void *__wrap__malloc_r(struct _reent*,size_t);
extern void *__wrap__calloc_r(struct _reent*,size_t,size_t);
extern void *__wrap__realloc_r(struct _reent*,void*,size_t);
extern void __wrap__free_r(struct _reent*,void*);
void rp2040_soft_restore(void){restores++;}
int rp2040_soft_libc_begin(void){return 0;}
int rp2040_soft_libc_end(int flush){(void)flush;return 0;}
void rp2040_soft_platform_cleanup(void){}
void rp2040_soft_fatal(int status){exit(status);}
static void handler(void){handlers++;}
int rp2040_console_game_main(int argc,char**argv){
 (void)argc;(void)argv;struct _reent*r=&rp2040_program_reent;
 assert(!rp2040_soft_live_allocations());
 assert(!__wrap__calloc_r(r,SIZE_MAX,2));
 for(unsigned i=0;i<1000;i++){
  unsigned char*p=__wrap__calloc_r(r,4,16);assert(p&&p[63]==0);memset(p,i%251,64);
  if(i%3==0){unsigned char*q=__wrap__realloc_r(r,p,100);assert(q&&q[63]==i%251);__wrap__free_r(r,q);}
  else if(i%3==1){assert(!__wrap__realloc_r(r,p,0));}
  /* Leave one third live: the real session cleanup must free these slots. */
 }
 assert(rp2040_soft_atexit(handler)==0);
 return 7;
}
int main(void){
 _REENT_INIT_PTR(&rp2040_program_reent);
 unsigned char*p=nh_pool_malloc(_GLOBAL_REENT,64);assert(p);memset(p,0x5a,64);
 unsigned baseline=live;
 for(unsigned i=0;i<100;i++){
  char*argv[]={"pool-test",NULL};assert(rp2040_soft_run(1,argv)==7);
  assert(!rp2040_soft_live_allocations()&&live==baseline);
  for(unsigned j=0;j<64;j++)assert(p[j]==0x5a);
 }
 nh_pool_free(_GLOBAL_REENT,p);assert(!live&&restores==100&&handlers==100);
 puts("PASS 100 pooled soft sessions: leaked slots reclaimed, platform slot survives, all slabs released");
 return 0;
}
'''
run(code,'pool-session',[console/'src/nethack_pool.c',console/'src/soft_reboot.c'])
assert '#define EDITLEVEL 130' in (src/'include/patchlevel.h').read_text()
assert '#define VERSION_COMPATIBILITY 0x03060782L' in (src/'include/patchlevel.h').read_text()
print('PASS distinct compact-save incarnation')

subprocess.run([sys.executable, str(HERE/'test-levels.py'), '--build-dir', str(work)], check=True)

subprocess.run([sys.executable,str(HERE/"test-checkpoint.py"),"--build-dir",str(work)],check=True)

subprocess.run([sys.executable,str(HERE/"test-records.py"),"--build-dir",str(work)],check=True)

subprocess.run([sys.executable,str(HERE/"test-quest-traits.py"),"--build-dir",str(work)],check=True)
