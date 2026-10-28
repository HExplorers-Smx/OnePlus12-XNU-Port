/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef WAFFLE_HFI_ALLOC_H
#define WAFFLE_HFI_ALLOC_H
#include <stddef.h>
#include <stdint.h>
/*
 * Gen7 HWSCHED F2H_MSG_MEM_ALLOC (id 20) transport contract.
 * Wire format checked against upstream OnePlus Ace 5 / Gen7 reference.
 * No device access: physical backing and both IOVAs MUST come from an
 * authenticated, already-owned GPU/GMU mapping provider. This is NOT a
 * hardware-ready XNU service.
 */
#ifdef __cplusplus
extern "C" {
#endif

enum { WA_HFI_ALLOC_ID=20, WA_HFI_ACK_TYPE=1, WA_HFI_V1_WORDS=11,
       WA_HFI_V0_WORDS=10, WA_HFI_LIMIT=32, WA_HFI_PAGE=4096,
       WA_HFI_MAX_BYTES=64*1024*1024 };
enum { WA_HFI_GFX_ACC=1u, WA_HFI_GMU_ACC=1u<<3,
       WA_HFI_GMU_PRIV=1u<<4, WA_HFI_GMU_WRITE=1u<<5,
       WA_HFI_SECURE=1u<<12, WA_HFI_KNOWN_FLAGS=0x13ffu };
enum wa_hfi_status {
  WA_HFI_OK=0, WA_HFI_INPUT, WA_HFI_HEADER, WA_HFI_FLAGS,
  WA_HFI_KIND, WA_HFI_SIZE, WA_HFI_ALIGNMENT, WA_HFI_RESERVED,
  WA_HFI_CONFLICT, WA_HFI_FULL, WA_HFI_PROVIDER,
  WA_HFI_PUBLISH_AMBIGUOUS, WA_HFI_RELEASE_AMBIGUOUS, WA_HFI_QUARANTINED
};
struct wa_hfi_request {
  uint32_t header, version;
  uint64_t gpu_addr;
  uint32_t flags, kind, host_handle, gmu_handle, gmu_addr, bytes, alignment;
  uint32_t gpu_align_bytes, size_align_bytes, effective_flags;
};
struct wa_hfi_owned_block {
  void *token;
  uint64_t gpu_iova, gmu_iova;
  uint32_t bytes, effective_flags;
};
struct wa_hfi_provider {
  void *context;
  /* 1 = owned and mapped; 0 = no allocation was created; <0 ambiguous state */
  int (*reserve)(void *context, const struct wa_hfi_request *request,
                 uint32_t padded_bytes, struct wa_hfi_owned_block *out);
  /* 1 = memory initialized, read back and cache-published; 0/negative ambiguous */
  int (*publish)(void *context, const struct wa_hfi_owned_block *block);
  /* 1 = confirmed completely unmapped and released, anything else uncertain */
  int (*release)(void *context, const struct wa_hfi_owned_block *block);
};
struct wa_hfi_cache_entry {
  struct wa_hfi_request request;
  struct wa_hfi_owned_block owned;
};
struct wa_hfi_allocator {
  struct wa_hfi_cache_entry entries[WA_HFI_LIMIT];
  unsigned count, ack_sequence, refused, reused, quarantined;
};

void wa_hfi_allocator_init(struct wa_hfi_allocator *allocator);
int wa_hfi_parse_alloc(const uint32_t *frame, size_t words,
                       struct wa_hfi_request *out);
/*
 * Returns WA_HFI_OK and an ACK frame only after provider ownership and
 * publish success. Failure always sets *ack_words = 0. Published memory is
 * NEVER recycled by this API; it may still be in use by firmware DMA.
 */
int wa_hfi_process_alloc(struct wa_hfi_allocator *allocator,
                         const struct wa_hfi_provider *provider,
                         const uint32_t *frame, size_t frame_words,
                         uint32_t *ack, size_t capacity, size_t *ack_words);
#ifdef __cplusplus
}
#endif
#endif
