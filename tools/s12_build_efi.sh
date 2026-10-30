#!/usr/bin/env bash
# Produces an actual ARM64 PE/COFF EFI application, but NO boot.img or phone firmware.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p output/s12
CC=${CC:-clang}
LINK=${LINK:-lld-link}
CFLAGS=(--target=aarch64-windows-msvc -std=c11 -ffreestanding -fno-builtin
  -fno-stack-protector -fshort-wchar -mno-outline-atomics -O1 -Wall -Wextra -Werror
  -Iuefi/WaffleS12Loader -Iboot)
for name in waffle_s12_efi waffle_s12_xnup waffle_s11_fdt; do
  case "$name" in
  waffle_s12_efi) src=uefi/WaffleS12Loader/$name.c;;
  *) src=boot/$name.c;;
  esac
  "$CC" "${CFLAGS[@]}" -c "$src" -o "output/s12/$name.obj"
done
"$LINK" /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:arm64 \
  /out:output/s12/WaffleS12Preflight.efi output/s12/*.obj
file output/s12/WaffleS12Preflight.efi
