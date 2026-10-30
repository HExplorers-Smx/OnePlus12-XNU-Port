#!/usr/bin/env bash
# Compile actual AArch64 UEFI PE32+ preflight and ephemeral segment staging.
# No boot.img or flash image. No boot handoff.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p output/s13
CC=${CC:-clang}
LINK=${LINK:-lld-link}
CFLAGS=(--target=aarch64-windows-msvc -std=c11 -ffreestanding -fno-builtin
  -fno-stack-protector -fshort-wchar -mno-outline-atomics -O1 -Wall -Wextra -Werror
  -Iuefi/WaffleS13Loader -Iboot)
for name in waffle_s13_efi waffle_s13_mem waffle_s13_macho waffle_s12_xnup waffle_s11_fdt; do
  case "$name" in
  waffle_s13_efi|waffle_s13_mem) src=uefi/WaffleS13Loader/$name.c;;
  *) src=boot/$name.c;;
  esac
  "$CC" "${CFLAGS[@]}" -c "$src" -o "output/s13/$name.obj"
done
"$LINK" /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:arm64 \
  /out:output/s13/WaffleS13EphemeralStage.efi output/s13/*.obj
file output/s13/WaffleS13EphemeralStage.efi
