/* SPDX-License-Identifier: MIT
 * Minimal compiler runtime primitives for freestanding PE/COFF.
 * Only linked into the UEFI application, not the host tests.
 */
#include <stddef.h>
void *memset(void *dest,int byte,size_t size){
    unsigned char *p=(unsigned char *)dest;
    for(size_t i=0;i<size;i++)p[i]=(unsigned char)byte;
    return dest;
}
void *memcpy(void *dest,const void *source,size_t size){
    unsigned char *d=(unsigned char *)dest;
    const unsigned char *s=(const unsigned char *)source;
    for(size_t i=0;i<size;i++)d[i]=s[i];
    return dest;
}
