#include "waffle_gpu_gate.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
  struct wa_gpu_gate s;
  wa_gpu_gate_init(&s);
  assert(!wa_gpu_gate_ready(&s));
  assert(!wa_gpu_gate_advance(&s, WA_GPU_EV_MATCHED_BOARD));
  assert(s.state == WA_GPU_RESET);
  const uint32_t ev[] = {
    WA_GPU_EV_MATCHED_BOARD | WA_GPU_EV_MMIO_AND_IRQ_VERIFIED,
    WA_GPU_EV_PINNED_FIRMWARE | WA_GPU_EV_AUTHENTICATED_FIRMWARE,
    WA_GPU_EV_POWER_LEASE | WA_GPU_EV_POWER_READBACK,
    WA_GPU_EV_SMMU_LEASE | WA_GPU_EV_SMMU_READBACK,
    WA_GPU_EV_GMU_READY | WA_GPU_EV_HFI_ACK,
    WA_GPU_EV_SUBMITTED_AND_COMPLETED_FENCE | WA_GPU_EV_DMA_STOP_CONFIRM,
    WA_GPU_EV_SCANOUT_PIXEL_MATCH | WA_GPU_EV_IOSURFACE_LIFETIME,
    WA_GPU_EV_METAL_DEVICE_PUBLISHED | WA_GPU_EV_RENDER_SERVER_FRAME
  };
  for (unsigned i=0;i<sizeof(ev)/sizeof(ev[0]);++i) {
    if (i == 0) {
      assert(!wa_gpu_gate_advance(&s, ev[i] | WA_GPU_EV_HFI_ACK));
      assert(s.state == WA_GPU_RESET);
    }
    assert(wa_gpu_gate_advance(&s, ev[i]));
    if (i+1 < sizeof(ev)/sizeof(ev[0])) assert(!wa_gpu_gate_ready(&s));
  }
  assert(wa_gpu_gate_ready(&s));
  assert(!wa_gpu_gate_advance(&s, ev[0]));
  wa_gpu_gate_quarantine(&s);
  assert(!wa_gpu_gate_ready(&s));
  assert(!wa_gpu_gate_cold_reset(&s, 0));
  assert(wa_gpu_gate_cold_reset(&s, 1));
  assert(s.epoch == 1 && s.state == WA_GPU_RESET);
  assert(!wa_gpu_gate_ready(&s));
  printf("GPU gate host tests passed (not hardware/Metal evidence)\n");
  return 0;
}
