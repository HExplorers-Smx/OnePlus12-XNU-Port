/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "waffle_hfi_alloc.h"
#include <limits.h>
#include <string.h>

static uint32_t align_of(uint32_t exp) {
  if (exp > 30u) return 0;
  return exp <= 12u ? WA_HFI_PAGE : (uint32_t)1u << exp;
}
static int is_reserved(uint32_t kind) {
  return kind == 5u || kind == 19u || kind == 22u || kind == 23u;
}
static int pad_bytes(uint32_t bytes, uint32_t a, uint32_t *padded) {
  uint64_t p;
  if (!bytes || bytes > WA_HFI_MAX_BYTES || !a || (a&(a-1u))) return 0;
  p = ((uint64_t)bytes + a - 1u) & ~(uint64_t)(a - 1u);
  if (p > WA_HFI_MAX_BYTES || p > UINT32_MAX) return 0;
  *padded = (uint32_t)p;
  return 1;
}
void wa_hfi_allocator_init(struct wa_hfi_allocator *allocator) {
  if (allocator) memset(allocator, 0, sizeof(*allocator));
}
int wa_hfi_parse_alloc(const uint32_t *f, size_t n, struct wa_hfi_request *out) {
  struct wa_hfi_request r;
  if (!f || !out || n < 2) return WA_HFI_INPUT;
  /* id: 0..7, dword count: 8..15, type: 16..19; 20..31: sequence */
  if ((f[0]&0xffu)!=WA_HFI_ALLOC_ID || ((f[0]>>8)&0xffu)!=n ||
      ((f[0]>>16)&15u)!=0u) return WA_HFI_HEADER;
  if (f[1]!=0u && n!=WA_HFI_V1_WORDS) return WA_HFI_HEADER;
  if (f[1]==0u && n!=WA_HFI_V0_WORDS) return WA_HFI_HEADER;
  memset(&r,0,sizeof(r));
  r.header=f[0]; r.version=f[1];
  r.gpu_addr=(uint64_t)f[2]|((uint64_t)f[3]<<32);
  r.flags=f[4];r.kind=f[5];r.host_handle=f[6];r.gmu_handle=f[7];
  r.gmu_addr=f[8];r.bytes=f[9];r.alignment=n==WA_HFI_V1_WORDS?f[10]:0u;
  if (r.kind>=26u) return WA_HFI_KIND;
  if (r.flags&~WA_HFI_KNOWN_FLAGS) return WA_HFI_FLAGS;
  if (r.flags&WA_HFI_SECURE) return WA_HFI_RESERVED;
  if (!r.bytes || r.bytes>WA_HFI_MAX_BYTES) return WA_HFI_SIZE;
  if (r.alignment & 0xffff0000u) return WA_HFI_ALIGNMENT;
  r.gpu_align_bytes=align_of(r.alignment&255u);
  r.size_align_bytes=align_of((r.alignment>>8)&255u);
  if (!r.gpu_align_bytes || !r.size_align_bytes) return WA_HFI_ALIGNMENT;
  r.effective_flags = (r.flags&WA_HFI_GFX_ACC) ? r.flags :
      (r.flags|WA_HFI_GMU_ACC|WA_HFI_GMU_PRIV|WA_HFI_GMU_WRITE);
  *out=r;
  return WA_HFI_OK;
}
static int block_ok(const struct wa_hfi_request *r,
                    const struct wa_hfi_owned_block *b,
                    uint32_t required) {
  int gfx=(r->flags&WA_HFI_GFX_ACC)!=0;
  int gmu=!gfx || (r->flags&WA_HFI_GMU_ACC)!=0;
  if (!b->token || b->bytes<required || b->effective_flags!=r->effective_flags)
    return 0;
  if ((b->gpu_iova&(WA_HFI_PAGE-1u)) || (b->gmu_iova&(WA_HFI_PAGE-1u)))
    return 0;
  if ((gfx && !b->gpu_iova) || (!gfx && b->gpu_iova)) return 0;
  if ((gmu && !b->gmu_iova) || (!gmu && b->gmu_iova)) return 0;
  if (b->gmu_iova>UINT32_MAX) return 0; /* wire field is only 32 bit */
  if (gmu && (b->gmu_iova & (r->gpu_align_bytes-1u))) return 0;
  return 1;
}
static int same_request(const struct wa_hfi_request *a,
                        const struct wa_hfi_request *b) {
  return a->version==b->version && a->flags==b->flags &&
      a->kind==b->kind && a->bytes==b->bytes &&
      a->alignment==b->alignment && a->gmu_handle==b->gmu_handle;
}
static void build_ack(const struct wa_hfi_request *req,
                      const struct wa_hfi_owned_block *block,
                      unsigned seq,uint32_t *out,size_t count) {
  out[0]=WA_HFI_ALLOC_ID|(WA_HFI_ACK_TYPE<<16)|
         ((seq&0xfffu)<<20)|((uint32_t)count<<8);
  out[1]=req->header;
  out[2]=(uint32_t)block->gpu_iova;
  out[3]=(uint32_t)(block->gpu_iova>>32);
  out[4]=req->flags;out[5]=req->kind;
  out[6]=req->gmu_handle;out[7]=req->gmu_handle;
  out[8]=(uint32_t)block->gmu_iova;out[9]=block->bytes;
  if (count==WA_HFI_V1_WORDS)out[10]=req->alignment;
}
int wa_hfi_process_alloc(struct wa_hfi_allocator *s,
                         const struct wa_hfi_provider *p,
                         const uint32_t *frame,size_t words,
                         uint32_t *ack,size_t capacity,size_t *ack_words) {
  struct wa_hfi_request r;
  struct wa_hfi_owned_block b;
  uint32_t padded;
  unsigned i;
  size_t n;
  int status, found=-1, rc;
  if (!ack_words) return WA_HFI_INPUT;
  *ack_words=0;
  if (!s || !p || !p->reserve || !p->publish || !p->release || !frame || !ack)
    return WA_HFI_INPUT;
  if (s->quarantined) return WA_HFI_QUARANTINED;
  status=wa_hfi_parse_alloc(frame,words,&r);
  if (status!=WA_HFI_OK) {++s->refused;return status;}
  n=r.version==0u?WA_HFI_V0_WORDS:WA_HFI_V1_WORDS;
  if (capacity<n) return WA_HFI_INPUT;
  if (is_reserved(r.kind)) {++s->refused;return WA_HFI_RESERVED;}
  if (!pad_bytes(r.bytes,r.size_align_bytes,&padded)) {++s->refused;return WA_HFI_SIZE;}
  for (i=0;i<s->count;i++) {
    if (s->entries[i].request.kind==r.kind &&
        s->entries[i].request.gmu_handle==r.gmu_handle) {found=(int)i;break;}
  }
  if (found>=0) {
    if (!same_request(&s->entries[found].request,&r)) {
      ++s->refused;return WA_HFI_CONFLICT;
    }
    b=s->entries[found].owned;
    ++s->reused;
  } else {
    if (s->count>=WA_HFI_LIMIT) {++s->refused;return WA_HFI_FULL;}
    memset(&b,0,sizeof(b));
    rc=p->reserve(p->context,&r,padded,&b);
    if (rc!=1) {
      /* A provider returning "no allocation" must not leave any token or
       * mapping behind; otherwise it has lost track of DMA ownership. */
      if (rc<0 || b.token || b.gpu_iova || b.gmu_iova || b.bytes)
        s->quarantined=1;
      ++s->refused;
      return s->quarantined?WA_HFI_QUARANTINED:WA_HFI_PROVIDER;
    }
    if (!block_ok(&r,&b,padded)) {
      /* Only a CONFIRMED release makes this path safely retryable. */
      if (p->release(p->context,&b)!=1) {
        s->quarantined=1;
        return WA_HFI_RELEASE_AMBIGUOUS;
      }
      ++s->refused;return WA_HFI_PROVIDER;
    }
    if (p->publish(p->context,&b)!=1) {
      /* Publication may have been visible to DMA; retain until cold reboot. */
      s->quarantined=1;
      return WA_HFI_PUBLISH_AMBIGUOUS;
    }
    s->entries[s->count].request=r;
    s->entries[s->count].owned=b;
    ++s->count;
  }
  build_ack(&r,&b,s->ack_sequence++,ack,n);
  *ack_words=n;
  return WA_HFI_OK;
}
