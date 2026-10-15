"""Exercise the actual quest fixups for every role/alignment under ARM/newlib."""
import argparse,re,subprocess,sys
from pathlib import Path
here=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build-dir',type=Path,required=True);a=p.parse_args()
work=a.build_dir.resolve();src=work/'source';out=work/'tests';out.mkdir(exist_ok=True)
s=(src/'src/role.c').read_text();roles=re.search(r'const struct Role roles\[\] = \{.*?\n\};',s,re.S).group()
fix=s[s.index('    /* Fix up the quest leader */'):s.index('    /* Fix up the god names */')]
code=r'''#include "compat.h"
#include "hack.h"
#include <assert.h>
#include <stdint.h>
struct Role urole;
struct q_score quest_status;
int rn2(int n){assert(n>0);return 0;}
void panic(const char *fmt,...){(void)fmt;abort();}
'''+roles+r'''
static void fix_traits(int alignmnt){
 const struct permonst *pm;
 nh_quest_traits_init(urole.ldrnum,urole.guardnum,urole.neminum);
'''+fix+r'''
}
static uint32_t table_hash(void){
 const unsigned char *p=(const unsigned char*)mons;
 uint32_t h=2166136261U;
 for(unsigned i=0;i<NUMMONS*sizeof mons[0];i++)h=(h^p[i])*16777619U;
 return h;
}
int main(void){
 unsigned cases=0;uint32_t hash=table_hash();
 for(int repeat=0;repeat<2;repeat++)for(unsigned r=0;roles[r].name.m;r++)for(int a=-1;a<=1;a++){
  urole=roles[r];memset(&quest_status,0,sizeof quest_status);fix_traits(a);
  if(urole.ldrnum!=NON_PM){const struct permonst *p=&mons[urole.ldrnum];assert(NH_PM_SOUND(p)==MS_LEADER);assert(NH_PM_FLAGS2(p)&M2_PEACEFUL);assert(NH_PM_FLAGS3(p)&M3_CLOSE);assert(NH_PM_ALIGN(p)==a*3);assert(p-mons==urole.ldrnum);}
  if(urole.guardnum!=NON_PM){const struct permonst *p=&mons[urole.guardnum];assert(NH_PM_FLAGS2(p)&M2_PEACEFUL);assert(NH_PM_ALIGN(p)==a*3);}
  if(urole.neminum!=NON_PM){const struct permonst *p=&mons[urole.neminum];assert(NH_PM_SOUND(p)==MS_NEMESIS);assert(!(NH_PM_FLAGS2(p)&M2_PEACEFUL));assert((NH_PM_FLAGS2(p)&(M2_NASTY|M2_STALK|M2_HOSTILE))==(M2_NASTY|M2_STALK|M2_HOSTILE));assert(!(NH_PM_FLAGS3(p)&M3_CLOSE));assert((NH_PM_FLAGS3(p)&(M3_WANTSARTI|M3_WAITFORU))==(M3_WANTSARTI|M3_WAITFORU));}
  for(int i=0;i<NUMMONS;i++)if(i!=urole.ldrnum&&i!=urole.guardnum&&i!=urole.neminum){assert(NH_PM_FLAGS2(&mons[i])==mons[i].mflags2);assert(NH_PM_FLAGS3(&mons[i])==mons[i].mflags3);assert(NH_PM_SOUND(&mons[i])==mons[i].msound);assert(NH_PM_ALIGN(&mons[i])==mons[i].maligntyp);}
  assert(table_hash()==hash);cases++;
 }
 printf("PASS %u role/alignment quest fixups: immutable table, unchanged identities and reset traits across roles\n",cases);return 0;
}
'''
c=out/'quest_traits_test.c';c.write_text(code);exe=out/'quest_traits_test';host=here.parent/'port/host'
subprocess.run([sys.executable,str(host/'host32_cc.py'),'-DRP2040_MUTABLE_QUEST=1','-I'+str(src),'-I'+str(src/'include'),str(c),str(src/'src/quest_traits.c'),str(src/'src/monst.c'),'-include',str(src/'compat.h'),'-o',str(exe)],check=True)
subprocess.run([sys.executable,str(host/'host32_run.py'),str(exe)],check=True)
