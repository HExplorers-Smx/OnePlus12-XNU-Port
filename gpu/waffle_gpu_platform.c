#include "waffle_gpu_platform.h"
#include <stddef.h>

int wa_gpu_platform_step(struct wa_gpu_gate *gate,
                         const struct wa_gpu_platform_ops *ops, void *ctx) {
  int (*callback)(void *) = NULL;
  int result;
  if (gate == NULL || ops == NULL) return 0;
  if (gate->state >= WA_GPU_METAL_PROVED) return 0;
  switch (gate->state) {
    case WA_GPU_RESET: callback=ops->verify_board_and_resources; break;
    case WA_GPU_DT_VERIFIED: callback=ops->verify_signed_firmware; break;
    case WA_GPU_FIRMWARE_VERIFIED: callback=ops->claim_power_and_confirm; break;
    case WA_GPU_POWER_OWNED: callback=ops->claim_smmu_and_confirm; break;
    case WA_GPU_SMMU_OWNED: callback=ops->start_gmu_and_confirm_hfi; break;
    case WA_GPU_GMU_HFI_READY: callback=ops->submit_and_confirm_fence; break;
    case WA_GPU_COMMAND_FENCE_PROVED: callback=ops->display_scanout_and_validate; break;
    case WA_GPU_SCANOUT_PROVED: callback=ops->publish_metal_and_verify_frame; break;
    default: return 0;
  }
  if (callback == NULL) return 0;
  result = callback(ctx);
  if (result == 0) return 0;
  if (result < 0 || !wa_gpu_gate_advance(gate, (uint32_t)result)) {
    wa_gpu_gate_quarantine(gate);
    return -1;
  }
  return 1;
}
