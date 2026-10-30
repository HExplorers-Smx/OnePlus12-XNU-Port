/* SPDX-License-Identifier: MIT */
#include "waffle_s12_xnup.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void set32(unsigned char *b,unsigned x){b[0]=x;b[1]=x>>8;b[2]=x>>16;b[3]=x>>24;}
int main(int argc,char **argv) {
    assert(argc==2);
    FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fseek(f,0,SEEK_END)==0);
    long n=ftell(f);assert(n>0 && n<2*1024*1024);
    rewind(f);
    unsigned char *b=malloc((size_t)n);assert(b);
    assert(fread(b,1,(size_t)n,f)==(size_t)n);fclose(f);
    struct wa_s12_view v;
    assert(wa_s12_xnup_parse(b,(size_t)n,&v)==WA_S12_OK);
    assert(v.entries==2 && v.kernel_bytes>=32);
    unsigned char saved=b[0];b[0]=0;
    assert(wa_s12_xnup_parse(b,(size_t)n,&v)==WA_S12_INVALID);b[0]=saved;
    assert(wa_s12_xnup_parse(b,(size_t)n-1,&v)==WA_S12_BOUNDS);
    set32(b+4,0xffffffffu);
    assert(wa_s12_xnup_parse(b,(size_t)n,&v)==WA_S12_BOUNDS);
    free(b);puts("s12 XNUP checks passed");return 0;
}
