/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "waffle_hfi_alloc.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct fake { unsigned reserved, published, released; int reserve_status;
              int publish_status, release_status, invalid_mode; };
static int reserve_fake(void *context,const struct wa_hfi_request *r,
                        uint32_t pad, struct wa_hfi_owned_block *out) {
  struct fake *f=context;
  ++f->reserved;
  if (f->reserve_status!=1) {
    if (f->invalid_mode==4) out->token=f; /* provider protocol violation */
    return f->reserve_status;
  }
  out->token=f;
  out->effective_flags=r->effective_flags;
  out->bytes=pad;
  if (r->flags & WA_HFI_GFX_ACC) out->gpu_iova=0x61000000;
  if (!(r->flags & WA_HFI_GFX_ACC) || (r->flags & WA_HFI_GMU_ACC))
    out->gmu_iova=0x61000000;
  if (f->invalid_mode==1) out->gmu_iova=0x61000001;
  if (f->invalid_mode==2) out->gmu_iova=0;
  if (f->invalid_mode==3) out->gmu_iova=0x100000000ULL;
  return 1;
}
static int publish_fake(void *ctx,const struct wa_hfi_owned_block *b) {
  struct fake *f=ctx;assert(b->token==f);++f->published;return f->publish_status;
}
static int release_fake(void *ctx,const struct wa_hfi_owned_block *b) {
  struct fake *f=ctx;assert(b->token==f);++f->released;return f->release_status;
}
static void reset(struct wa_hfi_allocator *s,struct fake *f) {
  wa_hfi_allocator_init(s);memset(f,0,sizeof(*f));
  f->reserve_status=f->publish_status=f->release_status=1;
}
static void scratch(uint32_t r[11]) {
  uint32_t q[]={0x00700b14,1,0,0,0x33f,2,0,2,0,4096,0};
  memcpy(r,q,sizeof(q));
}
int main(void) {
  struct fake f;struct wa_hfi_allocator s;
  struct wa_hfi_provider p={&f,reserve_fake,publish_fake,release_fake};
  struct wa_hfi_request parsed;
  uint32_t r[11], ack[12]={0xdeadbeef}; size_t n=777;
  reset(&s,&f);scratch(r);
  assert(wa_hfi_parse_alloc(r,11,&parsed)==WA_HFI_OK);
  assert(parsed.kind==2 && parsed.gmu_handle==2 && parsed.bytes==4096);
  assert(wa_hfi_process_alloc(&s,&p,r,11,ack,12,&n)==WA_HFI_OK);
  assert(n==11 && s.count==1 && f.reserved==1 && f.published==1);
  assert(ack[0]==(20u|(1u<<16)|(11u<<8)));
  assert(ack[1]==r[0] && ack[2]==0x61000000 && ack[8]==0x61000000);
  assert(ack[6]==2 && ack[7]==2 && ack[9]==4096);
  n=777;assert(wa_hfi_process_alloc(&s,&p,r,11,ack,12,&n)==WA_HFI_OK);
  assert(s.reused==1 && f.reserved==1 && s.count==1 && n==11);
  r[9]=8192;n=777;
  assert(wa_hfi_process_alloc(&s,&p,r,11,ack,12,&n)==WA_HFI_CONFLICT);
  assert(n==0 && s.count==1);
  reset(&s,&f);scratch(r);
  assert(wa_hfi_process_alloc(&s,&p,r,11,ack,10,&n)==WA_HFI_INPUT);
  assert(f.reserved==0 && n==0);
  r[0]^=1;
  assert(wa_hfi_parse_alloc(r,11,&parsed)==WA_HFI_HEADER);
  scratch(r);r[4]|=(1u<<12);
  assert(wa_hfi_parse_alloc(r,11,&parsed)==WA_HFI_RESERVED);
  scratch(r);r[4]|=(1u<<11);
  assert(wa_hfi_parse_alloc(r,11,&parsed)==WA_HFI_FLAGS);
  scratch(r);r[5]=22;
  assert(wa_hfi_process_alloc(&s,&p,r,11,ack,12,&n)==WA_HFI_RESERVED);
  assert(f.reserved==0);
  scratch(r);r[10]=31;
  assert(wa_hfi_parse_alloc(r,11,&parsed)==WA_HFI_ALIGNMENT);
  scratch(r);r[10]=0x00000c0c; /* 4096 GPU and size alignment */
  assert(wa_hfi_parse_alloc(r,11,&parsed)==WA_HFI_OK);
  r[10]=0x00001010; /* requested 64KiB alignment */
  f.invalid_mode=1;
  assert(wa_hfi_process_alloc(&s,&p,r,11,ack,12,&n)==WA_HFI_PROVIDER);
  assert(f.released==1 && !s.quarantined && s.count==0);
  f.invalid_mode=0;
  assert(wa_hfi_process_alloc(&s,&p,r,11,ack,12,&n)==WA_HFI_OK);
  assert(n==11 && s.count==1);
  reset(&s,&f);scratch(r);
  f.invalid_mode=1;f.release_status=0;
  assert(wa_hfi_process_alloc(&s,&p,r,11,ack,12,&n)==WA_HFI_RELEASE_AMBIGUOUS);
  assert(s.quarantined && s.count==0 && n==0);
  assert(wa_hfi_process_alloc(&s,&p,r,11,ack,12,&n)==WA_HFI_QUARANTINED);
  reset(&s,&f);scratch(r);f.publish_status=0;
  assert(wa_hfi_process_alloc(&s,&p,r,11,ack,12,&n)==WA_HFI_PUBLISH_AMBIGUOUS);
  assert(s.quarantined && f.released==0 && s.count==0);
  reset(&s,&f);scratch(r);f.reserve_status=-1;
  assert(wa_hfi_process_alloc(&s,&p,r,11,ack,12,&n)==WA_HFI_QUARANTINED);
  assert(s.quarantined && f.published==0);
  reset(&s,&f);scratch(r);f.reserve_status=0;
  assert(wa_hfi_process_alloc(&s,&p,r,11,ack,12,&n)==WA_HFI_PROVIDER);
  assert(!s.quarantined);
  reset(&s,&f);scratch(r);r[4]=0x338; /* GMU-only synthetic request */
  assert(wa_hfi_process_alloc(&s,&p,r,11,ack,12,&n)==WA_HFI_OK);
  assert(ack[2]==0 && ack[3]==0 && ack[8]!=0);
  reset(&s,&f);scratch(r);f.invalid_mode=3;
  assert(wa_hfi_process_alloc(&s,&p,r,11,ack,12,&n)==WA_HFI_PROVIDER);
  reset(&s,&f);scratch(r);f.reserve_status=0;f.invalid_mode=4;
  assert(wa_hfi_process_alloc(&s,&p,r,11,ack,12,&n)==WA_HFI_QUARANTINED);
  assert(s.quarantined && f.published==0);
  reset(&s,&f);scratch(r);r[1]=0; r[0]=0x00700a14;
  assert(wa_hfi_process_alloc(&s,&p,r,10,ack,12,&n)==WA_HFI_OK);
  assert(n==10);
  puts("S4 HFI allocation framing, SCRATCH ack, retry, and failure tests passed");
  return 0;
}
