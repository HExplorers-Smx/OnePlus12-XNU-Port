/* SPDX-License-Identifier: MIT */
#ifndef WAFFLE_S11_FDT_H
#define WAFFLE_S11_FDT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* No allocations, no writes to the FDT, no MMIO. Input MUST be a readable,
 * bounded buffer; the UEFI caller validates the enclosing memory descriptor. */
enum wa_s11_result {
    WA_S11_OK=0, WA_S11_NO_FDT=-1, WA_S11_BOUNDS=-2,
    WA_S11_FORMAT=-3, WA_S11_BOARD=-4, WA_S11_COMPLEXITY=-5
};
#define WA_S11_MAX_DTB (8u * 1024u * 1024u)
struct wa_s11_identity {
    uint32_t totalsize, version, structure_bytes, nodes, properties;
    uint32_t crc32; /* NON-cryptographic, does not prove firmware authenticity. */
    unsigned model_waffle, compatible_pineapple;
};
struct wa_s11_memory_region { uint64_t physical_start, pages; uint32_t memory_type; };
/* Returns 1 only if [address,address+size) lies entirely within one supplied
 * EFI memory descriptor. This is not proof of ownership or execution safety. */
int wa_s11_memory_contains(const struct wa_s11_memory_region *regions,
                           size_t count, uint64_t address, uint64_t size);
int wa_s11_fdt_inspect(const uint8_t *blob,size_t available,
                      struct wa_s11_identity *out);
#ifdef __cplusplus
}
#endif
#endif
