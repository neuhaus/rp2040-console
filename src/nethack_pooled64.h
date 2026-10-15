#ifndef NH_POOLED64_H
#define NH_POOLED64_H
/* 64-byte slabs for nano-newlib; empty slabs return to the allocator. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
struct nh64_slab {
    union {
        struct { struct nh64_slab *next; uint32_t used; } links;
        uint64_t alignment;
    } header;
    unsigned char slots[32][64];
};
static struct nh64_slab *nh64_slabs;
static struct nh64_slab *nh64_find(void *p, unsigned *slot) {
    struct nh64_slab *s;
    uintptr_t a = (uintptr_t)p;
    for (s=nh64_slabs;s;s=s->header.links.next) {
        uintptr_t start=(uintptr_t)s->slots;
        if(a>=start && a-start<sizeof s->slots && (a-start)%64==0) {
            *slot=(unsigned)((a-start)/64);return s;
        }
    }
    return NULL;
}
static void *nh64_malloc(struct _reent *r, size_t n) {
    struct nh64_slab *s;
    unsigned i;
    if(n!=64) return __real__malloc_r(r,n);
    for(s=nh64_slabs;s;s=s->header.links.next)
        if(s->header.links.used!=UINT32_MAX) break;
    if(!s) {
        s=(struct nh64_slab *)__real__malloc_r(r,sizeof *s);
        /* A slab must not reject a request that a small normal chunk can serve. */
        if(!s) return __real__malloc_r(r,n);
        s->header.links.next=nh64_slabs;s->header.links.used=0;nh64_slabs=s;
    }
    for(i=0;i<32;i++) if(!(s->header.links.used & (UINT32_C(1)<<i))) break;
    assert(i<32);
    s->header.links.used |= UINT32_C(1)<<i;
    return s->slots[i];
}
static void nh64_free(struct _reent *r, void *p) {
    unsigned i;
    struct nh64_slab *s,**link;
    if(!p) return;
    s=nh64_find(p,&i);
    if(!s) {__real__free_r(r,p);return;}
    assert(s->header.links.used & (UINT32_C(1)<<i));
    s->header.links.used &= ~(UINT32_C(1)<<i);
    if(!s->header.links.used) {
        for(link=&nh64_slabs;*link!=s;link=&(*link)->header.links.next) {}
        *link=s->header.links.next;
        __real__free_r(r,s);
    }
}
static void *nh64_realloc(struct _reent *r, void *p, size_t n) {
    unsigned i;
    void *q;
    if(!p) return nh64_malloc(r,n);
    if(!nh64_find(p,&i)) return __real__realloc_r(r,p,n);
    /* Nano-newlib frees a non-NULL allocation on resize to zero. */
    if(!n) { nh64_free(r,p); return NULL; }
    if(n==64) return p;
    q=nh64_malloc(r,n);
    if(!q) return NULL;
    memcpy(q,p,n<64?n:64);nh64_free(r,p);return q;
}
#endif
