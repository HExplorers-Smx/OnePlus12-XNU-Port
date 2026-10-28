/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "waffle_smmu_pt.h"
#include "waffle_s7_scratch.h"
#include "waffle_hfi_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fixture {
  struct wa_pt_page page[12];
  struct wa_pt_pool pool;
};
static void init_pages(struct fixture *f,uint64_t base) {
  size_t i;
  memset(f,0,sizeof(*f));
  for(i=0;i<12;i++) {
    f->page[i].pa=base+(uint64_t)i*WA_PT_BYTES;
    f->page[i].entries=aligned_alloc(WA_PT_BYTES,WA_PT_BYTES);
    assert(f->page[i].entries);
  }
  assert(wa_pt_init(&f->pool,f->page,12)==WA_PT_OK);
}
static void done_pages(struct fixture *f) {
  size_t i;
  for(i=0;i<12;i++)free(f->page[i].entries);
}
struct owner {
  struct wa_s7_region offered;
  unsigned claims,publishes,reclaims;
  int fail_publish,fail_claim;
};
static int claim(void *p,uint64_t epoch,struct wa_s7_region *r) {
  struct owner *o=p;
  assert(epoch==42);
  ++o->claims;
  if(o->fail_claim)return 0;
  *r=o->offered;
  return 1;
}
static int cache_publish(void *p,const struct wa_s7_region *r) {
  struct owner *o=p;
  assert(r->pa==o->offered.pa);
  ++o->publishes;
  return o->fail_publish?0:1;
}
static int reclaim(void *p,const struct wa_s7_region *r) {
  struct owner *o=p;
  assert(r->owner_token==o->offered.owner_token);
  ++o->reclaims;
  return 1;
}
static void packet(uint32_t *p) {
  uint32_t a[11]={20u|(11u<<8),1,0,0,0x33f,2,0,2,0,4096,0};
  memcpy(p,a,sizeof a);
}
static void page_table_tests(void) {
  struct fixture f;
  uint64_t pa=0,root=0,broken;
  enum wa_pt_access mode=0;
  size_t n=0;
  init_pages(&f,0x80000000);
  assert(wa_pt_preflight_page(&f.pool,0x61000000,0x8c000000,WA_PT_READWRITE)==WA_PT_OK);
  assert(wa_pt_map_page(&f.pool,0x61000000,0x8c000000,WA_PT_READWRITE)==WA_PT_OK);
  assert(wa_pt_map_page(&f.pool,0x61001000,0x8c001000,WA_PT_READONLY)==WA_PT_OK);
  assert(wa_pt_map_page(&f.pool,0x61200000,0x8c002000,WA_PT_READWRITE)==WA_PT_OK);
  assert(wa_pt_map_page(&f.pool,0xa0000000,0x8c010000,WA_PT_READWRITE)==WA_PT_OK);
  assert(wa_pt_map_page(&f.pool,0x61000000,0x8c003000,WA_PT_READWRITE)==WA_PT_CONFLICT);
  assert(wa_pt_map_page(&f.pool,0x61000100,0x8c004000,WA_PT_READWRITE)==WA_PT_ARGUMENT);
  assert(wa_pt_map_page(&f.pool,UINT64_C(0x100000000),0x8c005000,WA_PT_READWRITE)==WA_PT_ARGUMENT);
  assert(wa_pt_map_page(&f.pool,0x61003000,UINT64_C(0x1000000000),WA_PT_READWRITE)==WA_PT_ARGUMENT);
  assert(wa_pt_lookup(&f.pool,0x61000321,&pa,&mode)==WA_PT_OK);
  assert(pa==0x8c000321 && mode==WA_PT_READWRITE);
  assert(wa_pt_lookup(&f.pool,0x61001000,&pa,&mode)==WA_PT_OK && mode==WA_PT_READONLY);
  assert(wa_pt_lookup(&f.pool,0x61004000,&pa,&mode)==WA_PT_MISSING);
  assert(wa_pt_audit(&f.pool,&n)==WA_PT_OK && n==4);
  assert(wa_pt_seal(&f.pool,&root)==WA_PT_OK && root==0x80000000);
  assert(wa_pt_map_page(&f.pool,0x61003000,0x8c006000,WA_PT_READWRITE)==WA_PT_FROZEN);
  /* Independent walker detects an executable/unknown leaf descriptor. */
  done_pages(&f);
  init_pages(&f,0x80000000);
  assert(wa_pt_map_page(&f.pool,0x61000000,0x8c000000,WA_PT_READWRITE)==WA_PT_OK);
  broken=f.page[2].entries[(0x61000000u>>12)&511u];
  f.page[2].entries[(0x61000000u>>12)&511u]&=~(UINT64_C(1)<<53);
  assert(wa_pt_audit(&f.pool,&n)==WA_PT_CORRUPT);
  f.page[2].entries[(0x61000000u>>12)&511u]=broken;
  assert(wa_pt_audit(&f.pool,&n)==WA_PT_OK);
  done_pages(&f);
  init_pages(&f,0x80000000);
  /* Two table pages cannot host L1 + L2 + L3 for a new 4 KiB leaf. */
  assert(wa_pt_init(&f.pool,f.page,2)==WA_PT_OK);
  assert(wa_pt_map_page(&f.pool,0x61000000,0x8c000000,WA_PT_READWRITE)==WA_PT_NO_PAGES);
  assert(f.pool.mappings==0 && f.page[0].entries[1]==0);
  done_pages(&f);
}
static void scratch_test(int fail_publish,int conflict) {
  struct fixture gf,gm;
  struct owner o={0};
  struct wa_gpu_lease_runtime lease={WA_LEASE_SMMU,42,11,22};
  struct wa_s7_scratch scratch;
  struct wa_s7_memory_ops ops={&o,claim,cache_publish,reclaim};
  struct wa_hfi_allocator allocator;
  struct wa_hfi_provider p;
  uint32_t msg[11],reply[11];
  size_t words=0;
  uint64_t pa=0;
  enum wa_pt_access access=0;
  int rc;
  unsigned char memory[WA_PT_BYTES];
  init_pages(&gf,0x81000000);init_pages(&gm,0x82000000);
  memset(memory,0xcc,sizeof memory);
  o.offered=(struct wa_s7_region){&o,memory,0x8c000000,0x61000000,0x62000000,4096};
  o.fail_publish=fail_publish;
  assert(wa_s7_scratch_init(&scratch,&gf.pool,&gm.pool,&lease,42,&ops));
  p=wa_s7_hfi_provider(&scratch);
  wa_hfi_allocator_init(&allocator);packet(msg);
  if(conflict)assert(wa_pt_map_page(&gm.pool,0x62000000,0x8d000000,WA_PT_READWRITE)==WA_PT_OK);
  rc=wa_hfi_process_alloc(&allocator,&p,msg,11,reply,11,&words);
  if(conflict) {
    assert(rc==WA_HFI_PROVIDER && !words && o.reclaims==1);
    assert(!scratch.quarantined && !scratch.claimed);
    assert(gf.pool.mappings==0);
  } else if(fail_publish) {
    assert(rc==WA_HFI_PUBLISH_AMBIGUOUS && allocator.quarantined && scratch.quarantined);
    assert(o.publishes==1 && scratch.claimed && !scratch.published);
    assert(gf.pool.mappings==1 && gm.pool.mappings==1);
  } else {
    assert(rc==WA_HFI_OK && words==11);
    assert(reply[2]==0x61000000 && reply[8]==0x62000000 && reply[9]==4096);
    assert(o.claims==1 && o.publishes==1 && scratch.claimed && scratch.published);
    for(size_t i=0;i<sizeof memory;i++)assert(memory[i]==0);
    assert(wa_pt_lookup(&gf.pool,0x61000000,&pa,&access)==WA_PT_OK && pa==0x8c000000);
    assert(wa_pt_lookup(&gm.pool,0x62000000,&pa,&access)==WA_PT_OK && pa==0x8c000000);
    assert(wa_pt_audit(&gf.pool,0)==WA_PT_OK);
    assert(wa_pt_audit(&gm.pool,0)==WA_PT_OK);
    assert(wa_hfi_process_alloc(&allocator,&p,msg,11,reply,11,&words)==WA_HFI_OK);
    assert(o.claims==1 && allocator.reused==1);
    lease.boot_epoch=43;
    /* A new boot epoch cannot be used to reserve a fresh scratch region. */
    wa_hfi_allocator_init(&allocator);
    assert(wa_hfi_process_alloc(&allocator,&p,msg,11,reply,11,&words)==WA_HFI_PROVIDER);
  }
  done_pages(&gf);done_pages(&gm);
}
static int sync_ok(void *p) {(void)p;return 1;}
static int attest_ok(void *p,uint64_t epoch,uint64_t *token) {
  (void)p;if(epoch!=42)return 0;*token=5566;return 1;
}
static void s6_s7_end_to_end_host(void) {
  struct fixture gf,gm;
  struct owner o={0};
  struct wa_gpu_lease_runtime lease={WA_LEASE_SMMU,42,11,22};
  struct wa_s7_scratch scratch;
  struct wa_s7_memory_ops memops={&o,claim,cache_publish,reclaim};
  struct wa_s6_runtime run;
  struct wa_s6_ops transport;
  uint32_t msg[11];
  uint32_t *ring_ram=calloc(1,WA_RING_TOTAL_BYTES);
  uint64_t pa=0;
  enum wa_pt_access mode=0;
  unsigned char owned[4096];
  assert(ring_ram);
  init_pages(&gf,0x81000000);init_pages(&gm,0x82000000);
  o.offered=(struct wa_s7_region){&o,owned,0x8c000000,0x61000000,0x62000000,4096};
  assert(wa_s7_scratch_init(&scratch,&gf.pool,&gm.pool,&lease,42,&memops));
  transport.lease=&lease;
  transport.io=(struct wa_ring_io){ring_ram,WA_RING_TOTAL_BYTES,
                                     WA_RING_IOVA,0,sync_ok,sync_ok};
  transport.allocator=wa_s7_hfi_provider(&scratch);
  transport.attest_context=0;transport.attest_gmu_hfi=attest_ok;
  assert(wa_ring_initialize(ring_ram,WA_RING_TOTAL_BYTES,WA_RING_IOVA)==WA_RING_OK);
  wa_s6_init(&run);
  assert(wa_s6_attach(&run,&transport,42)==WA_S6_PROCESSED);
  packet(msg);
  {
    volatile uint32_t *hdr=ring_ram+WA_RING_HEADER_WORDS+WA_RING_QUEUE_HEADER_WORDS;
    volatile uint32_t *data=ring_ram+WA_RING_DATA_OFFSET/4+WA_RING_QUEUE_WORDS;
    for(unsigned i=0;i<11;i++)data[i]=msg[i];
    hdr[11]=12;
  }
  assert(wa_s6_pump_one(&run,&transport)==WA_S6_PROCESSED);
  assert(run.handled==1 && scratch.published && o.publishes==1);
  assert(wa_pt_lookup(&gf.pool,0x61000000,&pa,&mode)==WA_PT_OK && pa==0x8c000000);
  assert(wa_pt_lookup(&gm.pool,0x62000000,&pa,&mode)==WA_PT_OK && pa==0x8c000000);
  assert(ring_ram[WA_RING_DATA_OFFSET/4+2]==0x61000000);
  assert(ring_ram[WA_RING_DATA_OFFSET/4+8]==0x62000000);
  /* This is a SOFTWARE ACK, not evidence that GMU processed anything. */
  done_pages(&gf);done_pages(&gm);free(ring_ram);
}
int main(void) {
  page_table_tests();
  scratch_test(0,0);
  scratch_test(1,0);
  scratch_test(0,1);
  s6_s7_end_to_end_host();
  puts("S7 GPU/GMU 4K stage-1 software tables and SCRATCH backing: host checks passed");
  return 0;
}
