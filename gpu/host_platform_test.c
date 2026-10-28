#include "waffle_gpu_platform.h"
#include <assert.h>
#include <stdio.h>

static const unsigned int evidence[] = {
    WA_GPU_EV_MATCHED_BOARD | WA_GPU_EV_MMIO_AND_IRQ_VERIFIED,
    WA_GPU_EV_PINNED_FIRMWARE | WA_GPU_EV_AUTHENTICATED_FIRMWARE,
    WA_GPU_EV_POWER_LEASE | WA_GPU_EV_POWER_READBACK,
    WA_GPU_EV_SMMU_LEASE | WA_GPU_EV_SMMU_READBACK,
    WA_GPU_EV_GMU_READY | WA_GPU_EV_HFI_ACK,
    WA_GPU_EV_SUBMITTED_AND_COMPLETED_FENCE | WA_GPU_EV_DMA_STOP_CONFIRM,
    WA_GPU_EV_SCANOUT_PIXEL_MATCH | WA_GPU_EV_IOSURFACE_LIFETIME,
    WA_GPU_EV_METAL_DEVICE_PUBLISHED | WA_GPU_EV_RENDER_SERVER_FRAME
};
struct sim { unsigned int called; unsigned int fail_at; };
static int fake_proof(void *ctx) {
    struct sim *sim=(struct sim*)ctx;
    if (sim->fail_at == sim->called) { ++sim->called; return -1; }
    return (int)evidence[sim->called++];
}

int main(void) {
    struct wa_gpu_gate gate;
    struct sim sim = {0,42};
    struct wa_gpu_platform_ops ops = {fake_proof,fake_proof,fake_proof,fake_proof,
                                     fake_proof,fake_proof,fake_proof,fake_proof};
    wa_gpu_gate_init(&gate);
    assert(wa_gpu_platform_step(&gate,NULL,&sim)==0);
    assert(wa_gpu_platform_step(&gate,&ops,&sim)==1);
    assert(gate.state == WA_GPU_DT_VERIFIED);
    ops.verify_signed_firmware=NULL;
    assert(wa_gpu_platform_step(&gate,&ops,&sim)==0);
    assert(gate.state==WA_GPU_DT_VERIFIED);
    ops.verify_signed_firmware=fake_proof;
    while (gate.state != WA_GPU_METAL_PROVED)
        assert(wa_gpu_platform_step(&gate,&ops,&sim)==1);
    assert(wa_gpu_gate_ready(&gate));
    assert(sim.called==8);
    wa_gpu_gate_init(&gate);
    sim.called=0;sim.fail_at=3;
    for (int i=0;i<3;i++) assert(wa_gpu_platform_step(&gate,&ops,&sim)==1);
    assert(wa_gpu_platform_step(&gate,&ops,&sim)==-1);
    assert(gate.state==WA_GPU_QUARANTINED);
    assert(wa_gpu_platform_step(&gate,&ops,&sim)==0);
    puts("GPU adapter contract tests passed; real backends still absent");
    return 0;
}
