#!/usr/bin/env python3
"""Test actual patched zero-run adapters and framed codec under ASan/UBSan."""
import argparse
from pathlib import Path
import subprocess
import tempfile
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--source',type=Path,required=True)
a=p.parse_args()
root=Path(__file__).resolve().parents[3]
port=root/'applications/nethack-3.1.3/port'
tamp=root/'applications/nethack-3.6.7/port/tamp'
src=a.source.resolve()
write=(src/'src/save.c').read_text().split('#ifdef ZEROCOMP\n/* The runs')[1].split('#else /* ZEROCOMP */')[0]
write=write[write.index('#include "../tamp-codec.h"'):].replace('#include "../tamp-codec.h"','#include "tamp-codec.h"')
read=(src/'src/restore.c').read_text().split('#ifdef ZEROCOMP\n#include "../tamp-codec.h"')[1].split('#else /* ZEROCOMP */')[0]
uptodate=(src/'sys/unix/unixunix.c').read_text().split('int\nuptodate(fd)')[1].split('/* see whether')[0]
uptodate='int\nuptodate(fd)'+uptodate
pre='''#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "tamp-codec.h"
#define BUFSZ 256
#define NEARDATA
#define genericptr_t void *
#define panic nh_panic
static void bputc(int);
void nh_panic(const char *s, ...) { va_list a; va_start(a,s); vfprintf(stderr,s,a); va_end(a); fputc('\\n',stderr); exit(99); }
static struct stat buf,hbuf;
void pline(const char *s,...) { (void)s; }
void wait_synch(void) {}
static int short_write;
int test_write(int fd,const void *buf,size_t n) { if(short_write) return n ? n-1 : 0; return write(fd,buf,n); }
'''
main=r'''
static unsigned char data[12000], back[12000];
static void verify(int fd, unsigned n) {
    unsigned pos=0;
    if(lseek(fd,0,SEEK_SET)!=0 || !nh_codec_valid(fd)) abort();
    minit();
    while(pos<n) {
        unsigned k = 1 + (pos*13u)%937u;
        if(k>n-pos) k=n-pos;
        if(mread(fd,back+pos,k)) abort();
        pos+=k;
    }
    if(memcmp(data,back,n) || mread(fd,back,1)!=-1) abort();
}
int main(int argc,char **argv) {
    int fd=open(argv[2],O_RDWR|O_CREAT|O_TRUNC,0600), out;
    unsigned n,i,mode,cases=0,seed=12345;
    if(!strcmp(argv[1],"badwrite")) { short_write=1; bufon(fd); return 2; }
    if(!strcmp(argv[1],"decode")) {
        close(fd); fd=open(argv[3],O_RDONLY); minit();
        while(!nh_codec_read(fd,back,1)) {} return 0;
    }
    bufon(fd); bwrite(fd,"format",6); bflush(fd); nh_codec_end(fd);
    lseek(fd,0,SEEK_SET); hbuf.st_mtime=(time_t)4102444800LL;
    if(!uptodate(fd) || lseek(fd,0,SEEK_CUR)!=0 || uptodate(-1)) abort();
    ftruncate(fd,0); lseek(fd,0,SEEK_SET); write(fd,"legacy save",11); lseek(fd,0,SEEK_SET);
    if(uptodate(fd)) abort();
    puts("PASS: actual uptodate accepts a valid save with a future placeholder timestamp and rejects legacy formats");
    for(mode=0;mode<4;++mode) for(n=0;n<=12000;n=n<1050?n+1:n+991) {
        for(i=0;i<n;++i) {
            seed=seed*1664525u+1013904223u;
            data[i]=mode==0?0:mode==1?i:mode==2?(seed>>24):((i%400)<300?0:(i%17));
        }
        if(ftruncate(fd,0)||lseek(fd,0,SEEK_SET)) abort();
        bufon(fd);
        for(i=0;i<n;) {
            unsigned k=1+(i*7u)%513u;
            if(k>n-i) k=n-i;
            bwrite(fd,data+i,k); i+=k;
            if((i%7)==0) bflush(fd);
        }
        bflush(fd); nh_codec_end(fd);
        verify(fd,n); verify(fd,n); cases+=2;
    }
    /* Read a cached level while appending its decoded records to a save. */
    out=open(argv[3],O_RDWR|O_CREAT|O_TRUNC,0600);
    bufon(out); lseek(fd,0,SEEK_SET); minit();
    for(i=0;i<n-991;) { /* n advanced past the last payload length */
        unsigned k=113;
        if(k>n-991-i) k=n-991-i;
        if(mread(fd,back,k)) abort();
        bwrite(out,back,k); i+=k;
    }
    bflush(out); nh_codec_end(out);
    verify(out,n-991);
    close(fd); close(out);
    printf("PASS: %u zero-run/codec round trips, reset/rewind and interleaved save copying\n",cases);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    d=Path(d); fixture=d/'test.c';fixture.write_text(pre+write+read+uptodate+main)
    obj=d/'codec.o'; exe=d/'codec'
    common=['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-DTAMP_USE_EMBEDDED_MATCH=1','-DTAMP_STREAM=0','-fsanitize=address,undefined','-fno-omit-frame-pointer','-Wno-deprecated-non-prototype','-g','-O1','-I'+str(port),'-I'+str(tamp)]
    subprocess.run(common+['-Dwrite=test_write','-c',str(port/'tamp-codec.c'),'-o',str(obj)],check=True)
    subprocess.run(common+[str(fixture),str(obj),*[str(tamp/(n+'.c')) for n in ['common','compressor','decompressor']],'-o',str(exe)],check=True)
    def run(mode,other,expected=0):
        r=subprocess.run([str(exe),mode,str(d/'work'),str(other)],capture_output=True,text=True)
        if r.returncode!=expected: raise RuntimeError((mode,r.returncode,r.stdout,r.stderr))
        return r
    r=run('roundtrip',d/'stream');print(r.stdout,end='')
    run('badwrite',d/'unused',99)
    stream=(d/'stream').read_bytes()
    bad=[]
    for cut in [1,7,9,12,len(stream)-1]: bad.append(stream[:cut])
    for offset in [0,8,10,12,16,len(stream)-1]:
        b=bytearray(stream);b[offset]^=0xff;bad.append(bytes(b))
    for b in bad:
        (d/'bad').write_bytes(b);run('decode',d/'bad',99)
    print('PASS: short-write rejection and',len(bad),'truncation/corruption cases under ASan/UBSan')
