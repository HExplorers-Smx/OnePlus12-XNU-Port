/* SPDX-License-Identifier: MIT
 * XNUP payload compatibility layer based on the public wire format described by
 * HakiOS: superboyyy/hakios-arm64e-dada/base/xnuloader/mkpayload.py.
 * Independently implemented bounds-checking parser, no I/O or allocations.
 */
#ifndef WAFFLE_S12_XNUP_H
#define WAFFLE_S12_XNUP_H
#include <stddef.h>
#include <stdint.h>
#define WA_S12_MAX_PAYLOAD (128u * 1024u * 1024u)
#define WA_S12_MAX_ENTRIES 64
#define WA_S12_OK 0
#define WA_S12_INVALID (-1)
#define WA_S12_BOUNDS (-2)
#define WA_S12_DUPLICATE (-3)
#define WA_S12_NO_KERNEL (-4)
#define WA_S12_NOT_MACHO (-5)
struct wa_s12_view {
    size_t kernel_offset;
    size_t kernel_bytes;
    size_t cmdline_offset;
    size_t cmdline_bytes;
    unsigned entries;
    int usb;
};
int wa_s12_xnup_parse(const uint8_t *image, size_t actual,
                       struct wa_s12_view *view);
#endif
