#include "waffle_gpu_gate.h"
#include <stddef.h>

static const uint32_t requirements[] = {
    WA_GPU_EV_MATCHED_BOARD | WA_GPU_EV_MMIO_AND_IRQ_VERIFIED,
    WA_GPU_EV_PINNED_FIRMWARE | WA_GPU_EV_AUTHENTICATED_FIRMWARE,
    WA_GPU_EV_POWER_LEASE | WA_GPU_EV_POWER_READBACK,
    WA_GPU_EV_SMMU_LEASE | WA_GPU_EV_SMMU_READBACK,
    WA_GPU_EV_GMU_READY | WA_GPU_EV_HFI_ACK,
    WA_GPU_EV_SUBMITTED_AND_COMPLETED_FENCE | WA_GPU_EV_DMA_STOP_CONFIRM,
    WA_GPU_EV_SCANOUT_PIXEL_MATCH | WA_GPU_EV_IOSURFACE_LIFETIME,
    WA_GPU_EV_METAL_DEVICE_PUBLISHED | WA_GPU_EV_RENDER_SERVER_FRAME
};

void wa_gpu_gate_init(struct wa_gpu_gate *gate) {
    if (gate == NULL) return;
    gate->state = WA_GPU_RESET;
    gate->completed_evidence = 0;
    gate->epoch = 0;
}

int wa_gpu_gate_advance(struct wa_gpu_gate *gate, uint32_t provided_evidence) {
    unsigned int index;
    if (gate == NULL || gate->state == WA_GPU_QUARANTINED || gate->state == WA_GPU_METAL_PROVED) return 0;
    index = (unsigned int)gate->state;
    if (index >= sizeof(requirements) / sizeof(requirements[0])) return 0;
    /* Every step must present exactly its two independent readiness proofs.
     * Do not use stale evidence to advance or assume that a single "ready" bit suffices.
     */
    if (provided_evidence != requirements[index]) return 0;
    gate->completed_evidence |= provided_evidence;
    gate->state = (enum wa_gpu_state)(index + 1);
    return 1;
}

void wa_gpu_gate_quarantine(struct wa_gpu_gate *gate) {
    if (gate == NULL) return;
    gate->state = WA_GPU_QUARANTINED;
}

int wa_gpu_gate_cold_reset(struct wa_gpu_gate *gate, int verified_hardware_cold_reset) {
    if (gate == NULL || verified_hardware_cold_reset != 1) return 0;
    if (gate->epoch == ~0u) return 0;
    gate->state = WA_GPU_RESET;
    gate->completed_evidence = 0;
    gate->epoch++;
    return 1;
}

int wa_gpu_gate_ready(const struct wa_gpu_gate *gate) {
    return gate != NULL && gate->state == WA_GPU_METAL_PROVED;
}
