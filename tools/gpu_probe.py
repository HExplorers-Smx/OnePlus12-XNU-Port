#!/usr/bin/env python3
"""Extract strictly read-only GPU DT facts and firmware pins from local inputs.

It never asserts that these data establish GPU runtime readiness. No firmware
contents or device-unique identifiers are put into the emitted report.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import zipfile
import sys
from pathlib import Path
from inspect_firmware import FormatError, inspect_file, inspect_bytes

FIRMWARE_CANDIDATES = (
    'gmu_gen70900.bin', 'gen70900_sqe.fw',
    'gen70900_aqe.fw', 'gen70900_zap.mbn',
)
MAX_FIRMWARE_BYTES = 64 * 1024 * 1024

def hash_file(path: Path) -> dict:
    if path.is_symlink() or not path.is_file():
        raise ValueError('firmware must be a local regular file, not a symlink')
    stat=path.stat()
    if not 0 < stat.st_size <= MAX_FIRMWARE_BYTES:
        raise ValueError('firmware input length is outside configured limits')
    digest=hashlib.sha256()
    with path.open('rb') as f:
        while chunk:=f.read(256*1024): digest.update(chunk)
    return {'filename':path.name,'size_bytes':stat.st_size,'sha256':digest.hexdigest(),
            'verified_signature':False,'usable_on_waffle':False}

def firmware_inventory(directory: Path|None, system_build: str) -> dict:
    result={'build_identifier':system_build, 'required_candidates':list(FIRMWARE_CANDIDATES),
            'found': [], 'missing': [], 'complete': False,
            'authenticated':False,'board_binding_verified':False}
    if directory is None:
        result['missing']=list(FIRMWARE_CANDIDATES)
        return result
    if not directory.is_dir():
        raise ValueError('firmware directory not found')
    # Reject ambiguous case-insensitive names and oversized/unexpected entries.
    for name in FIRMWARE_CANDIDATES:
        candidate=directory/name
        if not candidate.exists() and not candidate.is_symlink():
            result['missing'].append(name)
        else:
            result['found'].append(hash_file(candidate))
    result['complete']=not result['missing']
    return result

def gpu_kind(node: dict) -> str|None:
    path=node['path'].lower()
    compat=' '.join(node.get('compatible',[])).lower()
    if ('gmu' in compat or ('gmu@' in path and 'gpu' in path)):
        return 'gmu'
    if ('smmu' in compat or 'smmu@' in path) and ('gpu' in path or 'gpu' in compat):
        return 'gpu_smmu_candidate'
    if ('adreno' in compat or 'gpu@' in path or 'kgsl-3d0' in compat or 'kgsl-3d0' in path) and 'gmu' not in path:
        return 'gpu'
    return None

def gpu_dt_inventory(parsed: dict) -> dict:
    if parsed.get('format')=='fdt':
        fdt_entries=[parsed['fdt']]
    elif parsed.get('format')=='android-vendor-boot':
        fdt_entries=[parsed['embedded_dtb']] if 'embedded_dtb' in parsed else []
    elif parsed.get('format')=='dtbo-table':
        fdt_entries=[entry['fdt'] for entry in parsed['entries']]
    else:
        fdt_entries=[]
    out=[]
    matches=[]
    resource_graphs=[]
    for index, fdt in enumerate(fdt_entries):
        resource_graphs.append(fdt.get('gpu_graph', {'schema':1,'nodes':[], 'usable_for_gpu_hw_programming':False}))
        root=' '.join(fdt.get('root_compatible',[])).lower()
        matches.append('pineapple' in root or 'waffle' in root)
        # DTBO overlays may omit a root compatible: this is unknown, NOT matched.
        for node in fdt.get('selected_nodes',[]):
            kind=gpu_kind(node)
            if kind is None: continue
            copy={key:node[key] for key in (
                'path','compatible','reg','reg_names','interrupt_names',
                'iommus_raw_cells','interrupts_raw_cells',
                'interrupts_extended_raw_cells','qcom_chipid_raw_cells','status')
                if key in node}
            out.append({'fdt_index':index,'role_candidate':kind,**copy})
    return {'candidates':out,'count':len(out),'root_compatible_matches_waffle':matches,
            'resource_graphs':resource_graphs,
            'phandles_resolved':False,
            'smmu_context_ownership_confirmed':False,'irq_controller_resolved':False,
            'mmio_safe_to_access':False}

def inspect_image_or_zip(path: Path) -> dict:
    if path.suffix.lower() != '.zip':
        return inspect_file(path)
    # Stream only the single expected image from the release ZIP; do not
    # extract arbitrary member paths and do not write source firmware to disk.
    with zipfile.ZipFile(path) as archive:
        candidates=[]
        for member in archive.infolist():
            if member.is_dir(): continue
            filename=Path(member.filename).name.lower()
            if filename.endswith(('.img','.dtb','.dtbo')):
                if not (40 <= member.file_size <= 512*1024*1024):
                    raise ValueError('image entry length is outside inspection limits')
                # ZIP unix symlinks are not regular image files.
                if (member.external_attr >> 16) & 0o170000 == 0o120000:
                    raise ValueError('symlink archive entries are not supported')
                candidates.append(member)
        if len(candidates) != 1:
            raise ValueError('ZIP must contain exactly one boot/dtb/dtbo image')
        entry=candidates[0]
        data=archive.read(entry)
        if len(data)!=entry.file_size:
            raise ValueError('archive image size mismatch')
        parsed=inspect_bytes(data)
        return {'schema':1, 'source_filename':path.name,
                'member_name':Path(entry.filename).name,
                'sha256':hashlib.sha256(data).hexdigest(),
                'file_size_bytes':len(data), 'verified_real_device':False,
                'not_flashable':True,**parsed}

def report(image: Path|None, firmware_dir: Path|None, build: str) -> dict:
    if not build or len(build)>160 or any(c in build for c in '\r\n\0'):
        raise ValueError('build identifier must be an explicit known software version')
    result={'schema':2, 'tool':'waffle-gpu-offline-probe',
            'board':'waffle','soc':'SM8650','expected_gpu':'Adreno750v2',
            'build':build,'offline_only':True,'flashable':False,
            'hardware_tested':False,'gpu_acceleration_available':False}
    if image is not None:
        img=inspect_image_or_zip(image)
        result['dt_image_sha256']=img['sha256']
        result['dt_image_format']=img['format']
        result['device_tree']=gpu_dt_inventory(img)
    else:
        result['device_tree']={'candidates':[],'count':0,'root_compatible_matches_waffle':[],
                                'phandles_resolved':False,
                                'smmu_context_ownership_confirmed':False,
                                'mmio_safe_to_access':False}
    result['firmware']=firmware_inventory(firmware_dir,build)
    return result

def main() -> int:
    p=argparse.ArgumentParser(description='WaFFle SM8650 GPU inventory; never writes flash or GPU registers')
    p.add_argument('--dt-image',type=Path,help='raw DTB, DTBO, vendor_boot image or single-image ZIP')
    p.add_argument('--firmware-dir',type=Path,help='optional verified stock extracted firmware directory')
    p.add_argument('--build',required=True,help='matching ColorOS software version; never inferred from filename')
    p.add_argument('--out',type=Path,help='write metadata JSON only')
    args=p.parse_args()
    try:
        result=report(args.dt_image,args.firmware_dir,args.build)
        encoded=json.dumps(result,ensure_ascii=False,indent=2)+'\n'
        if args.out:
            args.out.parent.mkdir(parents=True,exist_ok=True)
            args.out.write_text(encoded,encoding='utf-8')
        else: sys.stdout.write(encoded)
        return 0
    except (FormatError,ValueError,OSError,zipfile.BadZipFile) as exc:
        print(f'gpu-probe: {exc}',file=sys.stderr)
        return 2

if __name__=='__main__':
    raise SystemExit(main())
