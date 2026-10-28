#!/usr/bin/env python3
"""Offline OnePlus12 rooted ZIP evidence qualifier. Never writes partition or binaries.

Qualifies copied bytes against capture SHA256 (not OEM authenticity), parses the
runtime FDT (not an unapplied overlay), and writes only non-secret metadata.
Fails closed for wrong board/build, missing firmware, unexpected ZIP members,
invalid GPU address translation, GMU/IRQ/SMMU mismatches and digest disagreement.

Usage: python3 tools/qualify_evidence_s5.py PRIVATE.zip --out boards/gpu/waffle_a750_verified_16_501.json
Keep PRIVATE.zip out of public repositories. Outputs do not embed raw FDT or firmware.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import re
import sys
import zipfile
from pathlib import Path
from inspect_firmware import inspect_bytes, FormatError

BUILD='PJD110_16.0.10.501(CN01)'
FIRMWARE=('gmu_gen70900.bin','gen70900_sqe.fw','gen70900_aqe.fw','gen70900_zap.mbn')
REQUIRED=('capture.txt','fdt.dtb','dtbo.img',*(f'firmware/{x}' for x in FIRMWARE),'README-VERIFY.txt')
MAX_ITEM={'capture.txt':20_000,'fdt.dtb':8_000_000,'dtbo.img':40_000_000,
          'README-VERIFY.txt':10000,**{f'firmware/{x}':8_000_000 for x in FIRMWARE}}
GMU_MAX_BLOCKS=64
RANGES=((0,0x4000,'itcm'),(0x4000,0xffc000,'icache'),(0x10004000,0x4000,'dtcm'),
        (0x60000000,0x20000000,'kernel'),(0xc0000000,0x20000000,'kernel_ext'))

class EvidenceError(ValueError): pass

def read_zip_bundle(path:Path) -> dict[str,bytes]:
    try:
        with zipfile.ZipFile(path) as z:
            info=z.infolist()
            names=[x.filename for x in info]
            if len(names)!=len(set(names)) or set(names)!=set(REQUIRED):
                raise EvidenceError('missing/duplicate/unexpected ZIP member')
            for item in info:
                if item.file_size<0 or item.file_size>MAX_ITEM[item.filename]:
                    raise EvidenceError(f'ZIP member length exceeds limit: {item.filename}')
                if item.is_dir() or item.filename.startswith(('/','../')) or '..' in item.filename.split('/'):
                    raise EvidenceError('unsafe ZIP member')
                # Exclude special Unix files and symlinks when mode is present.
                mode=(item.external_attr>>16)&0o170000
                if mode not in (0,0o100000): raise EvidenceError('unexpected ZIP file type')
            return {n:z.read(n) for n in REQUIRED}
    except (OSError,zipfile.BadZipFile,RuntimeError) as exc:
        raise EvidenceError(f'invalid ZIP archive: {exc}') from exc

def capture_properties(raw:bytes) -> dict[str,str]:
    txt=raw.decode('utf-8','strict')
    props={}
    for line in txt.splitlines():
        if ': ' in line and not line.startswith('## '):
            k,v=line.split(': ',1)
            if k in props: raise EvidenceError(f'duplicate capture field: {k}')
            props[k]=v.strip()
    if props.get('root.uid')!='0' or props.get('capture.complete')!='true':
        raise EvidenceError('incomplete Root evidence capture')
    if props.get('prop.ro.product.model')!='PJD110' or props.get('prop.ro.board.platform')!='pineapple' or props.get('prop.ro.soc.model')!='SM8650':
        raise EvidenceError('wrong model or SoC')
    if props.get('prop.ro.build.display.id')!=BUILD or props.get('prop.ro.boot.slot_suffix')!='_a' or props.get('dtbo.partition')!='dtbo_a':
        raise EvidenceError('incompatible build or active-slot DTBO')
    return props

def parse_gmu(raw:bytes) -> dict:
    """Read 16-byte Gen7 blocks; metadata addresses are GMU virtual addresses."""
    if len(raw)<16 or len(raw)>8_000_000: raise EvidenceError('GMU firmware length invalid')
    pos=0; seen_data=False; blocks=[]; occupied=[]
    while pos<len(raw):
        if len(raw)-pos<16 or len(blocks)>=GMU_MAX_BLOCKS: raise EvidenceError('GMU header truncated/too many blocks')
        addr,size,typ,val=(int.from_bytes(raw[pos+i:pos+i+4],'little') for i in (0,4,8,12))
        start=pos+16
        if size>len(raw)-start: raise EvidenceError('GMU data truncated')
        region=next((name for base,span,name in RANGES if addr%4==0 and
                     (size if size else val)>0 and (size if size else val)%4==0 and
                     addr>=base and (size if size else val)<=span and addr-base<=span-(size if size else val)),None)
        if size==0:
            if seen_data or typ<1 or typ>7: raise EvidenceError('GMU metadata order/type invalid')
            if typ in (1,7) and not region: raise EvidenceError('GMU prealloc outside virtual range')
        else:
            seen_data=True
            if not region:raise EvidenceError('GMU data outside valid virtual range')
            if any(addr<a+s and a<addr+size for a,s in occupied):
                raise EvidenceError('overlapping GMU data segments')
            occupied.append((addr,size))
        blocks.append({'gmu_virtual_address':f'0x{addr:08x}', 'data_size':size,
                       'metadata_type':typ if size==0 else None,
                       'prealloc_bytes':val if size==0 and typ in (1,7) else None,
                       'region':region})
        pos=start+size
    if not seen_data: raise EvidenceError('empty GMU firmware')
    return {'block_count':len(blocks),'data_block_count':len(occupied),
            'persistent_prealloc_count':sum(x['metadata_type']==7 for x in blocks),
            'blocks':blocks,'parsed_format_only':True,'firmware_authenticated':False}

def only_node(graph:dict,path:str)->dict:
    matches=[x for x in graph['nodes'] if x['path']==path]
    if len(matches)!=1: raise EvidenceError('GPU hardware node missing or duplicated: '+path)
    return matches[0]

def register(node:dict,name:str)->dict:
    items=[x for x in node['registers']['entries'] if x['name']==name]
    if len(items)!=1: raise EvidenceError('missing named GPU register: '+name)
    item=items[0]
    tr=item['translation']
    if tr.get('status')!='decoded-cpu-address-without-access-grant':
        raise EvidenceError('untranslated GPU MMIO: '+name)
    return {'cpu_physical_base':tr['cpu_physical_address'],'size_bytes':item['size_bytes']}

def specifiers(node:dict)->list[list[int]]:
    if node['interrupts']['status']!='syntactically-resolved-no-irq-lease':
        raise EvidenceError('missing GIC interrupt specifiers')
    return [x['specifier_cells'] for x in node['interrupts']['items']]

def smmu_sids(graph:dict)->dict[str,int]:
    mapping={}
    for label,suffix in [('user','gfx3d_user'),('lpac','gfx3d_lpac'),('secure','gfx3d_secure')]:
        node=only_node(graph,'/soc/qcom,kgsl-iommu@3da0000/'+suffix)
        iommu=node['links']['iommus']
        if iommu['status']!='syntactically-resolved-no-ownership' or len(iommu['items'])!=1:
            raise EvidenceError('unresolved '+label+' SMMU link')
        item=iommu['items'][0]
        if item['provider_path']!='/soc/kgsl-smmu@3da0000' or len(item['argument_cells'])!=2:
            raise EvidenceError('wrong '+label+' SMMU provider')
        mapping[label]=item['argument_cells'][0]
    gmu=only_node(graph,'/soc/qcom,gmu@3d69000')
    item=gmu['links']['iommus']['items']
    if len(item)!=1 or item[0]['provider_path']!='/soc/kgsl-smmu@3da0000' or len(item[0]['argument_cells'])!=2:
        raise EvidenceError('wrong GMU IOMMU specifier')
    mapping['gmu']=item[0]['argument_cells'][0]
    if mapping!={'user':0,'lpac':1,'secure':2,'gmu':5}:
        raise EvidenceError('unexpected SID routing; no fallback')
    return mapping

def qualify(bundle:dict[str,bytes])->dict:
    cap=capture_properties(bundle['capture.txt'])
    checks={}
    for name in REQUIRED:
        if name in ('capture.txt','README-VERIFY.txt'):continue
        digest=hashlib.sha256(bundle[name]).hexdigest()
        if cap.get('sha256.'+name)!=digest:
            raise EvidenceError(f'capture hash mismatch for {name}')
        checks[name]={'bytes':len(bundle[name]),'sha256':digest,'captured_hash_matches':True}
    fdt=inspect_bytes(bundle['fdt.dtb'])
    if fdt['format']!='fdt':raise EvidenceError('runtime FDT not valid')
    root=fdt['fdt']
    if not root['root_model'] or 'waffle' not in root['root_model'][0]:
        raise EvidenceError('FDT board model is not waffle')
    graph=root['gpu_graph']
    chip_entries=[node for node in root['selected_nodes'] if node['path']=='/soc/qcom,kgsl-3d0@3d00000']
    if len(chip_entries)!=1 or chip_entries[0].get('qcom_chipid_raw_cells')!=[0x43051401]:
        raise EvidenceError('unexpected Adreno chip ID')
    if graph['duplicate_phandles']:
        raise EvidenceError('duplicate FDT phandles; cannot resolve topology')
    gpu=only_node(graph,'/soc/qcom,kgsl-3d0@3d00000')
    gmu=only_node(graph,'/soc/qcom,gmu@3d69000')
    smmu=only_node(graph,'/soc/kgsl-smmu@3da0000')
    if 'qcom,adreno-gpu-gen7-9-1' not in gpu['compatible'] or 'qcom,gen7-gmu' not in gmu['compatible'] or 'qcom,adreno-smmu' not in smmu['compatible']:
        raise EvidenceError('incompatible GPU / GMU / SMMU kind')
    regs={'gpu':register(gpu,'kgsl_3d0_reg_memory'), 'gmu':register(gmu,'gmu'),
          'gpu_smmu':register(smmu,'base')}
    if regs!={'gpu':{'cpu_physical_base':'0x3d00000','size_bytes':262144},
              'gmu':{'cpu_physical_base':'0x3d68000','size_bytes':225280},
              'gpu_smmu':{'cpu_physical_base':'0x3da0000','size_bytes':262144}}:
        raise EvidenceError('GPU register windows differ from qualified PJD110 board')
    irq={'gpu':specifiers(gpu),'gmu':specifiers(gmu)}
    if irq!={'gpu':[[0,300,4]],'gmu':[[0,304,4],[0,305,4]]}:
        raise EvidenceError('unexpected GPU/GMU IRQ topology')
    sids=smmu_sids(graph)
    if gmu['links']['clocks']['status']!='syntactically-resolved-no-ownership':
        raise EvidenceError('unresolved GMU clocks')
    if gmu['links']['mboxes']['status']!='syntactically-resolved-no-ownership':
        raise EvidenceError('unresolved GMU AOP mailbox')
    if not all(k in gmu['links'] and gmu['links'][k]['status']=='syntactically-resolved-no-ownership' for k in ['vdd-supply','vddcx-supply']):
        raise EvidenceError('unresolved GMU power supplies')
    dtbo=inspect_bytes(bundle['dtbo.img'])
    if dtbo['format']!='dtbo-table' or dtbo['entry_count']!=12:
        raise EvidenceError('incompatible active-slot DTBO table')
    # The DTBO header can be smaller than the 24 MiB partition capacity.
    # The active runtime FDT is authoritative; table index cannot be guessed.
    gm=parse_gmu(bundle['firmware/gmu_gen70900.bin'])
    if gm['block_count']!=15 or gm['data_block_count']!=4:
        raise EvidenceError('GMU firmware block count mismatch')
    for fw in ('gen70900_sqe.fw','gen70900_aqe.fw'):
        raw=bundle['firmware/'+fw]
        if len(raw)<8 or len(raw)%4:
            raise EvidenceError('malformed SQE/AQE microcode file')
    zap=bundle['firmware/gen70900_zap.mbn']
    if len(zap)<256:raise EvidenceError('ZAP firmware unexpectedly short')
    return {'schema':2,'phase':'S5','model':'PJD110','board':'waffle',
            'soc':'SM8650','os_build':BUILD,'active_slot':'a',
            'source_kind':'owner_supplied_root_read_only_capture',
            'input_checksums':checks, 'runtime_fdt':{'node_count':root['node_count'],
            'root_model':root['root_model'][0],'adreno_chip_id':'0x43051401',
            'gpu_mmio':regs,'gic_specifiers':irq,
            'smmu_stream_ids_not_context_banks':sids,
            'gmu_clock_provider_paths':[x.get('provider_path') for x in gmu['links']['clocks']['items']],
            'gmu_aop_mailbox':gmu['links']['mboxes']['items'][0]['provider_path'],
            'gmu_supplies':{k:gmu['links'][k]['provider_path'] for k in ('vdd-supply','vddcx-supply')},
            'runtime_tree_used_not_unapplied_dtbo':True},
            'dtbo':{'partition_capacity_bytes':len(bundle['dtbo.img']),
                    'dtbo_table_bytes':dtbo['total_size'],'entry_count':dtbo['entry_count'],
                    'entry_hashes':[e['dtb_sha256'] for e in dtbo['entries']],
                    'applied_overlay_index':'unknown_do_not_guess'},
            'gmu_firmware':gm,
            'gpu_hardware_powered_by_xnu':False,'gpu_smmu_ownership_acquired':False,
            'gmu_hfi_handshake_verified':False,'gpu_command_submitted':False,
            'hardware_fence_completed':False,'ios_metal_acceleration_verified':False,
            'firmware_authenticity_proven':False,'may_flash':False,
            'contains_firmware_bytes':False,'contains_raw_device_tree':False}

def main()->int:
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('private_zip',type=Path)
    ap.add_argument('--out',type=Path)
    opts=ap.parse_args()
    try:result=qualify(read_zip_bundle(opts.private_zip))
    except (EvidenceError,FormatError,UnicodeError,ValueError,KeyError) as exc:
        print(f'qualification failed: {exc}',file=sys.stderr);return 1
    txt=json.dumps(result,ensure_ascii=False,indent=2)+'\n'
    if opts.out:
        opts.out.parent.mkdir(parents=True,exist_ok=True)
        opts.out.write_text(txt,encoding='utf-8')
        print('qualified:',opts.out)
    else: print(txt,end='')
    return 0
if __name__=='__main__':raise SystemExit(main())
