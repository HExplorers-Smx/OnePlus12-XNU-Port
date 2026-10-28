/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef WAFFLE_SMMU_PT_H
#define WAFFLE_SMMU_PT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Software ARMv8 LPAE stage-1 4 KiB page-table builder, 32-bit IOVA and
 * at most 36-bit PA. No SMMU access, no TCR/MAIR/ACTLR programming, no TLBI.
 * A future XNU provider MUST prove page ownership, physical/cache coherence,
 * context bank, stream and device quiescence before installing these tables.
 */
enum { WA_PT_BYTES=4096, WA_PT_ENTRIES=512, WA_PT_MAX_PAGES=256 };
enum wa_pt_result {
  WA_PT_OK=1, WA_PT_MISSING=0, WA_PT_ARGUMENT=-1, WA_PT_NO_PAGES=-2,
  WA_PT_CONFLICT=-3, WA_PT_CORRUPT=-4, WA_PT_FROZEN=-5
};
enum wa_pt_access { WA_PT_READONLY=1, WA_PT_READWRITE=2 };
struct wa_pt_page {
  uint64_t pa;         /* provider-confirmed owned 4 KiB table physical page */
  uint64_t *entries;   /* mapped and owned 4096-byte CPU view of that page */
};
struct wa_pt_pool {
  struct wa_pt_page *pages;
  size_t capacity, used, mappings;
  unsigned sealed, quarantined;
};
int wa_pt_init(struct wa_pt_pool *pool,struct wa_pt_page *pages,size_t count);
int wa_pt_preflight_page(const struct wa_pt_pool *pool,uint64_t iova,uint64_t pa,
                         enum wa_pt_access access);
int wa_pt_map_page(struct wa_pt_pool *pool,uint64_t iova,uint64_t pa,
                   enum wa_pt_access access);
int wa_pt_lookup(const struct wa_pt_pool *pool,uint64_t iova,
                 uint64_t *pa,enum wa_pt_access *access);
/* Traverses every reachable page and rejects aliases, orphan tables, bad
 * descriptors and executable data leaves; does not attest hardware access. */
int wa_pt_audit(const struct wa_pt_pool *pool,size_t *mappings);
/* Sealing prevents edits but does not install the root into the device. */
int wa_pt_seal(struct wa_pt_pool *pool,uint64_t *root_pa);
#ifdef __cplusplus
}
#endif
#endif
