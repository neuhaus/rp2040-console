from pathlib import Path
import re,subprocess,argparse,sys
root=Path(__file__).resolve().parent
p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,required=True);a=p.parse_args()
out=a.build_dir.resolve()/'tests';out.mkdir(exist_ok=True);r=a.build_dir.resolve()/'source'
s=(r/'src/sp_lev.c').read_text();h=(r/'include/sp_lev.h').read_text();decl=(r/'include/decl.h').read_text()
funcs='\n'.join(re.search(pattern,s,re.S).group() for pattern in [r'static void\nnh_sp_read\(.*?\n\}',r'STATIC_OVL boolean\nsp_level_loader\(.*?\n\}',r'STATIC_OVL boolean\nsp_level_free\(.*?\n\}'])
enum=re.search(r'enum opcode_defs \{.*?\n\};',h,re.S).group()
var=re.search(r'struct opvar \{.*?\n\};',decl,re.S).group()
defines='\n'.join('#define '+n+' '+v for n,v in re.findall(r'#define (SPOVAR_\w+)\s+(?:\\\n\s*)?(0x[0-9a-fA-F]+)',h))
names=sorted(p.name for p in (r/'dat').glob('*.lev'))
code=r'''#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#define STATIC_OVL static
#define TRUE 1
typedef int boolean;
typedef unsigned char xchar;
typedef void *genericptr_t;
typedef FILE dlb;
#define dlb_fread fread
#define dlb_fseek fseek
#define dlb_ftell ftell
static int live;
static void *alloc(size_t n) {void *p=malloc(n?n:1);assert(p);live++;return p;}
static void release(void *p) {if(p){assert(live>0);live--;free(p);}}
#define Free release
static void panic(const char *message,...) {fprintf(stderr,"%s\n",message);exit(2);}
'''+enum+'\n'+defines+'\n'+var+'''
typedef struct {int opcode;struct opvar *opdat;} _opcode;
typedef struct {_opcode *opcodes;long n_opcodes;} sp_lev;
'''+funcs+'\nstatic const char *names[] = {'+','.join('"'+n+'"' for n in names)+'};\n'+r'''
int main(int argc,char **argv) {
 assert(sizeof(long)==4);assert(argc==2);
 for(unsigned f=0;f<sizeof names/sizeof names[0];f++) {
  char path[512];snprintf(path,sizeof path,"%s/%s",argv[1],names[f]);
  FILE *fd=fopen(path,"rb");assert(fd);assert(fseek(fd,20,SEEK_SET)==0);
  sp_lev program;void *arena=NULL;assert(sp_level_loader(fd,&program,&arena));
  assert(live==(arena?2:1));assert(fseek(fd,20,SEEK_SET)==0);
  long count;assert(fread(&count,4,1,fd)==1);assert(program.n_opcodes==count);
  for(long i=0;i<count;i++) {
   int opcode;assert(fread(&opcode,4,1,fd)==1);assert(program.opcodes[i].opcode==opcode);
   struct opvar *ov=program.opcodes[i].opdat;
   if(opcode!=SPO_PUSH) {assert(!ov);continue;}
   xchar type;assert(fread(&type,1,1,fd)==1);assert(ov&&ov->spovartyp==type);
   if(type==SPOVAR_NULL) {assert(ov->vardata.l==0);continue;}
   if(type==SPOVAR_STRING||type==SPOVAR_VARIABLE||type==SPOVAR_SEL) {
    int length;assert(fread(&length,4,1,fd)==1);assert(length>=0);
    for(int j=0;j<length;j++) {int c=fgetc(fd);assert(c!=EOF);assert((unsigned char)ov->vardata.str[j]==c);}
    assert(ov->vardata.str[length]==0);
   } else {
    long value;assert(fread(&value,4,1,fd)==1);assert(ov->vardata.l==value);
   }
  }
  assert(fgetc(fd)==EOF);fclose(fd);sp_level_free(&program,arena);assert(live==0);
 }
 printf("PASS: %u full level programs match every opcode, operand and string; all arenas freed\n",(unsigned)(sizeof names/sizeof names[0]));
 return 0;
}
'''
p=out/'operand_arena_test.c';p.write_text(code)
subprocess.run([sys.executable,str(root.parent/'port/host/host32_cc.py'),str(p),'-o',str(out/'operand_arena_test')],check=True)
subprocess.run([sys.executable,str(root.parent/'port/host/host32_run.py'),str(out/'operand_arena_test'),'dat'],cwd=r,check=True)
