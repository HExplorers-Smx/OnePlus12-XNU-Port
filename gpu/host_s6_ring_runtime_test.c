/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* CPU-only fault-injection tests, NOT evidence of phone GPU operation. */
#include "waffle_hfi_ring.h"
#include "waffle_hfi_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct fake {
  int sync_fail,cpu_fail,attest_status,reserve_status,publish_status,release_status;
  unsigned sync_count,reserve_count,publish_count,release_count,attest_count;
  uint64_t proof_token;
};
static int sync_device(void *ctx) {struct fake*f=ctx;++f->sync_count;return !f->sync_fail;}
static int sync_cpu(void *ctx) {struct fake*f=ctx;return !f->cpu_fail;}
static int attest(void *ctx,uint64_t epoch,uint64_t *token) {
  struct fake*f=ctx;assert(epoch==42);++f->attest_count;
  if(f->attest_status==1) *token=f->proof_token;
  return f->attest_status;
}
static int reserve_cb(void *ctx,const struct wa_hfi_request *r,uint32_t bytes,
                      struct wa_hfi_owned_block *b) {
  struct fake*f=ctx;++f->reserve_count;
  if(f->reserve_status==1) {
    b->token=f;b->bytes=bytes;b->effective_flags=r->effective_flags;
    if(r->flags&WA_HFI_GFX_ACC)b->gpu_iova=0x61000000u;
    if(!(r->flags&WA_HFI_GFX_ACC)||(r->flags&WA_HFI_GMU_ACC))
      b->gmu_iova=0x61000000u;
  }
  return f->reserve_status;
}
static int publish_cb(void *ctx,const struct wa_hfi_owned_block *b) {
  struct fake*f=ctx;assert(b->token==f);++f->publish_count;return f->publish_status;
}
static int release_cb(void *ctx,const struct wa_hfi_owned_block *b) {
  struct fake*f=ctx;assert(b->token==f);++f->release_count;return f->release_status;
}
static void scratch(uint32_t p[11]) {
  const uint32_t sample[11]={0x00700b14u,1u,0,0,0x33fu,2u,0,2u,0,4096u,0};
  memcpy(p,sample,sizeof(sample));
}
static volatile uint32_t *queue_header(uint32_t *ram,unsigned q) {
  return ram+WA_RING_HEADER_WORDS+q*WA_RING_QUEUE_HEADER_WORDS;
}
static volatile uint32_t *queue_ring(uint32_t *ram,unsigned q) {
  return ram+WA_RING_DATA_OFFSET/4+q*WA_RING_QUEUE_WORDS;
}
static void inject(uint32_t *ram,unsigned q,const uint32_t *msg,unsigned n,
                   unsigned rd) {
  volatile uint32_t *hdr=queue_header(ram,q),*ring=queue_ring(ram,q);
  unsigned i,pad=(n+3u)&~3u;
  assert(q==1 || q==2);
  hdr[10]=rd;
  for(i=0;i<pad;i++)ring[(rd+i)&1023u]=i<n?msg[i]:0xfafafafau;
  hdr[11]=(rd+pad)&1023u;
}
static void good_fake(struct fake*f) {
  memset(f,0,sizeof(*f));
  f->attest_status=f->reserve_status=f->publish_status=f->release_status=1;
  f->proof_token=654321u;
}
int main(void) {
  uint32_t *mem=calloc(1,WA_RING_TOTAL_BYTES+16u);
  struct fake f;
  struct wa_ring_io io;
  struct wa_s6_ops ops;
  struct wa_s6_runtime s;
  struct wa_gpu_lease_runtime lease;
  uint32_t packet[11];size_t n=567;
  uint32_t out[16]={0};
  assert(mem);
  good_fake(&f);
  assert(wa_ring_initialize(mem,WA_RING_TOTAL_BYTES,WA_RING_IOVA)==WA_RING_OK);
  io=(struct wa_ring_io){mem,WA_RING_TOTAL_BYTES,WA_RING_IOVA,&f,sync_device,sync_cpu};
  assert(wa_ring_check(&io,0)==WA_RING_OK);
  assert(wa_ring_check(&io,1)==WA_RING_OK);
  assert(wa_ring_check(&io,2)==WA_RING_OK);
  assert(wa_ring_initialize((uint8_t*)mem+1,WA_RING_TOTAL_BYTES,WA_RING_IOVA)==WA_RING_INVALID);
  assert(wa_ring_initialize(mem,1000,WA_RING_IOVA)==WA_RING_INVALID);
  assert(wa_ring_initialize(mem,WA_RING_TOTAL_BYTES,WA_RING_IOVA+1)==WA_RING_INVALID);
  scratch(packet);
  assert(wa_ring_send(&io,packet,11)==WA_RING_OK);
  assert(queue_header(mem,0)[11]==12u);
  assert(queue_ring(mem,0)[0]==packet[0]);
  assert(wa_ring_send(&io,packet,256)==WA_RING_INVALID);
  assert(wa_ring_receive(&io,0,out,16,&n)==WA_RING_INVALID);
  assert(wa_ring_receive(&io,1,out,16,&n)==WA_RING_EMPTY && n==0);
  /* Full ring must not mutate producer cursor. */
  queue_header(mem,0)[10]=0;queue_header(mem,0)[11]=1016;
  assert(wa_ring_send(&io,packet,11)==WA_RING_FULL);
  assert(queue_header(mem,0)[11]==1016);
  queue_header(mem,0)[10]=20;queue_header(mem,0)[11]=1020;
  assert(wa_ring_send(&io,packet,11)==WA_RING_OK);
  assert(queue_header(mem,0)[11]==8);
  assert(queue_ring(mem,0)[1020]==packet[0]);
  assert(queue_ring(mem,0)[0]==packet[4]);
  assert(wa_ring_initialize(mem,WA_RING_TOTAL_BYTES,WA_RING_IOVA)==WA_RING_OK);
  inject(mem,2,packet,11,1020);
  assert(wa_ring_receive(&io,2,out,16,&n)==WA_RING_OK && n==11);
  assert(!memcmp(packet,out,sizeof(packet)));
  assert(queue_header(mem,2)[10]==8);
  assert(wa_ring_receive(&io,2,out,16,&n)==WA_RING_EMPTY);
  inject(mem,1,packet,11,0);
  queue_ring(mem,1)[0]=0; /* malformed count */
  assert(wa_ring_receive(&io,1,out,16,&n)==WA_RING_CORRUPT);
  assert(queue_header(mem,1)[10]==0);
  assert(wa_ring_initialize(mem,WA_RING_TOTAL_BYTES,WA_RING_IOVA)==WA_RING_OK);
  /* No owned power or SMMU means no queue attach or writes. */
  memset(&lease,0,sizeof(lease));
  ops=(struct wa_s6_ops){&lease,io,{&f,reserve_cb,publish_cb,release_cb},&f,attest};
  wa_s6_init(&s);
  assert(wa_s6_attach(&s,&ops,42)==WA_S6_BLOCKED);
  assert(s.attached==0 && f.reserve_count==0);
  lease.phase=WA_LEASE_SMMU;lease.boot_epoch=42;lease.power_token=12;lease.smmu_token=34;
  f.attest_status=0;
  assert(wa_s6_attach(&s,&ops,42)==WA_S6_BLOCKED && !s.attached);
  f.attest_status=1;
  assert(wa_s6_attach(&s,&ops,43)==WA_S6_BLOCKED);
  assert(wa_s6_attach(&s,&ops,42)==WA_S6_PROCESSED && s.attached);
  assert(wa_s6_pump_one(&s,&ops)==WA_S6_EMPTY);
  inject(mem,1,packet,11,0);
  assert(wa_s6_pump_one(&s,&ops)==WA_S6_PROCESSED);
  assert(s.handled==1 && s.allocator.count==1);
  assert(f.reserve_count==1 && f.publish_count==1);
  assert(queue_header(mem,1)[10]==12 && queue_header(mem,0)[11]==12);
  assert(queue_ring(mem,0)[0]==(20u|(1u<<16)|(11u<<8)));
  assert(queue_ring(mem,0)[2]==0x61000000u);
  assert(queue_ring(mem,0)[8]==0x61000000u);
  assert(queue_ring(mem,0)[6]==2);
  inject(mem,1,packet,11,12);
  assert(wa_s6_pump_one(&s,&ops)==WA_S6_PROCESSED && s.allocator.reused==1);
  assert(f.reserve_count==1 && f.publish_count==1);
  /* Lease revoked after attachment => quarantine; never consume packet. */
  lease.phase=WA_LEASE_QUARANTINED;
  inject(mem,1,packet,11,24);
  assert(wa_s6_pump_one(&s,&ops)==WA_S6_QUARANTINED);
  assert(queue_header(mem,1)[10]==24 && s.quarantined);
  assert(wa_s6_pump_one(&s,&ops)==WA_S6_QUARANTINED);
  /* A dropped cache-publication barrier cannot create an ACK. */
  assert(wa_ring_initialize(mem,WA_RING_TOTAL_BYTES,WA_RING_IOVA)==WA_RING_OK);
  lease.phase=WA_LEASE_SMMU;wa_s6_init(&s);
  assert(wa_s6_attach(&s,&ops,42)==WA_S6_PROCESSED);
  inject(mem,1,packet,11,0);
  f.sync_fail=1;
  assert(wa_s6_pump_one(&s,&ops)==WA_S6_QUARANTINED);
  assert(s.handled==0 && s.quarantined);
  f.sync_fail=0;
  /* No proof of authenticated transport -> hard-stop after attachment. */
  assert(wa_ring_initialize(mem,WA_RING_TOTAL_BYTES,WA_RING_IOVA)==WA_RING_OK);
  wa_s6_init(&s);assert(wa_s6_attach(&s,&ops,42)==WA_S6_PROCESSED);
  f.proof_token++;
  inject(mem,1,packet,11,0);
  assert(wa_s6_pump_one(&s,&ops)==WA_S6_QUARANTINED);
  assert(queue_header(mem,1)[10]==0);
  /* If allocator refuses after RX consumption, session must halt. */
  good_fake(&f);
  assert(wa_ring_initialize(mem,WA_RING_TOTAL_BYTES,WA_RING_IOVA)==WA_RING_OK);
  wa_s6_init(&s);assert(wa_s6_attach(&s,&ops,42)==WA_S6_PROCESSED);
  inject(mem,2,packet,11,0);f.reserve_status=0;
  assert(wa_s6_pump_one(&s,&ops)==WA_S6_QUARANTINED);
  assert(s.handled==0 && s.rejected==1);
  /* Malformed packet cannot be mistaken for empty success. */
  good_fake(&f);
  assert(wa_ring_initialize(mem,WA_RING_TOTAL_BYTES,WA_RING_IOVA)==WA_RING_OK);
  wa_s6_init(&s);assert(wa_s6_attach(&s,&ops,42)==WA_S6_PROCESSED);
  inject(mem,1,packet,11,0);queue_header(mem,1)[10]=2;
  assert(wa_s6_pump_one(&s,&ops)==WA_S6_QUARANTINED);
  assert(s.handled==0);
  free(mem);
  puts("S6 Gen7 17-queue ring and HFI memory-alloc pump: host failure tests passed");
  return 0;
}
