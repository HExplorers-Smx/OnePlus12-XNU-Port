/* SPDX-License-Identifier: MIT
 * Minimal UEFI declarations per UEFI specification (not a stock firmware ABI
 * replacement). Adapted from the public UEFI layouts used by HakiOS efi.h:
 * https://github.com/superboyyy/hakios-arm64e-dada/blob/main/base/xnuloader/efi.h
 * Only read-only UEFI operations are exposed to the S12 preflight app.
 */
#ifndef WAFFLE_EFI_MIN_H
#define WAFFLE_EFI_MIN_H
#include <stdint.h>
#include <stddef.h>
#define EFIAPI
#define WA_EFI_ERR (1ull<<63)
#define WA_EFI_SUCCESS 0ull
#define WA_EFI_NOT_FOUND (WA_EFI_ERR|14ull)
#define WA_EFI_SECURITY (WA_EFI_ERR|26ull)
#define WA_EFI_UNSUPPORTED (WA_EFI_ERR|3ull)
#define WA_EFI_BUFFER_TOO_SMALL (WA_EFI_ERR|5ull)
#define WA_EFI_ERROR(x) (((x)&WA_EFI_ERR)!=0)
typedef uint64_t wa_efi_status;
typedef void *wa_efi_handle;
typedef uint16_t wa_char16;
typedef struct {uint32_t a;uint16_t b,c;uint8_t d[8];} wa_guid;
typedef struct {uint64_t signature;uint32_t revision,size,crc,reserved;} wa_table_header;
typedef struct wa_text_out {
    void *reset;
    wa_efi_status(EFIAPI *output_string)(struct wa_text_out *,const wa_char16 *);
} wa_text_out;
typedef struct {
    uint32_t type,pad;uint64_t physical,virtual_addr,pages,attributes;
} wa_memory_descriptor;
typedef struct {
    wa_table_header hdr;
    void *raise_tpl,*restore_tpl;
    wa_efi_status(EFIAPI *allocate_pages)(int,int,uint64_t,uint64_t *);
    wa_efi_status(EFIAPI *free_pages)(uint64_t,uint64_t);
    wa_efi_status(EFIAPI *get_memory_map)(uint64_t *,void *,uint64_t *,uint64_t *,uint32_t *);
    wa_efi_status(EFIAPI *allocate_pool)(int,uint64_t,void **);
    wa_efi_status(EFIAPI *free_pool)(void *);
    void *create_event,*set_timer,*wait_for_event,*signal_event,*close_event,*check_event;
    void *install_protocol_interface,*reinstall_protocol_interface,*uninstall_protocol_interface;
    wa_efi_status(EFIAPI *handle_protocol)(wa_efi_handle,const wa_guid *,void **);
} wa_boot_services;
typedef struct {
    wa_table_header hdr;
    wa_char16 *vendor;uint32_t revision,pad;
    void *in_handle,*con_in,*out_handle;
    wa_text_out *out;
    void *err_handle,*err,*runtime;
    wa_boot_services *bs;
    uint64_t count;
    struct {wa_guid guid;void *table;} *tables;
} wa_system_table;
typedef struct {
    uint32_t revision,pad;
    wa_efi_handle parent;
    wa_system_table *system_table;
    wa_efi_handle device_handle;
    void *file_path,*reserved;
    uint32_t options_bytes,pad2;
    void *options;
    void *image_base;
    uint64_t image_bytes;
    uint32_t image_code_type,image_data_type;
    void *unload;
} wa_loaded_image;
#endif
