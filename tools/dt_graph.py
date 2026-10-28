"""Read-only DT GPU resource graph; provider phandles are not ownership grants.

Input is a mapping of FDT node paths to raw property bytes, from the strict
FDT tokenizer in inspect_firmware.py. A missing provider stays unresolved;
DTBO overlays cannot be treated as a standalone, fully applied board tree.
"""
from __future__ import annotations
import struct
from typing import Mapping

MAX_NODE_REFERENCES = 256
REFERENCE_PROPERTIES = {
    'iommus': '#iommu-cells',
    'clocks': '#clock-cells',
    'power-domains': '#power-domain-cells',
    'resets': '#reset-cells',
    'interconnects': '#interconnect-cells',
    'mboxes': '#mbox-cells',
    'phys': '#phy-cells',
}


def _cells(raw: bytes) -> list[int] | None:
    if len(raw) % 4 or len(raw) > 64 * 1024:
        return None
    return list(struct.unpack(f'>{len(raw)//4}I', raw)) if raw else []


def _strs(raw: bytes) -> list[str]:
    return [x.decode('utf-8', 'replace')[:96] for x in raw.split(b'\0') if x][:32]


def _kind(path: str, props: Mapping[str, bytes]) -> str | None:
    leaf=path.rsplit('/',1)[-1].lower()
    comp=' '.join(_strs(props.get('compatible', b''))).lower()
    if 'gmu@' in leaf or 'adreno-gmu' in comp or 'qcom,gmu' in comp:
        return 'gmu'
    if ('smmu' in leaf or 'smmu' in comp) and any(x in path.lower()+' '+comp for x in ('gpu','kgsl','gfx')):
        return 'gpu_smmu_candidate'
    if leaf.startswith(('gpu@','kgsl-3d0','kgsl@')) or any(x in comp for x in ('adreno-gpu','kgsl-3d0','qcom,gpu')):
        return 'gpu'
    return None


def _ancestor(path: str) -> str:
    parent = path.rsplit('/', 1)[0]
    return parent or '/'


def _address_width(nodes: Mapping[str, Mapping[str, bytes]], parent: str) -> tuple[int,int]:
    p=nodes.get(parent, {})
    a=_cells(p.get('#address-cells', b''))
    s=_cells(p.get('#size-cells', b''))
    return ((a[0] if a and len(a)==1 else 2), (s[0] if s and len(s)==1 else 1))



def _physical_address(nodes: Mapping[str, Mapping[str, bytes]], bus: str,
                      address: int, size: int) -> dict:
    """Translate reg bus addresses through explicit FDT ranges only.

    Empty `ranges` asserts identity on a transparent bus; absent ranges does NOT.
    Even an unambiguous physical address is not permission for MMIO access.
    """
    translated=[]
    while bus != '/':
        parent=_ancestor(bus)
        props=nodes.get(bus,{})
        if 'ranges' not in props:
            return {'status':'bus-ranges-missing','at_bus':bus}
        raw=props['ranges']
        if not raw:
            translated.append({'bus':bus,'method':'explicit-identity'})
            bus=parent
            continue
        values=_cells(raw)
        c_addr,_=_address_width(nodes,bus)
        p_addr,_=_address_width(nodes,parent)
        _,size_cells=_address_width(nodes,bus)
        if values is None or not (1<=c_addr<=4 and 1<=p_addr<=4 and 1<=size_cells<=4):
            return {'status':'ranges-invalid','at_bus':bus}
        width=c_addr+p_addr+size_cells
        if len(values)%width or len(values)//width>128:
            return {'status':'ranges-invalid','at_bus':bus}
        candidates=[]
        for start in range(0,len(values),width):
            fields=values[start:start+width]
            child_addr=0; parent_addr=0; region_size=0
            for value in fields[:c_addr]:child_addr=(child_addr<<32)|value
            for value in fields[c_addr:c_addr+p_addr]:parent_addr=(parent_addr<<32)|value
            for value in fields[c_addr+p_addr:]:region_size=(region_size<<32)|value
            if region_size>0 and address>=child_addr and size<=region_size and \
                    address-child_addr<=region_size-size:
                candidates.append(parent_addr+address-child_addr)
        if len(candidates)!=1:
            return {'status':'ranges-ambiguous-or-unmapped','at_bus':bus,'match_count':len(candidates)}
        address=candidates[0]
        translated.append({'bus':bus,'method':'explicit-map'})
        bus=parent
    return {'status':'decoded-cpu-address-without-access-grant',
            'cpu_physical_address':f'0x{address:x}','translation':translated}

def _decode_regs(nodes: Mapping[str, Mapping[str, bytes]], path: str) -> dict:
    raw=nodes[path].get('reg')
    if raw is None:
        return {'status':'missing','entries':[]}
    cells=_cells(raw)
    if cells is None:
        return {'status':'invalid-cell-encoding','entries':[]}
    addr_cells,size_cells=_address_width(nodes,_ancestor(path))
    if not (1 <= addr_cells <= 4 and 1 <= size_cells <= 4):
        return {'status':'unsupported-address-size-cells','entries':[]}
    step=addr_cells+size_cells
    if len(cells)%step or len(cells)//step>64:
        return {'status':'invalid-reg-length','entries':[]}
    names=_strs(nodes[path].get('reg-names',b''))
    items=[]
    for n,start in enumerate(range(0,len(cells),step)):
        address=0
        size=0
        for v in cells[start:start+addr_cells]: address=(address<<32)|v
        for v in cells[start+addr_cells:start+step]: size=(size<<32)|v
        items.append({'name':names[n] if n<len(names) else None,
                      'address':f'0x{address:x}','size_bytes':size,
                      'nonempty':size>0,
                      'translation':_physical_address(nodes,_ancestor(path),address,size)})
    return {'status':'decoded-bus-relative-no-ranges-translation', 'entries':items}


def build_gpu_graph(nodes: Mapping[str, Mapping[str, bytes]]) -> dict:
    phandles={}
    duplicate_phandles=[]
    for path, props in nodes.items():
        for prop_name in ('phandle','linux,phandle'):
            if prop_name not in props: continue
            raw=_cells(props[prop_name])
            if raw is None or len(raw)!=1 or raw[0] in (0,0xffffffff):
                duplicate_phandles.append(f'invalid:{path}:{prop_name}')
                continue
            ph=raw[0]
            if ph in phandles and phandles[ph]!=path:
                duplicate_phandles.append(f'0x{ph:x}')
            else:
                phandles[ph]=path

    def provider_cells(path: str, count_prop: str) -> int | None:
        raw=_cells(nodes[path].get(count_prop,b''))
        if raw is None or len(raw)!=1 or raw[0]>16:
            return None
        return raw[0]

    def references(prop: str, raw: bytes) -> dict:
        nums=_cells(raw)
        if nums is None:
            return {'status':'malformed','items':[]}
        entries=[]
        at=0
        while at<len(nums):
            if len(entries)>=MAX_NODE_REFERENCES:
                return {'status':'too-many-references','items':entries}
            ph=nums[at]
            at+=1
            provider=phandles.get(ph)
            if provider is None:
                entries.append({'provider_phandle':f'0x{ph:x}','status':'provider-absent'})
                return {'status':'unresolved-provider','items':entries,
                        'remaining_raw_cells':nums[at:at+12]}
            ncells=provider_cells(provider,REFERENCE_PROPERTIES[prop])
            if ncells is None or at+ncells>len(nums):
                entries.append({'provider_phandle':f'0x{ph:x}','provider_path':provider,
                                'status':'cell-width-unavailable-or-truncated'})
                return {'status':'unresolved-cells','items':entries}
            entries.append({'provider_phandle':f'0x{ph:x}','provider_path':provider,
                            'argument_cells':nums[at:at+ncells],
                            'status':'dt-specifier-decoded-not-authorized'})
            at+=ncells
        return {'status':'syntactically-resolved-no-ownership', 'items':entries}

    def interrupt_spec(path: str, props: Mapping[str,bytes]) -> dict:
        if 'interrupts-extended' in props:
            nums=_cells(props['interrupts-extended'])
            if nums is None:return {'status':'invalid-cell-encoding','items':[]}
            entries=[]
            at=0
            while at<len(nums):
                if len(entries)>=MAX_NODE_REFERENCES:
                    return {'status':'too-many-interrupts','items':entries}
                ph=nums[at];at+=1
                provider=phandles.get(ph)
                if provider is None:
                    return {'status':'provider-absent','items':entries,'raw_tail':nums[at-1:at+12]}
                ncells=provider_cells(provider,'#interrupt-cells')
                if ncells is None or ncells==0 or at+ncells>len(nums):
                    return {'status':'invalid-provider-cells','items':entries}
                entries.append({'controller_path':provider, 'specifier_cells':nums[at:at+ncells],
                                'status':'controller-relative-not-a-linux-irq'})
                at+=ncells
            return {'status':'syntactically-resolved-no-irq-lease','items':entries}
        raw=props.get('interrupts')
        if raw is None:return {'status':'missing','items':[]}
        nums=_cells(raw)
        if nums is None:return {'status':'invalid-cell-encoding','items':[]}
        parent=path
        found=None
        while parent in nodes:
            if 'interrupt-parent' in nodes[parent]:
                found=_cells(nodes[parent]['interrupt-parent']);break
            if parent=='/':break
            parent=_ancestor(parent)
        if not found or len(found)!=1 or found[0] not in phandles:
            return {'status':'interrupt-parent-absent-or-unresolved','raw_cells':nums[:24], 'items':[]}
        provider=phandles[found[0]]
        ncells=provider_cells(provider,'#interrupt-cells')
        if ncells is None or not ncells or len(nums)%ncells or len(nums)//ncells>MAX_NODE_REFERENCES:
            return {'status':'invalid-provider-cells','raw_cells':nums[:24], 'items':[]}
        return {'status':'syntactically-resolved-no-irq-lease', 'items':[
            {'controller_path':provider, 'specifier_cells':nums[i:i+ncells],
             'status':'controller-relative-not-a-linux-irq'} for i in range(0,len(nums),ncells)]}

    results=[]
    for path,props in nodes.items():
        kind=_kind(path,props)
        if kind is None:continue
        record={'path':path,'role_candidate':kind,
                'compatible':_strs(props.get('compatible',b'')),
                'status':_strs(props.get('status',b'\0'))[:1],
                'registers':_decode_regs(nodes,path),
                'interrupts':interrupt_spec(path,props),
                'links':{},
                'hardware_owner_verified':False,
                'safe_to_access_mmio':False}
        for name in REFERENCE_PROPERTIES:
            if name in props:
                record['links'][name]=references(name,props[name])
        for name,raw in props.items():
            if name.endswith('-supply'):
                nums=_cells(raw)
                provider=phandles.get(nums[0]) if nums and len(nums)==1 else None
                record['links'][name]={'status':'syntactically-resolved-no-ownership' if provider else 'unresolved',
                                      'provider_path':provider}
        results.append(record)
    return {'schema':1,'nodes':results,'candidate_count':len(results),
            'phandle_count':len(phandles),'duplicate_phandles':duplicate_phandles,
            'source_is_applied_runtime_fdt':False,
            'bus_address_ranges_translated':False,
            'smmu_stream_context_owned':False,
            'power_domains_lease_owned':False,
            'irq_lease_owned':False,
            'usable_for_gpu_hw_programming':False}
