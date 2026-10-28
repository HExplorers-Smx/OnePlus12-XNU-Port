/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "waffle_s8_preboot.h"
#include "waffle_a750_hw_s5.h"
#include <string.h>

static int quarantine(struct wa_s8_preboot *s) {
  if(s) s->phase=WA_S8_QUARANTINED;
  return WA_S8_UNCERTAIN;
}
static int lease_matches(const struct wa_s8_preboot *s) {
  return s && s->lease && s->lease->phase==WA_LEASE_SMMU &&
      s->lease->power_token && s->lease->smmu_token &&
      s->lease->boot_epoch==s->epoch;
}
static int pa_is_table(const struct wa_pt_pool *pool,uint64_t pa) {
  size_t i;
  if(!pool || !pool->pages)return 1;
  for(i=0;i<pool->capacity;i++) if(pool->pages[i].pa==pa) return 1;
  return 0;
}
static int valid_region(const struct wa_s8_preboot *s,
                        const struct wa_s7_region *r) {
  if(!r || !r->owner_token || !r->host || r->bytes!=4096 || !r->pa ||
     (r->pa&4095u) || r->pa>(UINT64_C(1)<<36)-4096 ||
     !r->gfx_iova || !r->gmu_iova || r->gfx_iova>UINT32_MAX ||
     r->gmu_iova>UINT32_MAX || (r->gfx_iova&4095u) ||
     (r->gmu_iova&4095u) || r->gfx_iova==r->gmu_iova ||
     pa_is_table(s->gpu_tables,r->pa) || pa_is_table(s->gmu_tables,r->pa))
    return 0;
  return 1;
}
int wa_s8_init(struct wa_s8_preboot *s,struct wa_pt_pool *gpu,
               struct wa_pt_pool *gmu,struct wa_gpu_lease_runtime *lease,
               uint64_t epoch,const struct wa_s8_runtime_ops *ops,
               const char *board,const char *build,const char *fdt_sha,
               const char *gmu_fw_sha) {
  if(!s || !gpu || !gmu || gpu==gmu || !lease || !epoch ||
     lease->phase!=WA_LEASE_IDLE || !ops || !ops->claim ||
     !ops->publish_backing || !ops->reclaim_unmapped ||
     !ops->publish_tables || !ops->attest_runtime ||
     !waffle_a750_s5_match_snapshot(board,build,fdt_sha,gmu_fw_sha) ||
     gpu->sealed || gmu->sealed || gpu->quarantined || gmu->quarantined ||
     !gpu->pages || !gmu->pages ||
     wa_pt_audit(gpu,0)!=WA_PT_OK || wa_pt_audit(gmu,0)!=WA_PT_OK)
    return WA_S8_REFUSED;
  memset(s,0,sizeof(*s));
  s->ops=*ops;s->gpu_tables=gpu;s->gmu_tables=gmu;
  s->lease=lease;s->epoch=epoch;
  return WA_S8_OK;
}
int wa_s8_prepare(struct wa_s8_preboot *s) {
  struct wa_s7_region r={0};
  size_t j;
  int result;
  if(!s || s->phase!=WA_S8_EMPTY || !s->epoch ||
     !s->gpu_tables || !s->gmu_tables || !s->lease ||
     s->lease->phase!=WA_LEASE_IDLE)return WA_S8_REFUSED;
  result=s->ops.claim(s->ops.context,s->epoch,&r);
  if(result==0 && !r.owner_token && !r.host && !r.pa &&
     !r.gfx_iova && !r.gmu_iova && !r.bytes)return WA_S8_REFUSED;
  if(result!=1 || !valid_region(s,&r))return quarantine(s);
  s->region=r;s->claimed=1;
  if(wa_pt_preflight_page(s->gpu_tables,r.gfx_iova,r.pa,WA_PT_READWRITE)!=WA_PT_OK ||
     wa_pt_preflight_page(s->gmu_tables,r.gmu_iova,r.pa,WA_PT_READWRITE)!=WA_PT_OK) {
    if(s->ops.reclaim_unmapped(s->ops.context,&r)!=1)return quarantine(s);
    s->claimed=0;memset(&s->region,0,sizeof(s->region));
    return WA_S8_REFUSED;
  }
  /* Publish the backing while GPU/GMU are still quiesced, before making any
   * page descriptors. A failed cache clean is always ambiguous. */
  memset(r.host,0,4096);
  for(j=0;j<4096;j++) if(r.host[j])return quarantine(s);
  if(s->ops.publish_backing(s->ops.context,&r)!=1)return quarantine(s);
  s->backing_published=1;
  if(wa_pt_map_page(s->gpu_tables,r.gfx_iova,r.pa,WA_PT_READWRITE)!=WA_PT_OK ||
     wa_pt_map_page(s->gmu_tables,r.gmu_iova,r.pa,WA_PT_READWRITE)!=WA_PT_OK)
    return quarantine(s);
  if(wa_pt_seal(s->gpu_tables,&s->gpu_root)!=WA_PT_OK ||
     wa_pt_seal(s->gmu_tables,&s->gmu_root)!=WA_PT_OK)return quarantine(s);
  if(s->ops.publish_tables(s->ops.context,s->gpu_tables,s->gpu_root)!=1 ||
     s->ops.publish_tables(s->ops.context,s->gmu_tables,s->gmu_root)!=1)
    return quarantine(s);
  s->phase=WA_S8_PREPARED;
  return WA_S8_OK;
}
/* Intent range belongs to the caller's real GMU table-page owner. Merely
 * having the TTBR root somewhere in range is insufficient: every reachable
 * table page must be inside the same proven allocation range. */
static int gmu_table_range_owned(const struct wa_pt_pool *p,
                                 const struct wa_gpu_smmu_intent *i) {
  size_t n;
  uint64_t end;
  if(!p || !i || !wa_gpu_smmu_intent_valid(i) ||
     i->owned_table_bytes>UINT64_MAX-i->owned_table_pa)return 0;
  end=i->owned_table_pa+i->owned_table_bytes;
  for(n=0;n<p->used;n++) {
    uint64_t pa=p->pages[n].pa;
    if(pa<i->owned_table_pa || pa>end-4096u)return 0;
  }
  return 1;
}
static int root_ok(const struct wa_pt_pool *p,uint64_t root,
                   uint64_t iova,uint64_t pa) {
  uint64_t actual=0;
  enum wa_pt_access a=0;
  return p && p->sealed && !p->quarantined && p->pages[0].pa==root &&
     wa_pt_audit(p,0)==WA_PT_OK &&
     wa_pt_lookup(p,iova,&actual,&a)==WA_PT_OK &&
     actual==pa && a==WA_PT_READWRITE;
}
int wa_s8_activate(struct wa_s8_preboot *s,
                   const struct wa_gpu_lease_ops *ops,
                   const struct wa_gpu_smmu_intent *intent) {
  uint64_t token=0;
  int rc;
  if(!s || !ops || !intent || s->phase!=WA_S8_PREPARED ||
     !s->claimed || !s->backing_published ||
     intent->boot_epoch!=s->epoch || intent->ttbr0_pa!=s->gmu_root ||
     intent->gmu_sid!=WAFFLE_A750_SID_GMU ||
     intent->gpu_sid!=WAFFLE_A750_SID_USER ||
     intent->secure_sid!=WAFFLE_A750_SID_SECURE ||
     !gmu_table_range_owned(s->gmu_tables,intent) ||
     !root_ok(s->gpu_tables,s->gpu_root,s->region.gfx_iova,s->region.pa) ||
     !root_ok(s->gmu_tables,s->gmu_root,s->region.gmu_iova,s->region.pa))
    return WA_S8_REFUSED;
  rc=wa_gpu_claim_power(s->lease,ops,s->epoch);
  if(rc==WA_LEASE_BLOCKED)return WA_S8_REFUSED;
  if(rc!=WA_LEASE_OK)return quarantine(s);
  rc=wa_gpu_claim_smmu(s->lease,ops,intent);
  if(rc==WA_LEASE_BLOCKED) {
    /* SMMU definitively denied; lease helper already released power. */
    return WA_S8_REFUSED;
  }
  if(rc!=WA_LEASE_OK)return quarantine(s);
  rc=s->ops.attest_runtime(s->ops.context,s->epoch,s->gpu_root,
                           s->gmu_root,s->lease->power_token,
                           s->lease->smmu_token,&token);
  if(rc!=1 || !token)return quarantine(s);
  s->runtime_token=token;s->phase=WA_S8_ACTIVE;
  return WA_S8_OK;
}
static int still_live(struct wa_s8_preboot *s) {
  uint64_t token=0;
  int rc;
  if(!s || s->phase!=WA_S8_ACTIVE || !s->runtime_token ||
     !lease_matches(s))return 0;
  rc=s->ops.attest_runtime(s->ops.context,s->epoch,s->gpu_root,
                           s->gmu_root,s->lease->power_token,
                           s->lease->smmu_token,&token);
  if(rc==1 && token==s->runtime_token)return 1;
  quarantine(s);return 0;
}
static int reserve(void *context,const struct wa_hfi_request *req,
                   uint32_t bytes,struct wa_hfi_owned_block *out) {
  struct wa_s8_preboot *s=context;
  if(!s || !req || !out || !still_live(s) || req->kind!=2u ||
     bytes!=4096u || req->bytes!=4096u || s->offer_assigned ||
     (req->flags & (WA_HFI_GFX_ACC|WA_HFI_GMU_ACC))!=
       (WA_HFI_GFX_ACC|WA_HFI_GMU_ACC) ||
     (req->flags&WA_HFI_SECURE) || req->gpu_align_bytes>4096u ||
     req->size_align_bytes>4096u)return 0;
  s->offer_assigned=1;
  out->token=s->region.owner_token;
  out->gpu_iova=s->region.gfx_iova;
  out->gmu_iova=s->region.gmu_iova;
  out->bytes=s->region.bytes;
  out->effective_flags=req->effective_flags;
  return 1;
}
static int publish(void *context,const struct wa_hfi_owned_block *block) {
  struct wa_s8_preboot *s=context;
  if(!s || !block || !still_live(s) || !s->backing_published ||
     block->token!=s->region.owner_token ||
     block->gpu_iova!=s->region.gfx_iova ||
     block->gmu_iova!=s->region.gmu_iova ||block->bytes!=4096u)return -1;
  /* NEVER zero a page after device start. It was published during PREPARE. */
  s->reply_count++;
  return 1;
}
static int release(void *context,const struct wa_hfi_owned_block *block) {
  struct wa_s8_preboot *s=context;
  (void)block;
  /* A reply may have become visible to firmware. Can't reclaim without
   * actual quiescence, unmap, TLBI, and ownership proof. */
  quarantine(s);return -1;
}
struct wa_hfi_provider wa_s8_hfi_provider(struct wa_s8_preboot *s) {
  struct wa_hfi_provider p={s,reserve,publish,release};
  return p;
}

static int provider_is_same(const struct wa_s8_preboot *s,
                            const struct wa_s6_ops *ops) {
  struct wa_hfi_provider own;
  if(!s || !ops || !ops->lease || ops->lease!=s->lease)return 0;
  own=wa_s8_hfi_provider((struct wa_s8_preboot *)s);
  return ops->allocator.context==own.context &&
     ops->allocator.reserve==own.reserve &&
     ops->allocator.publish==own.publish &&
     ops->allocator.release==own.release;
}
int wa_s8_attach_s6(struct wa_s8_preboot *s,struct wa_s6_runtime *runtime,
                    const struct wa_s6_ops *ops,uint64_t epoch) {
  if(!s || !runtime || !provider_is_same(s,ops) || epoch!=s->epoch ||
     !still_live(s))return WA_S6_BLOCKED;
  return wa_s6_attach(runtime,ops,epoch);
}
int wa_s8_pump_s6(struct wa_s8_preboot *s,struct wa_s6_runtime *runtime,
                  const struct wa_s6_ops *ops) {
  int result;
  if(!s || !runtime || !provider_is_same(s,ops))return WA_S6_BLOCKED;
  if(!still_live(s))return WA_S6_QUARANTINED;
  result=wa_s6_pump_one(runtime,ops);
  if(!still_live(s))return WA_S6_QUARANTINED;
  return result;
}
