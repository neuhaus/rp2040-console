"""Check actual map and object/monster save functions on ARM/newlib."""
import argparse,re,subprocess,sys
from pathlib import Path
here=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir',type=Path,required=True)
a=p.parse_args();work=a.build_dir.resolve();src=work/'source';out=work/'tests';out.mkdir(exist_ok=True)
s=(src/'src/save.c').read_text()
funcs='\n'.join(re.search(r'STATIC_OVL void\n'+name+r'\(fd, '+arg+r'\).*?\n\}',s,re.S).group() for name,arg in [('saveobj','otmp'),('savemon','mtmp')])
funcs+='\n'+re.search(r'STATIC_OVL void\nsavelevl\(fd, rlecomp\).*?\n\}',s,re.S).group()
funcs+='\n'+re.search(r'STATIC_OVL void\nrest_levl\(fd, rlecomp\).*?\n\}',(src/'src/restore.c').read_text(),re.S).group()
code=r'''#include "compat.h"
#include "hack.h"
#include <assert.h>
#include <stdint.h>
static unsigned char output[32768];
static size_t used, cursor;
#undef levl
static struct rm levl[COLNO][ROWNO];
void mread(int fd,genericptr_t data,unsigned n){(void)fd;assert(cursor+n<=used);memcpy(data,output+cursor,n);cursor+=n;}
void bwrite(int fd,genericptr_t data,unsigned n){(void)fd;assert(used+n<=sizeof output);memcpy(output+used,data,n);used+=n;}
static void savemon(int,struct monst*);
'''+funcs+r'''
int main(void){
 struct rm saved[COLNO][ROWNO];
 for(int pattern=0;pattern<4;pattern++){
  memset(levl,0,sizeof levl);
  for(int y=0;y<ROWNO;y++)for(int x=0;x<COLNO;x++){
   int i=y*COLNO+x;struct rm *r=&levl[x][y];
   r->glyph=pattern==0?0:pattern==1?i:pattern==2?i/254:i/255;
   r->typ=ROOM;r->seenv=255;r->flags=31;r->horizontal=1;r->lit=1;r->waslit=1;r->roomno=63;r->edge=1;r->candig=1;
  }
  memcpy(saved,levl,sizeof levl);
  for(int rle=0;rle<2;rle++){
   used=cursor=0;savelevl(0,rle);memset(levl,0,sizeof levl);rest_levl(0,rle);
   assert(cursor==used);assert(!memcmp(saved,levl,sizeof levl));
  }
 }
 struct rm square,restored;
 assert(sizeof square==6);
 for(int glyph=-1;glyph<=MAX_GLYPH;glyph++){
  memset(&square,0,sizeof square);square.glyph=glyph;square.typ=MAX_TYPE-1;
  square.seenv=255;square.flags=31;square.horizontal=1;square.lit=1;square.waslit=1;
  square.roomno=63;square.edge=1;square.candig=1;
  unsigned char bytes[sizeof square];memcpy(bytes,&square,sizeof square);memcpy(&restored,bytes,sizeof square);
  assert(restored.glyph==glyph&&restored.typ==MAX_TYPE-1&&restored.seenv==255);
  assert(restored.flags==31&&restored.horizontal&&restored.lit&&restored.waslit);
  assert(restored.roomno==63&&restored.edge&&restored.candig);
 }
 struct obj obj,original,expected,disk;
 struct monst mon,moriginal,mexpected,mdisk;
 struct oextra ox;struct mextra mx;memset(&ox,0,sizeof ox);memset(&mx,0,sizeof mx);
 for(unsigned bits=0;bits<32;bits++){
  memset(&obj,0,sizeof obj);obj.o_id=0x12345678;obj.otyp=3;obj.quan=19;obj.spe=-3;obj.blessed=1;obj.cursed=1;
  obj.nobj=&obj;obj.ocarry=&mon;obj.cobj=(bits&1)?&obj:0;obj.oextra=(bits&2)?&ox:0;
  original=obj;expected=obj;expected.nobj=expected.nexthere=0;
  expected.cobj=obj.cobj?(struct obj*)1:0;expected.oextra=obj.oextra?(struct oextra*)1:0;
  used=0;saveobj(0,&obj);assert(!memcmp(&original,&obj,sizeof obj));
  int size;memcpy(&size,output,sizeof size);assert(size==sizeof obj);memcpy(&disk,output+sizeof size,sizeof disk);assert(!memcmp(&disk,&expected,sizeof disk));
  memset(&mon,0,sizeof mon);mon.m_id=0x23456789;mon.mnum=25;mon.mhp=37;mon.mhpmax=49;mon.mpeaceful=1;
  mon.nmon=&mon;mon.data=(struct permonst*)(uintptr_t)0x10123456;
  mon.minvent=(bits&4)?&obj:0;mon.mw=(bits&8)?&obj:0;mon.mextra=(bits&16)?&mx:0;
  moriginal=mon;mexpected=mon;mexpected.nmon=0;mexpected.data=0;
  mexpected.minvent=mon.minvent?(struct obj*)1:0;mexpected.mw=mon.mw?(struct obj*)1:0;mexpected.mextra=mon.mextra?(struct mextra*)1:0;
  used=0;savemon(0,&mon);assert(!memcmp(&moriginal,&mon,sizeof mon));
  memcpy(&size,output,sizeof size);assert(size==sizeof mon);memcpy(&mdisk,output+sizeof size,sizeof mdisk);assert(!memcmp(&mdisk,&mexpected,sizeof mdisk));
 }
 puts("PASS map RLE/raw round trips (uniform, distinct, 254/255 boundaries), six-byte squares across all glyphs; 32 object/monster save-record combinations: canonical links, presence markers, scalars and live records preserved");return 0;
}
'''
c=out/'record_test.c';c.write_text(code);exe=out/'record_test';host=here.parent/'port/host'
subprocess.run([sys.executable,str(host/'host32_cc.py'),'-I'+str(src),'-I'+str(src/'include'),str(c),'-o',str(exe)],check=True)
subprocess.run([sys.executable,str(host/'host32_run.py'),str(exe)],check=True)
