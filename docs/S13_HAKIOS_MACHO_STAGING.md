# S13 — Temporary Mach-O segment staging for OnePlus 12 (waffle)

**Deliverable:** native AArch64 PE32+ UEFI program (`WaffleS13EphemeralStage.efi`),
portable C Mach-O parser/stager and mock Boot Services tests. Based on the
public HakiOS `xnuloader/loader.c` boot architecture; original independent
implementation with a fail-closed, temporary-memory-only boundary.

## New working code

- `boot/waffle_s13_macho.{c,h}` scans `LC_SEGMENT_64` records and an optional
  ARM64 `LC_UNIXTHREAD` entry, checks load command bounds and segment overlap,
  rejects invalid permissions, out-of-file offsets, mismatched page offsets,
  invalid executable entry, and excessive VM spans.
- `wa_s13_macho_stage()` **actually copies segment bytes and zero-fills BSS**
  into independently owned memory. It recomputes the plan before staging to
  prevent callers forging segments with unverified offsets.
- `wa_s13_bootargs_draft()` builds metadata using the **iOS 26 XNU 12377 draft
  layout** (1,024-character command line). This is **not** the iOS 16 layout
  and is *not a valid boot handoff*: device tree, memory map, framebuffer, PAC,
  chained fixups, and boot arguments are not yet populated/relocated correctly.
- `uefi/WaffleS13Loader/waffle_s13_efi.c` reuses S12's FDT/XNUP verification;
  after validation it requests `AllocateAnyPages(EfiLoaderData)` from UEFI,
  stages the Mach-O in those pages, writes the boot_args draft in an aligned
  tail, then calls `FreePages` before returning. It does **not** call
  `ExitBootServices`, touch chipset MMIO, install a new memory map, use USB,
  read or write storage, or jump to XNU.
- `boot/host_s13_efi_mock.c` executes the *same* `efi_main` with a synthetic
  memory map and verifies the data and BSS **at FreePages time**, proving
  the pages were filled and released. The phone is not involved.

## Building and testing

```sh
bash tools/s13_build_efi.sh
python3 -m unittest tests.test_s13_macho -v
ONEPLUS12_PRIVATE_EVIDENCE=/absolute/path/to/private.zip \
    python3 -m unittest discover -s tests -v
WAFFLE_SANITIZE=1 python3 -m unittest tests.test_s13_macho -v
python3 tools/check_manual_actions.py
```

The private evidence ZIP is optional, belongs **outside** this repository and
must never be shared publicly. Tests use a **synthetic Mach-O kernel** to prove
memory handling; this milestone does not demonstrate compatibility with any
real Apple kernelcache or HakiOS Mach-O kernel.

## Missing for a real XNU handoff (not silently implemented)

1. OnePlus12-specific **working** UEFI / EDK2 and protected payload RAM layout,
   CPU0, GOP and GIC handoff; tested rollback/recovery from a non-boot.
2. XNU-specific Mach-O page placement, entry, relocation, chained fixups,
   arm64e PAC and memory attributes, with explicit iOS16-versus-iOS26 variants.
3. XNU `boot_args` full initialization including a valid flattened device tree,
   reserved memory, framebuffer and memory descriptors; correct handoff registers.
4. Safe `ExitBootServices` path with memory-map retry/rekey, exception logging,
   non-flash recovery and an externally controlled USB/serial log channel.
5. Actual phone tests **only after** restoring Android has been proven; no
   `fastboot boot`, `fastboot flash`, DSU installation or partition writes here.

**Do not flash this `.efi`.** It is an EFI application, not an Android boot
image or OEM-signed UEFI. All phone partitions remain untouched.

## Provenance

- HakiOS loader architecture: https://github.com/superboyyy/hakios-arm64e-dada/blob/main/base/xnuloader/loader.c
- HakiOS XNUP format: https://github.com/superboyyy/hakios-arm64e-dada/blob/main/base/xnuloader/mkpayload.py
- Source written for this repository; no upstream proprietary firmware or
  vendor firmware binaries are redistributed. Consult original licenses before
  adopting upstream source code verbatim.
- GitHub Actions remains manually triggered (`workflow_dispatch` only).
