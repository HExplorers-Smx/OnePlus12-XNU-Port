/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef WAFFLE_HFI_RUNTIME_H
#define WAFFLE_HFI_RUNTIME_H
#include "waffle_gpu_lease.h"
#include "waffle_hfi_alloc.h"
#include "waffle_hfi_ring.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* This is a fully functioning SOFTWARE queue/allocator coordinator, not a
 * bootable hardware backend. A real XNU implementation must supply: exclusive
 * live GPU SMMU/power leases; verified firmware/HFI ownership; cache sync;
 * GPU/GMU mapping provider; interrupt dispatch; fault and shutdown handling.
 */
enum wa_s6_status { WA_S6_PROCESSED=1, WA_S6_EMPTY=0,
                    WA_S6_BLOCKED=-1, WA_S6_QUARANTINED=-2 };
struct wa_s6_ops {
  struct wa_gpu_lease_runtime *lease;
  struct wa_ring_io io;
  struct wa_hfi_provider allocator;
  void *attest_context;
  /* Returns 1 only for this boot: exclusive ring mapping, verified GMU
   * transport-ready plus authenticated firmware. Token cannot be fabricated.
   * Return 0 if not proven, negative for uncertain hardware ownership.
   */
  int (*attest_gmu_hfi)(void *context,uint64_t epoch,uint64_t *token);
};
struct wa_s6_runtime {
  struct wa_hfi_allocator allocator;
  uint64_t epoch, transport_token;
  unsigned handled, rejected, quarantined, attached;
};
void wa_s6_init(struct wa_s6_runtime *state);
int wa_s6_attach(struct wa_s6_runtime *state,const struct wa_s6_ops *ops,
                 uint64_t boot_epoch);
/* Never indicate hardware-ready based on a mock provider or an ACK queued in
 * host RAM. No hardware completion, real fence or GPU execute implied.
 */
int wa_s6_pump_one(struct wa_s6_runtime *state,const struct wa_s6_ops *ops);
/* Does not release DMA-visible memory. Quarantine is sticky until externally
 * confirmed cold reset and ownership reclamation, outside this API. */
#ifdef __cplusplus
}
#endif
#endif
