/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Cross-stage integration test. Includes S8 host fixture, never device MMIO.
 */
#define main s8_embedded_main
#include "host_s8_preboot_test.c"
#undef main
#include "waffle_s9_runtime_guard.h"

struct observer {
  struct fixture *f;
  unsigned calls,fail_on_call,fail_clock,fail_secure,wrong_bank,wrong_root;
  int return_code;
};
static int s9_observe_mock(void *ctx,uint64_t epoch,struct wa_s9_observation *o) {
  struct observer *p=ctx;
  struct fixture *f=p->f;
  ++p->calls;
  if(p->return_code || p->calls==p->fail_on_call)return p->return_code?p->return_code:0;
  memset(o,0,sizeof(*o));
  o->epoch=epoch;
  o->serial=p->calls;
  o->power_token=f->lease.power_token;
  o->smmu_token=f->lease.smmu_token;
  o->runtime_token=f->run.runtime_token;
  o->exclusive_power_aop_clock_lease=1;
  o->exclusive_smmu_lease=1;
  o->power_clock_readback_good=!p->fail_clock;
  o->sid2_reserved_for_secure=!p->fail_secure;
  o->no_unmatched_stream_mappings=1;
  o->dma_quiescence_policy_known=1;
  o->gpu=(struct wa_s9_smmu_context){0,1,1,4096,f->run.gpu_root,
                                     1,1,1,1,1};
  o->gmu=(struct wa_s9_smmu_context){5,p->wrong_bank?1u:2u,2,4096,
                                     p->wrong_root?0xabad0000:f->run.gmu_root,
                                     1,1,1,1,1};
  return 1;
}
static void setup(struct fixture *f,struct wa_s9_guard *g) {
  make(f);
  assert(wa_s8_prepare(&f->run)==WA_S8_OK);
  assert(wa_s8_activate(&f->run,&f->lease_ops,&f->intent)==WA_S8_OK);
  wa_s9_guard_init(g,&f->run);
}
static void probes(void) {
  struct fixture f;
  struct wa_s9_guard g;
  struct observer observer={0};
  struct wa_s9_probe_ops ops={&observer,s9_observe_mock};
  setup(&f,&g);observer.f=&f;
  assert(wa_s9_observe(&g,0)==WA_S9_BLOCKED);
  assert(wa_s9_observe(&g,&ops)==WA_S9_OK);
  assert(g.admitted && observer.calls==1);
  observer.fail_clock=1;
  assert(wa_s9_observe(&g,&ops)==WA_S9_UNCERTAIN);
  assert(g.quarantined && f.run.phase==WA_S8_QUARANTINED);
  assert(f.mock.reclaim_count==0);
  finish(&f);
  setup(&f,&g);observer=(struct observer){&f,0,0,0,1,0,0,0};
  assert(wa_s9_observe(&g,&ops)==WA_S9_UNCERTAIN);finish(&f);
  setup(&f,&g);observer=(struct observer){&f,0,0,0,0,1,0,0};
  assert(wa_s9_observe(&g,&ops)==WA_S9_UNCERTAIN);finish(&f);
  setup(&f,&g);observer=(struct observer){&f,0,0,0,0,0,1,0};
  assert(wa_s9_observe(&g,&ops)==WA_S9_UNCERTAIN);finish(&f);
  setup(&f,&g);observer=(struct observer){&f,0,0,0,0,0,0,0};
  observer.return_code=0;
  /* Empty hardware observer cannot open the guard. */
  observer.fail_on_call=1;
  assert(wa_s9_observe(&g,&ops)==WA_S9_BLOCKED);
  observer.fail_on_call=0;
  assert(wa_s9_observe(&g,&ops)==WA_S9_OK);
  observer.return_code=-1;
  assert(wa_s9_observe(&g,&ops)==WA_S9_UNCERTAIN);finish(&f);
}
static void guarded_queue(void) {
  struct fixture f;
  struct wa_s9_guard guard;
  struct observer obs={0};
  struct wa_s9_probe_ops observer={&obs,s9_observe_mock};
  struct wa_s6_runtime rt;
  struct wa_s6_ops ops;
  uint32_t request_words[11];
  uint32_t *ram=calloc(1,WA_RING_TOTAL_BYTES);
  assert(ram);
  setup(&f,&guard);obs.f=&f;
  memset(&ops,0,sizeof(ops));
  ops.lease=&f.lease;
  ops.io=(struct wa_ring_io){ram,WA_RING_TOTAL_BYTES,WA_RING_IOVA,
                            &f.mock,sync_ok,sync_ok};
  ops.allocator=wa_s8_hfi_provider(&f.run);
  ops.attest_context=&f.mock;ops.attest_gmu_hfi=transport_attest;
  assert(wa_ring_initialize(ram,WA_RING_TOTAL_BYTES,WA_RING_IOVA)==WA_RING_OK);
  wa_s6_init(&rt);
  assert(wa_s8_attach_s6(&f.run,&rt,&ops,42)==WA_S6_PROCESSED);
  request(request_words,2);
  {
    volatile uint32_t *h=ram+WA_RING_HEADER_WORDS+WA_RING_QUEUE_HEADER_WORDS;
    volatile uint32_t *data=ram+WA_RING_DATA_OFFSET/4+WA_RING_QUEUE_WORDS;
    for(unsigned i=0;i<11;i++)data[i]=request_words[i];
    h[11]=12;
  }
  assert(wa_s9_pump_s6(&guard,&observer,&rt,&ops)==WA_S6_PROCESSED);
  assert(rt.handled==1 && obs.calls==2);
  assert(ram[WA_RING_DATA_OFFSET/4+2]==0x61000000);
  assert(ram[WA_RING_DATA_OFFSET/4+8]==0x62000000);
  /* Second run: post-send observation fails, ACK may already be DMA-visible. */
  obs.fail_on_call=obs.calls+2;
  assert(wa_s9_pump_s6(&guard,&observer,&rt,&ops)==WA_S9_UNCERTAIN);
  assert(guard.quarantined && f.run.phase==WA_S8_QUARANTINED);
  assert(f.mock.reclaim_count==0);
  free(ram);finish(&f);
}
int main(void) {
  probes();guarded_queue();
  puts("S9 runtime readback guard + double-checked HFI: host checks passed");
  return 0;
}
