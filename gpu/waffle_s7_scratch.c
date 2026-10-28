/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "waffle_s7_scratch.h"
#include <string.h>

static int lease_valid(const struct wa_s7_scratch *s) {
  return s && s->lease && s->lease->phase==WA_LEASE_SMMU &&
      s->lease->boot_epoch==s->epoch && s->lease->power_token &&
      s->lease->smmu_token;
}
static int reserved_mmio(const struct wa_pt_pool *p,uint64_t pa) {
  size_t i;
  for(i=0;i<p->capacity;i++)if(p->pages[i].pa==pa)return 1;
  return 0;
}
int wa_s7_scratch_init(struct wa_s7_scratch *s,
                       struct wa_pt_pool *gfx,struct wa_pt_pool *gmu,
                       struct wa_gpu_lease_runtime *lease,uint64_t epoch,
                       const struct wa_s7_memory_ops *backend) {
  if(!s || !gfx || !gmu || !lease || !epoch || gfx==gmu ||
     !backend || !backend->claim || !backend->publish || !backend->reclaim)
    return 0;
  memset(s,0,sizeof(*s));
  s->gfx=gfx;s->gmu=gmu;s->lease=lease;s->epoch=epoch;s->backend=*backend;
  return 1;
}
static int reserve(void *ctx,const struct wa_hfi_request *req,
                   uint32_t bytes,struct wa_hfi_owned_block *out) {
  struct wa_s7_scratch *s=ctx;
  struct wa_s7_region r={0};
  int rc;
  if(!s || !req || !out || s->quarantined || s->claimed ||
     !lease_valid(s) || s->gfx->sealed || s->gmu->sealed ||
     s->gfx->quarantined || s->gmu->quarantined ||
     req->kind!=2u || bytes!=WA_PT_BYTES ||
     !(req->flags&WA_HFI_GFX_ACC) || !(req->flags&WA_HFI_GMU_ACC) ||
     req->flags&WA_HFI_SECURE)return 0;
  rc=s->backend.claim(s->backend.context,s->epoch,&r);
  if(rc==0 && !r.owner_token && !r.host && !r.pa && !r.gfx_iova && !r.gmu_iova)
    return 0;
  if(rc!=1 || !r.owner_token || !r.host || r.bytes!=WA_PT_BYTES ||
     !r.pa || (r.pa&(WA_PT_BYTES-1u)) || r.pa>(UINT64_C(1)<<36)-WA_PT_BYTES ||
     !r.gfx_iova || !r.gmu_iova || r.gfx_iova>UINT32_MAX ||
     r.gmu_iova>UINT32_MAX || (r.gfx_iova&(WA_PT_BYTES-1u)) ||
     (r.gmu_iova&(WA_PT_BYTES-1u)) ||
     reserved_mmio(s->gfx,r.pa) || reserved_mmio(s->gmu,r.pa)) {
    /* Ambiguous claim is never automatically reclaimed. */
    s->quarantined=1;return -1;
  }
  if(wa_pt_preflight_page(s->gfx,r.gfx_iova,r.pa,WA_PT_READWRITE)!=WA_PT_OK ||
     wa_pt_preflight_page(s->gmu,r.gmu_iova,r.pa,WA_PT_READWRITE)!=WA_PT_OK) {
    if(s->backend.reclaim(s->backend.context,&r)!=1) {
      s->quarantined=1;return -1;
    }
    return 0;
  }
  /* Only owned host RAM tables are modified. Caller must serialize access. */
  if(wa_pt_map_page(s->gfx,r.gfx_iova,r.pa,WA_PT_READWRITE)!=WA_PT_OK ||
     wa_pt_map_page(s->gmu,r.gmu_iova,r.pa,WA_PT_READWRITE)!=WA_PT_OK) {
    s->quarantined=1;return -1;
  }
  s->region=r;s->claimed=1;
  out->token=r.owner_token;out->gpu_iova=r.gfx_iova;
  out->gmu_iova=r.gmu_iova;out->bytes=r.bytes;
  out->effective_flags=req->effective_flags;
  return 1;
}
static int publish(void *ctx,const struct wa_hfi_owned_block *block) {
  struct wa_s7_scratch *s=ctx;
  size_t i;
  if(!s || !block || !s->claimed || s->published || s->quarantined ||
     !lease_valid(s) || block->token!=s->region.owner_token)return -1;
  memset(s->region.host,0,s->region.bytes);
  for(i=0;i<s->region.bytes;i++)if(s->region.host[i]) {
    s->quarantined=1;return -1;
  }
  if(s->backend.publish(s->backend.context,&s->region)!=1) {
    s->quarantined=1;return -1;
  }
  s->published=1;return 1;
}
static int release(void *ctx,const struct wa_hfi_owned_block *block) {
  struct wa_s7_scratch *s=ctx;
  (void)block;
  /* Descriptors are now present in the host table, and S7 has no supported
   * rollback/TLBI. Never pretend that memory is no longer visible to DMA. */
  if(s)s->quarantined=1;
  return -1;
}
struct wa_hfi_provider wa_s7_hfi_provider(struct wa_s7_scratch *s) {
  struct wa_hfi_provider p={0};
  p.context=s;p.reserve=reserve;p.publish=publish;p.release=release;
  return p;
}
