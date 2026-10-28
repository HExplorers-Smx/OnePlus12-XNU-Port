#ifndef WAFFLE_GPU_GATE_H
#define WAFFLE_GPU_GATE_H
#include <stdint.h>

/* Portable, side-effect-free bring-up gate used by a future XNU platform provider.
 * This is NOT an Adreno hardware driver and never accesses MMIO, firmware, DMA or SMMU.
 * Each successful step requires external hardware evidence from a separate provider.
 */
#ifdef __cplusplus
extern "C" {
#endif

enum wa_gpu_state {
  WA_GPU_RESET = 0,
  WA_GPU_DT_VERIFIED,
  WA_GPU_FIRMWARE_VERIFIED,
  WA_GPU_POWER_OWNED,
  WA_GPU_SMMU_OWNED,
  WA_GPU_GMU_HFI_READY,
  WA_GPU_COMMAND_FENCE_PROVED,
  WA_GPU_SCANOUT_PROVED,
  WA_GPU_METAL_PROVED,
  WA_GPU_QUARANTINED
};

enum wa_gpu_evidence {
  WA_GPU_EV_MATCHED_BOARD = (1u << 0),
  WA_GPU_EV_MMIO_AND_IRQ_VERIFIED = (1u << 1),
  WA_GPU_EV_PINNED_FIRMWARE = (1u << 2),
  WA_GPU_EV_AUTHENTICATED_FIRMWARE = (1u << 3),
  WA_GPU_EV_POWER_LEASE = (1u << 4),
  WA_GPU_EV_POWER_READBACK = (1u << 5),
  WA_GPU_EV_SMMU_LEASE = (1u << 6),
  WA_GPU_EV_SMMU_READBACK = (1u << 7),
  WA_GPU_EV_GMU_READY = (1u << 8),
  WA_GPU_EV_HFI_ACK = (1u << 9),
  WA_GPU_EV_SUBMITTED_AND_COMPLETED_FENCE = (1u << 10),
  WA_GPU_EV_DMA_STOP_CONFIRM = (1u << 11),
  WA_GPU_EV_SCANOUT_PIXEL_MATCH = (1u << 12),
  WA_GPU_EV_IOSURFACE_LIFETIME = (1u << 13),
  WA_GPU_EV_METAL_DEVICE_PUBLISHED = (1u << 14),
  WA_GPU_EV_RENDER_SERVER_FRAME = (1u << 15)
};

struct wa_gpu_gate {
  enum wa_gpu_state state;
  uint32_t completed_evidence;
  unsigned int epoch;
};

void wa_gpu_gate_init(struct wa_gpu_gate *gate);
/* Caller provides evidence only after hardware validation. Fail closed: missing/extra bits denied. */
int wa_gpu_gate_advance(struct wa_gpu_gate *gate, uint32_t provided_evidence);
/* Uncertain DMA or unknown failure means quarantined until next cold reset. */
void wa_gpu_gate_quarantine(struct wa_gpu_gate *gate);
/* Reset is permitted only after independently verified cold reset; not software cleanup. */
int wa_gpu_gate_cold_reset(struct wa_gpu_gate *gate, int verified_hardware_cold_reset);
int wa_gpu_gate_ready(const struct wa_gpu_gate *gate);
#ifdef __cplusplus
}
#endif
#endif
