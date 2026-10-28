/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "waffle_s8_preboot.h"
#include "waffle_a750_hw_s5.h"
#include "waffle_hfi_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct tables {
  struct wa_pt_page pages[12];
  struct wa_pt_pool pool;
};
static void table_create(struct tables *t,uint64_t base) {
  memset(t,0,sizeof(*t));
  for(size_t i=0;i<12;i++) {
    t->pages[i].pa=base+4096*i;
    t->pages[i].entries=aligned_alloc(4096,4096);
    assert(t->pages[i].entries);
  }
  assert(wa_pt_init(&t->pool,t->pages,12)==WA_PT_OK);
}
static void table_free(struct tables *t) {
  for(size_t i=0;i<12;i++)free(t->pages[i].entries);
}
struct mock {
  struct wa_s7_region r;
  uint64_t epoch;
  int fail_claim,fail_publish,fail_table,fail_attest;
  int fail_power,fail_smmu,fail_readback;
  unsigned claim_count,reclaim_count,mem_publish_count,table_count,
           power_count,smmu_count,attest_count;
};
static int claim(void *ctx,uint64_t epoch,struct wa_s7_region *r) {
  struct mock *m=ctx;
  ++m->claim_count;
  if(epoch!=m->epoch)return 0;
  if(m->fail_claim)return m->fail_claim;
  *r=m->r;return 1;
}
static int publish_mem(void *ctx,const struct wa_s7_region *r) {
  struct mock *m=ctx;
  assert(r->owner_token==m->r.owner_token);
  ++m->mem_publish_count;
  for(unsigned i=0;i<4096;i++)assert(r->host[i]==0);
  return m->fail_publish?0:1;
}
static int reclaim(void *ctx,const struct wa_s7_region *r) {
  struct mock *m=ctx;assert(r->owner_token==m->r.owner_token);
  ++m->reclaim_count;return 1;
}
static int tables_publish(void *ctx,const struct wa_pt_pool *p,uint64_t root) {
  struct mock *m=ctx;
  ++m->table_count;
  assert(p->sealed && p->pages[0].pa==root);
  assert(wa_pt_audit(p,0)==WA_PT_OK);
  return m->fail_table?0:1;
}
static int hw_attest(void *ctx,uint64_t epoch,uint64_t gpu_root,
                     uint64_t gmu_root,uint64_t power,uint64_t smmu,
                     uint64_t *token) {
  struct mock *m=ctx;
  ++m->attest_count;
  if(m->fail_attest || epoch!=m->epoch || gpu_root!=0x81000000 ||
     gmu_root!=0x82000000 || power!=11 || smmu!=22)return 0;
  *token=33;return 1;
}
static int power_claim(void *ctx,uint64_t epoch,uint64_t *token) {
  struct mock *m=ctx;++m->power_count;
  if(epoch!=m->epoch || m->fail_power)return 0;
  *token=11;return 1;
}
static int power_read(void *ctx,uint64_t token) {
  struct mock *m=ctx;
  return !m->fail_readback && token==11;
}
static int smmu_claim(void *ctx,const struct wa_gpu_smmu_intent *p,
                      uint64_t power,uint64_t *token) {
  struct mock *m=ctx;++m->smmu_count;
  if(m->fail_smmu || power!=11 || p->ttbr0_pa!=0x82000000 ||
     p->gmu_sid!=5 || p->gpu_sid!=0)return 0;
  *token=22;return 1;
}
static int smmu_read(void *ctx,uint64_t token,
                     const struct wa_gpu_smmu_intent *p) {
  struct mock *m=ctx;
  return !m->fail_readback && token==22 && p->ttbr0_pa==0x82000000;
}
static int power_release(void *ctx,uint64_t token) {
  (void)ctx;assert(token==11);return 1;
}
static int smmu_release(void *ctx,uint64_t token) {
  (void)ctx;assert(token==22);return 1;
}
struct fixture {
  struct tables gpu,gmu;
  struct mock mock;
  struct wa_gpu_lease_runtime lease;
  struct wa_s8_preboot run;
  struct wa_s8_runtime_ops ops;
  struct wa_gpu_lease_ops lease_ops;
  struct wa_gpu_smmu_intent intent;
  unsigned char *host;
};
static void make(struct fixture *f) {
  memset(f,0,sizeof(*f));
  table_create(&f->gpu,0x81000000);table_create(&f->gmu,0x82000000);
  f->host=aligned_alloc(4096,4096);assert(f->host);
  memset(f->host,0x7f,4096);
  f->mock.epoch=42;
  f->mock.r=(struct wa_s7_region){&f->mock,f->host,0x8c000000,
                                    0x61000000,0x62000000,4096};
  f->ops=(struct wa_s8_runtime_ops){&f->mock,claim,publish_mem,reclaim,
                                    tables_publish,hw_attest};
  f->lease_ops=(struct wa_gpu_lease_ops){&f->mock,power_claim,power_read,
                  smmu_claim,smmu_read,smmu_release,power_release};
  f->intent=(struct wa_gpu_smmu_intent){42,0x82000000,0x82000000,4096*12,
                                       5,2,0,0,1,4096,1};
  wa_gpu_lease_init(&f->lease);
  assert(wa_s8_init(&f->run,&f->gpu.pool,&f->gmu.pool,&f->lease,42,
     &f->ops,waffle_a750_s5_pins.board,waffle_a750_s5_pins.android_build,
     waffle_a750_s5_pins.runtime_fdt_sha256,
     waffle_a750_s5_pins.gmu_firmware_sha256)==WA_S8_OK);
}
static void finish(struct fixture *f) {
  table_free(&f->gpu);table_free(&f->gmu);free(f->host);
}
static void request(uint32_t *m,uint32_t handle) {
  uint32_t a[11]={20u|(11u<<8),1,0,0,0x33f,2,0,handle,0,4096,0};
  memcpy(m,a,sizeof a);
}
static void success(void) {
  struct fixture f;
  struct wa_hfi_provider p;
  struct wa_hfi_allocator alloc;
  uint32_t req[11],ack[11];size_t words=0;
  uint64_t pa=0;enum wa_pt_access access=0;
  make(&f);
  assert(wa_s8_prepare(&f.run)==WA_S8_OK);
  assert(f.run.phase==WA_S8_PREPARED && f.mock.table_count==2);
  assert(f.gpu.pool.sealed && f.gmu.pool.sealed);
  assert(wa_pt_lookup(&f.gpu.pool,0x61000000,&pa,&access)==WA_PT_OK &&
         pa==0x8c000000 && access==WA_PT_READWRITE);
  assert(wa_pt_map_page(&f.gpu.pool,0x61001000,0x8c001000,WA_PT_READWRITE)==WA_PT_FROZEN);
  assert(wa_s8_activate(&f.run,&f.lease_ops,&f.intent)==WA_S8_OK);
  assert(f.run.phase==WA_S8_ACTIVE && f.run.runtime_token==33);
  p=wa_s8_hfi_provider(&f.run);wa_hfi_allocator_init(&alloc);
  request(req,2);
  assert(wa_hfi_process_alloc(&alloc,&p,req,11,ack,11,&words)==WA_HFI_OK);
  assert(words==11 && ack[2]==0x61000000 && ack[8]==0x62000000);
  assert(f.run.reply_count==1 && f.mock.claim_count==1);
  /* Identical request reuses the provider's ONE backing. */
  assert(wa_hfi_process_alloc(&alloc,&p,req,11,ack,11,&words)==WA_HFI_OK);
  assert(alloc.reused==1 && f.run.reply_count==1);
  request(req,3);
  assert(wa_hfi_process_alloc(&alloc,&p,req,11,ack,11,&words)==WA_HFI_PROVIDER);
  assert(f.run.phase==WA_S8_ACTIVE && f.mock.claim_count==1);
  /* Real-time readback changes: must quarantine without releasing backing. */
  f.mock.fail_attest=1;
  request(req,4);
  assert(wa_hfi_process_alloc(&alloc,&p,req,11,ack,11,&words)==WA_HFI_PROVIDER);
  assert(f.run.phase==WA_S8_QUARANTINED);
  assert(f.mock.reclaim_count==0);
  finish(&f);
}
static void errors(void) {
  struct fixture f;
  make(&f);
  /* Preflight failure is definite, so the backing can be reclaimed before map. */
  assert(wa_pt_map_page(&f.gmu.pool,0x62000000,0x8d000000,WA_PT_READWRITE)==WA_PT_OK);
  assert(wa_s8_prepare(&f.run)==WA_S8_REFUSED);
  assert(f.mock.reclaim_count==1 && !f.run.claimed);
  finish(&f);
  make(&f);
  f.mock.fail_publish=1;
  assert(wa_s8_prepare(&f.run)==WA_S8_UNCERTAIN);
  assert(f.run.phase==WA_S8_QUARANTINED && f.mock.reclaim_count==0);
  finish(&f);
  make(&f);
  f.mock.fail_table=1;
  assert(wa_s8_prepare(&f.run)==WA_S8_UNCERTAIN);
  assert(f.run.phase==WA_S8_QUARANTINED && f.mock.reclaim_count==0);
  finish(&f);
  make(&f);
  assert(wa_s8_prepare(&f.run)==WA_S8_OK);
  f.mock.fail_smmu=1;
  assert(wa_s8_activate(&f.run,&f.lease_ops,&f.intent)==WA_S8_REFUSED);
  assert(f.lease.phase==WA_LEASE_IDLE);
  finish(&f);
  make(&f);
  assert(wa_s8_prepare(&f.run)==WA_S8_OK);
  f.intent.gmu_sid=2;
  assert(wa_s8_activate(&f.run,&f.lease_ops,&f.intent)==WA_S8_REFUSED);
  assert(f.mock.power_count==0 && f.mock.smmu_count==0);
  finish(&f);
  make(&f);
  assert(wa_s8_prepare(&f.run)==WA_S8_OK);
  /* Root is inside the stated span but an allocated child table is NOT. */
  f.intent.owned_table_bytes=4096;
  assert(wa_s8_activate(&f.run,&f.lease_ops,&f.intent)==WA_S8_REFUSED);
  assert(f.mock.power_count==0 && f.mock.smmu_count==0);
  finish(&f);
  make(&f);
  assert(wa_s8_prepare(&f.run)==WA_S8_OK);
  f.mock.fail_readback=1;
  assert(wa_s8_activate(&f.run,&f.lease_ops,&f.intent)==WA_S8_UNCERTAIN);
  assert(f.run.phase==WA_S8_QUARANTINED);
  finish(&f);
}
static int sync_ok(void *x) {(void)x;return 1;}
static int transport_attest(void *p,uint64_t epoch,uint64_t *token) {
  struct mock *m=p;if(epoch!=m->epoch)return 0;
  *token=112233;return 1;
}
static void queue_end_to_end(void) {
  struct fixture f;
  struct wa_s6_runtime run;
  struct wa_s6_ops ops;
  uint32_t req[11],*ram=calloc(1,WA_RING_TOTAL_BYTES);
  assert(ram);
  make(&f);
  assert(wa_s8_prepare(&f.run)==WA_S8_OK);
  assert(wa_s8_activate(&f.run,&f.lease_ops,&f.intent)==WA_S8_OK);
  memset(&ops,0,sizeof(ops));
  ops.lease=&f.lease;
  ops.io=(struct wa_ring_io){ram,WA_RING_TOTAL_BYTES,WA_RING_IOVA,
                               &f.mock,sync_ok,sync_ok};
  ops.allocator=wa_s8_hfi_provider(&f.run);
  ops.attest_context=&f.mock;ops.attest_gmu_hfi=transport_attest;
  assert(wa_ring_initialize(ram,WA_RING_TOTAL_BYTES,WA_RING_IOVA)==WA_RING_OK);
  wa_s6_init(&run);
  assert(wa_s8_attach_s6(&f.run,&run,&ops,42)==WA_S6_PROCESSED);
  request(req,2);
  {
    volatile uint32_t *h=ram+WA_RING_HEADER_WORDS+WA_RING_QUEUE_HEADER_WORDS;
    volatile uint32_t *data=ram+WA_RING_DATA_OFFSET/4+WA_RING_QUEUE_WORDS;
    for(size_t i=0;i<11;i++)data[i]=req[i];
    h[11]=12;
  }
  assert(wa_s8_pump_s6(&f.run,&run,&ops)==WA_S6_PROCESSED);
  assert(run.handled==1 && f.run.reply_count==1);
  assert(ram[WA_RING_DATA_OFFSET/4+2]==0x61000000);
  assert(ram[WA_RING_DATA_OFFSET/4+8]==0x62000000);
  /* S6 allocator caches duplicate requests: stale S8 owner must also be
   * checked on that path (not just in the provider's reserve callback). */
  f.mock.fail_attest=1;
  assert(wa_s8_pump_s6(&f.run,&run,&ops)==WA_S6_QUARANTINED);
  assert(f.run.phase==WA_S8_QUARANTINED);
  /* Synthetic queue ACK: this is NOT a GMU-executed GPU command/fence. */
  free(ram);finish(&f);
}
int main(void) {
  success();errors();queue_end_to_end();
  puts("S8 preboot static SCRATCH + sealed dual SMMU roots + HFI: host checks passed");
  return 0;
}
