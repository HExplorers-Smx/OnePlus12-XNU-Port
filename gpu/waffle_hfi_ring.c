/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "waffle_hfi_ring.h"
#include <limits.h>
#include <stdint.h>

static unsigned priority(unsigned q) {
  return q<7u?0u:q<11u?1u:q<14u?2u:q<16u?3u:4u;
}
static unsigned round4(unsigned n) {return (n+3u)&~3u;}
int wa_ring_initialize(void *raw,size_t bytes,uint32_t base) {
  uint32_t *w=(uint32_t *)raw;
  unsigned q,i;
  if (!raw || ((uintptr_t)raw&3u) || bytes<WA_RING_TOTAL_BYTES ||
      (base&4095u) || base>UINT32_MAX-WA_RING_TOTAL_BYTES) return WA_RING_INVALID;
  for (i=0;i<WA_RING_TOTAL_BYTES/4;i++) w[i]=0;
  w[1]=WA_RING_TABLE_WORDS;w[2]=WA_RING_HEADER_WORDS;
  w[3]=WA_RING_QUEUE_HEADER_WORDS;w[4]=w[5]=WA_RING_QUEUES;
  for (q=0;q<WA_RING_QUEUES;q++) {
    uint32_t *h=w+WA_RING_HEADER_WORDS+q*WA_RING_QUEUE_HEADER_WORDS;
    h[0]=1u;h[1]=base+WA_RING_DATA_OFFSET+q*4096u;
    h[2]=q|(priority(q)<<8);h[3]=WA_RING_QUEUE_WORDS;
  }
  return WA_RING_OK;
}
static int lookup(const struct wa_ring_io *io,unsigned q,
                  volatile uint32_t **hdr,volatile uint32_t **ring,
                  unsigned *read,unsigned *write) {
  volatile uint32_t *w,*h;
  if(!io || !io->memory || !hdr || !ring || !read || !write ||
     !io->sync_for_device || !io->sync_for_cpu || q>=WA_RING_QUEUES ||
     ((uintptr_t)io->memory&3u) || io->bytes<WA_RING_TOTAL_BYTES ||
     (io->base_iova&4095u) || io->base_iova>UINT32_MAX-WA_RING_TOTAL_BYTES)
    return WA_RING_INVALID;
  if(io->sync_for_cpu(io->context)!=1) return WA_RING_CORRUPT;
  w=io->memory;h=w+WA_RING_HEADER_WORDS+q*WA_RING_QUEUE_HEADER_WORDS;
  if(w[0]!=0u || w[1]!=WA_RING_TABLE_WORDS ||
     w[2]!=WA_RING_HEADER_WORDS || w[3]!=WA_RING_QUEUE_HEADER_WORDS ||
     w[4]!=WA_RING_QUEUES || w[5]!=WA_RING_QUEUES ||
     h[0]!=1u || h[1]!=io->base_iova+WA_RING_DATA_OFFSET+q*4096u ||
     h[2]!=(q|(priority(q)<<8)) || h[3]!=WA_RING_QUEUE_WORDS || h[4]!=0u)
    return WA_RING_CORRUPT;
  *read=h[10];*write=h[11];
  if (*read>=WA_RING_QUEUE_WORDS || *write>=WA_RING_QUEUE_WORDS ||
      ((*read|*write)&3u)) return WA_RING_CORRUPT;
  *hdr=h;*ring=w+WA_RING_DATA_OFFSET/4+q*WA_RING_QUEUE_WORDS;
  return WA_RING_OK;
}
int wa_ring_check(const struct wa_ring_io *io,unsigned q) {
  volatile uint32_t *h,*r;unsigned rd,wr;
  return lookup(io,q,&h,&r,&rd,&wr);
}
int wa_ring_send(const struct wa_ring_io *io,const uint32_t *message,size_t n) {
  volatile uint32_t *h,*ring;unsigned rd,wr,pad,i;int rc;
  if(!message || n==0 || n>WA_RING_MAX_PACKET ||
     ((message[0]>>8)&255u)!=n) return WA_RING_INVALID;
  rc=lookup(io,0,&h,&ring,&rd,&wr);if(rc!=WA_RING_OK)return rc;
  pad=round4((unsigned)n);
  if (((rd-wr-1u)&(WA_RING_QUEUE_WORDS-1u))<pad) return WA_RING_FULL;
  for(i=0;i<pad;i++) ring[(wr+i)&(WA_RING_QUEUE_WORDS-1u)] =
      i<n?message[i]:0xfafafafau;
  /* Never publish producer cursor if data cache publication was not proven. */
  if(io->sync_for_device(io->context)!=1) return WA_RING_CORRUPT;
  h[11]=(wr+pad)&(WA_RING_QUEUE_WORDS-1u);
  if(io->sync_for_device(io->context)!=1) return WA_RING_CORRUPT;
  return WA_RING_OK;
}
int wa_ring_receive(const struct wa_ring_io *io,unsigned q,uint32_t *out,
                    size_t capacity,size_t *count) {
  volatile uint32_t *h,*ring;unsigned rd,wr,n,pad,i;int rc;
  if(!count) return WA_RING_INVALID;
  *count=0;
  if(!out || (q!=1u && q!=2u)) return WA_RING_INVALID;
  rc=lookup(io,q,&h,&ring,&rd,&wr);if(rc!=WA_RING_OK)return rc;
  if(rd==wr)return WA_RING_EMPTY;
  n=(ring[rd]>>8)&255u;pad=round4(n);
  if(!n || n>capacity || n>WA_RING_MAX_PACKET ||
     pad>((wr-rd)&(WA_RING_QUEUE_WORDS-1u))) return WA_RING_CORRUPT;
  for(i=0;i<n;i++)out[i]=ring[(rd+i)&(WA_RING_QUEUE_WORDS-1u)];
  if(io->sync_for_cpu(io->context)!=1) return WA_RING_CORRUPT;
  h[10]=(rd+pad)&(WA_RING_QUEUE_WORDS-1u);
  if(io->sync_for_device(io->context)!=1) return WA_RING_CORRUPT;
  *count=n;
  return WA_RING_OK;
}
