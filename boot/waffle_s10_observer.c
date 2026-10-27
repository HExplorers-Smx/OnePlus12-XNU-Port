/* SPDX-License-Identifier: MIT */
#include "waffle_s10_observer.h"
void wa_s10_observer_init(struct wa_s10_observer *s,uint64_t epoch)
{
    if(!s) return;
    s->boot_epoch=epoch;s->last_serial=0;s->quarantined=0;s->accepted=0;
}
int wa_s10_observer_accept(struct wa_s10_observer *s,
                           const struct wa_s10_observation *o)
{
    if(!s||!o||s->quarantined) return WA_S10_BLOCKED;
    if(!s->boot_epoch || !o->current_boot || o->boot_epoch!=s->boot_epoch ||
       !o->read_only || !o->exclusive_power || !o->exclusive_smmu ||
       !o->clocks_enabled_confirmed || !o->mmio_mapped_by_owner ||
       !o->runtime_owner_confirmed || !o->access_complete ||
       !o->power_owner_token || !o->smmu_owner_token ||
       (o->reported_device!=WA_S10_GPUCC && o->reported_device!=WA_S10_SMMU)) {
        /* Once a hardware observation might have happened, missing ownership
         * proof is a quarantine; don't try again with stale tokens. */
        if(o->access_complete || s->accepted) s->quarantined=1;
        return s->quarantined?WA_S10_QUARANTINE:WA_S10_BLOCKED;
    }
    if(!o->observation_serial || o->observation_serial<=s->last_serial) {
        s->quarantined=1;return WA_S10_QUARANTINE;
    }
    s->last_serial=o->observation_serial;s->accepted++;
    return WA_S10_READY;
}
