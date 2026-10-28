#!/usr/bin/env python3
"""S9: pinned waffle FDT clock-name/specifier audit, never a vote writer.

The captured FDT is kept OUTSIDE this repository. The sanitized result
contains paths, indices and evidence digest; not the original binary bytes.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path
from inspect_firmware import parse_fdt, FormatError
from qualify_evidence_s5 import read_zip_bundle, capture_properties, BUILD
from s8_power_inventory import FDT_PIN, PATHS, inventory

NAMES=("gcc_gpu_memnoc_gfx","gcc_gpu_snoc_dvm_gfx","gpu_cc_ahb","apb_pclk")
EXPECTED=(
    ("/soc/clock-controller@100000",37),
    ("/soc/clock-controller@100000",38),
    ("/soc/clock-controller@3d90000",0),
    ("/soc/power-controller@c300000",None),
)

def selected_properties(blob:bytes,paths:set[str]) -> dict[str,dict[str,bytes]]:
    """Extract properties after the existing strict FDT tokenizer verifies bounds."""
    parse_fdt(blob)
    total,struct_off,strings_off,_,_,_,_,strings_size,struct_size=struct.unpack_from('>9I',blob,4)
    structure=blob[struct_off:struct_off+struct_size]
    strings=blob[strings_off:strings_off+strings_size]
    stack:list[str]=[]
    found:dict[str,dict[str,bytes]]={}
    pos=0
    while pos+4<=len(structure):
        tag=struct.unpack_from('>I',structure,pos)[0];pos+=4
        if tag==1:
            end=structure.find(b'\0',pos)
            name=structure[pos:end].decode('utf-8','replace')
            pos=(end+4)&~3
            path='/' if not stack else stack[-1].rstrip('/')+'/'+name
            stack.append(path)
            if path in paths:found[path]={}
        elif tag==2:stack.pop()
        elif tag==3:
            length,nameoff=struct.unpack_from('>II',structure,pos);pos+=8
            end=strings.find(b'\0',nameoff)
            key=strings[nameoff:end].decode('ascii','replace')
            if stack and stack[-1] in paths:
                found[stack[-1]][key]=structure[pos:pos+length]
            pos=(pos+length+3)&~3
        elif tag==4:pass
        elif tag==9:break
        else:raise FormatError('unexpected FDT token')
    return found

def _cells(value:bytes)->list[int]:
    if len(value)%4:raise ValueError('non-aligned FDT cells')
    return list(struct.unpack(f'>{len(value)//4}I',value))

def decode_clock_names(node:dict[str,bytes],providers:dict[int,tuple[str,int]])->dict:
    """Accept one terminal phandle=0 *only*, never treat it as an actual clock.

    Provider names are informational: even fully decoded records are not
    permits to access GPUCC or power-controller registers.
    """
    names=[x.decode('utf-8') for x in node.get('clock-names',b'').split(b'\0') if x]
    if names!=list(NAMES):raise ValueError('GPU clock-names drift or reordered')
    raw=_cells(node.get('clocks',b''))
    at=0;found=[]
    for i,name in enumerate(names):
        if at>=len(raw):raise ValueError('missing named clock specifier')
        ph=raw[at];at+=1
        if ph in (0,0xffffffff) or ph not in providers:
            raise ValueError('missing/untrusted provider for '+name)
        path,width=providers[ph]
        if width not in (0,1) or at+width>len(raw):raise ValueError('bad clock cells')
        args=raw[at:at+width];at+=width
        wanted_path,wanted_cell=EXPECTED[i]
        if path!=wanted_path or (wanted_cell is None and args) or \
           (wanted_cell is not None and args!=[wanted_cell]):
            raise ValueError('clock provider/index mismatch for '+name)
        found.append({'index':i,'name':name,'provider_path':path,
                      'specifier':args,
                      'kind':'power-controller-reference-unverified' if i==3 else 'clock-provider-reference'})
    remainder=raw[at:]
    if remainder!=[0]:raise ValueError('unexpected trailing GPU clock cells')
    return {'named':found,'trailing_phandle_zero':True,
            'named_specifiers_decoded':True,'power_owner_verified':False,
            'safe_to_vote_or_write':False,
            'notice':'The fourth named entry resolves to power-controller, not a proved clock vote. Trailing zero is an unused raw cell.'}

def audit(fdt:bytes,build:str)->dict:
    if build!=BUILD or hashlib.sha256(fdt).hexdigest()!=FDT_PIN:
        raise ValueError('FDT/build differs from pinned PJD110 ColorOS16 capture')
    base=inventory(fdt,board_build=build)
    wanted={PATHS['gpu'],EXPECTED[0][0],EXPECTED[2][0],EXPECTED[3][0]}
    nodes=selected_properties(fdt,wanted)
    providers={}
    for path in wanted-{PATHS['gpu']}:
        props=nodes.get(path,{})
        ph=_cells(props.get('phandle',b''))
        nc=_cells(props.get('#clock-cells',b''))
        if len(ph)!=1 or len(nc)!=1:raise ValueError('provider missing phandle/#clock-cells: '+path)
        if ph[0] in providers:raise ValueError('duplicate provider phandle')
        providers[ph[0]]=(path,nc[0])
    result=decode_clock_names(nodes[PATHS['gpu']],providers)
    refs=base['dependencies']['gpu']['clocks']['specifiers']
    if len(refs)!=5 or [(v['provider_path'],v['argument_cells']) for v in refs[:4]] != \
       [(v['provider_path'],v['specifier']) for v in result['named']]:
        raise ValueError('GPU clock parser disagreement')
    return {'schema':1,'stage':'S9','board':'waffle','build':BUILD,
            'runtime_fdt_sha256':FDT_PIN,
            'clock_audit':result,'clock_configuration_live_verified':False,
            'hardware_control_available':False,'no_device_operations':True}

def main()->int:
    a=argparse.ArgumentParser(description=__doc__)
    a.add_argument('private_evidence_zip',type=Path)
    a.add_argument('--out',required=True,type=Path)
    args=a.parse_args()
    try:
        z=read_zip_bundle(args.private_evidence_zip)
        props=capture_properties(z['capture.txt'])
        result=audit(z['fdt.dtb'],props['prop.ro.build.display.id'])
        args.out.parent.mkdir(parents=True,exist_ok=True)
        args.out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('S9 waffle clocks: named indices verified, no runtime lease')
        return 0
    except (OSError,ValueError,KeyError,FormatError) as exc:
        print(f'S9 clock audit REFUSED: {exc}',file=sys.stderr)
        return 1
if __name__=='__main__':raise SystemExit(main())
