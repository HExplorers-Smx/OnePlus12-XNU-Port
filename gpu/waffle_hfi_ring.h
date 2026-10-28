/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef WAFFLE_HFI_RING_H
#define WAFFLE_HFI_RING_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Gen7 HWSCHED 17-queue ABI. Words are 32-bit and packets use 4-word padding.
 * Follows the pinned OnePlus Gen7 layout (not the iOS/Metal ABI).
 * This module edits OWNED HOST RAM only; it does not access MMIO, perform
 * cache maintenance, or claim that firmware will observe the changes.
 * The provider barrier MUST do the actual cache publication on the target.
 */
enum { WA_RING_QUEUES=17, WA_RING_HEADER_WORDS=6,
       WA_RING_QUEUE_HEADER_WORDS=12, WA_RING_TABLE_WORDS=210,
       WA_RING_DATA_OFFSET=848, WA_RING_QUEUE_WORDS=1024,
       WA_RING_TOTAL_BYTES=0x12000, WA_RING_IOVA=0x60010000,
       WA_RING_MAX_PACKET=255 };
enum wa_ring_result { WA_RING_OK=1, WA_RING_EMPTY=0, WA_RING_INVALID=-1,
                      WA_RING_CORRUPT=-2, WA_RING_FULL=-3 };
typedef int (*wa_ring_sync)(void *context); /* 1: confirmed publication, else uncertain */
struct wa_ring_io {
  volatile uint32_t *memory;
  size_t bytes;
  uint32_t base_iova;
  void *context;
  wa_ring_sync sync_for_device;
  wa_ring_sync sync_for_cpu;
};
/* Initialize memory BEFORE ownership is transferred to the GMU. */
int wa_ring_initialize(void *owned_host_ram, size_t size, uint32_t base_iova);
int wa_ring_check(const struct wa_ring_io *io, unsigned queue);
/* queue0: CPU -> GMU; caller must first obtain exclusive producer ownership. */
int wa_ring_send(const struct wa_ring_io *io, const uint32_t *packet, size_t words);
/* queue1/2: GMU -> CPU; receiving consumes the packet only on WA_RING_OK.
 * A malformed packet does not move the read cursor and must quarantine session.
 */
int wa_ring_receive(const struct wa_ring_io *io, unsigned queue,
                    uint32_t *output, size_t capacity, size_t *received);
#ifdef __cplusplus
}
#endif
#endif
