/* SPDX-License-Identifier: MIT
 * S13: bounded Mach-O 64 loader plan and staging; no relocation/fixups/PAC.
 */
#ifndef WAFFLE_S13_MACHO_H
#define WAFFLE_S13_MACHO_H
#include <stddef.h>
#include <stdint.h>
#define WA_S13_SEGMENTS 64u
#define WA_S13_MAX_IMAGE (128u*1024u*1024u)
#define WA_S13_MAX_SPAN (128u*1024u*1024u)
#define WA_S13_MAX_COMMANDS 4096u
#define WA_S13_ALIGN 16384u
#define WA_S13_OK 0
#define WA_S13_BAD (-1)
#define WA_S13_BOUNDS (-2)
#define WA_S13_UNSUPPORTED (-3)
#define WA_S13_OVERLAP (-4)
#define WA_S13_NO_TEXT (-5)
struct wa_s13_segment {
    uint64_t vmaddr, vmsize;
    uint64_t fileoff, filesize;
    uint32_t initprot;
};
struct wa_s13_plan {
    struct wa_s13_segment seg[WA_S13_SEGMENTS];
    uint64_t vm_base;
    uint64_t vm_size;
    uint64_t entry_vmaddr; /* zero means unknown */
    uint32_t segments;
    uint32_t has_fixups;
};
int wa_s13_macho_plan(const uint8_t *file, size_t bytes, struct wa_s13_plan *out);
/* Stage at (writable, caller-owned, independently allocated) destination.
 * No kernel execution; source and destination must be distinct. */
int wa_s13_macho_stage(const uint8_t *file, size_t bytes,
                       const struct wa_s13_plan *plan, uint8_t *dest, size_t capacity);
/* Define xnu-12377 (iOS26) *draft* boot_args only; NOT iOS16's layout. */
#define WA_S13_BOOT_LINE 1024u
struct wa_s13_boot_video {
    uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};
struct wa_s13_boot_args {
    uint16_t revision, version;
    uint64_t virtBase, physBase, memSize, topOfKernelData;
    struct wa_s13_boot_video video;
    uint32_t machineType;
    uint64_t deviceTreeP;
    uint32_t deviceTreeLength;
    char commandLine[WA_S13_BOOT_LINE];
    uint64_t bootFlags, memSizeActual;
};
/* Only prepare checked metadata for offline inspection; never hand to an XNU kernel. */
int wa_s13_bootargs_draft(struct wa_s13_boot_args *out,
                          const struct wa_s13_plan *plan,
                          uint64_t physbase, uint64_t capacity,
                          const char *cmdline, size_t cmdline_bytes);
#endif
