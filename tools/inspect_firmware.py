#!/usr/bin/env python3
"""Read-only Android boot/vendor_boot/DTB/DTBO metadata inspector.

Never writes source images or constructs flashable payloads. Malformed input fails closed.
Only structure metadata and cryptographic hashes are emitted.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
from pathlib import Path
from dt_graph import build_gpu_graph

DTB_MAGIC = 0xd00dfeed
DTBO_MAGIC = 0xd7b7ab1e
MAX_STRUCTURE = 64 * 1024 * 1024
MAX_ENTRIES = 1024

class FormatError(ValueError):
    pass

def be32(buf: bytes, off: int) -> int:
    if off < 0 or off + 4 > len(buf):
        raise FormatError(f"truncated big-endian uint32 at {off}")
    return struct.unpack_from('>I', buf, off)[0]

def le32(buf: bytes, off: int) -> int:
    if off < 0 or off + 4 > len(buf):
        raise FormatError(f"truncated little-endian uint32 at {off}")
    return struct.unpack_from('<I', buf, off)[0]

def align(n: int, page: int) -> int:
    if page <= 0 or page & (page-1) or page > 65536:
        raise FormatError(f"invalid page size: {page}")
    return (n + page - 1) & ~(page - 1)

def span(data: bytes, off: int, length: int, what: str) -> bytes:
    if off < 0 or length < 0 or off + length > len(data):
        raise FormatError(f"{what} extends beyond image")
    return data[off:off+length]

def cstrings(raw: bytes) -> list[str]:
    return [x.decode('utf-8','replace')[:250] for x in raw.split(b'\0') if x]

def parse_fdt(blob: bytes) -> dict:
    if len(blob) < 40 or be32(blob, 0) != DTB_MAGIC:
        raise FormatError('not a Flattened Device Tree')
    total, off_struct, off_strings, off_reserved, version, last_ver, _, size_strings, size_struct = struct.unpack_from('>9I', blob, 4)
    if version < 16 or version > 17 or last_ver > version:
        raise FormatError('unsupported FDT version')
    if total < 40 or total > len(blob) or total > MAX_STRUCTURE:
        raise FormatError('invalid FDT total size')
    structure = span(blob[:total], off_struct, size_struct, 'FDT structure')
    strings = span(blob[:total], off_strings, size_strings, 'FDT strings')
    span(blob[:total], off_reserved, 16, 'FDT reserved map')
    at = 0
    paths = []
    nodes: dict[str, dict[str, bytes]] = {}
    finished = False
    while at + 4 <= len(structure):
        tag = be32(structure, at); at += 4
        if tag == 1:  # FDT_BEGIN_NODE
            end = structure.find(b'\0', at)
            if end < 0 or end >= len(structure) or end-at > 200:
                raise FormatError('malformed FDT node name')
            name = structure[at:end].decode('utf-8', 'replace')
            at = align(end + 1, 4)
            if not paths and name != '':
                raise FormatError('FDT root must be unnamed')
            path = '/' if not paths else paths[-1].rstrip('/') + '/' + name
            paths.append(path)
            if len(paths) > 64 or len(nodes) > 20000:
                raise FormatError('FDT resource limit exceeded')
            nodes[path] = {}
        elif tag == 2:  # FDT_END_NODE
            if not paths:
                raise FormatError('unmatched FDT_END_NODE')
            paths.pop()
        elif tag == 3:  # FDT_PROP
            if not paths or at + 8 > len(structure):
                raise FormatError('malformed FDT_PROP')
            length = be32(structure, at)
            nameoff = be32(structure, at + 4)
            at += 8
            if length > 1_000_000 or nameoff >= len(strings):
                raise FormatError('unreasonably large FDT property')
            end = strings.find(b'\0', nameoff)
            if end < 0 or end-nameoff > 200:
                raise FormatError('invalid FDT property name')
            key = strings[nameoff:end].decode('ascii', 'replace')
            nodes[paths[-1]][key] = span(structure, at, length, 'FDT property')
            at = align(at + length, 4)
        elif tag == 4:  # FDT_NOP
            continue
        elif tag == 9:  # FDT_END
            if paths:
                raise FormatError('FDT ended inside node')
            finished = True
            break
        else:
            raise FormatError(f'unknown FDT token {tag}')
    if not finished or '/' not in nodes:
        raise FormatError('FDT missing root or terminator')

    def cell(data: bytes | None, default: int) -> int:
        return be32(data, 0) if data is not None and len(data) >= 4 else default
    def ancestor(path: str) -> str:
        p=path.rsplit('/', 1)[0]
        return p or '/'
    def parent_cells(path: str) -> tuple[int,int]:
        parent=ancestor(path)
        p=nodes.get(parent, {})
        # #address-cells and #size-cells for parent bus; fall back to FDT common root defaults
        return cell(p.get('#address-cells'),2), cell(p.get('#size-cells'),1)
    def decode_reg(path: str, raw: bytes) -> list[dict]:
        addr_cells,size_cells=parent_cells(path)
        if not (0 <= addr_cells <= 4 and 0 <= size_cells <= 4) or addr_cells + size_cells == 0:
            return []
        stride=4*(addr_cells+size_cells)
        if len(raw) % stride:
            return []
        items=[]
        for o in range(0,min(len(raw), stride*16),stride):
            addr=int.from_bytes(raw[o:o+addr_cells*4],'big') if addr_cells else 0
            size=int.from_bytes(raw[o+addr_cells*4:o+stride],'big') if size_cells else None
            items.append({'address':f'0x{addr:x}', 'size_bytes':size})
        return items
    out={'fdt_version':version,'total_size':total,'node_count':len(nodes),
         'root_compatible':cstrings(nodes['/'].get('compatible',b'')),
         'root_model':cstrings(nodes['/'].get('model',b''))[:1],
         'selected_nodes':[], 'gpu_graph':build_gpu_graph(nodes)}
    needles=('reserved-memory','qcom,ipa','qcom,ipcc','qcom,rpmh-rsc','qcom,smem',
             'qcom,msm-imem','qcom,smmu','qcom,mdss','qcom,dsi','qcom,ufshc',
             'qcom,qupv3','qcom,usb','qcom,pcie','touchscreen','goodix','synaptics','focaltech',
             'qcom,adreno','qcom,gmu','qcom,gpu','qcom,kgsl','arm,smmu','qcom,smmu')
    for path, props in nodes.items():
        compat=cstrings(props.get('compatible',b''))
        if not (path.startswith('/reserved-memory/') or
                any(word in ' '.join(compat).lower() for word in needles)):
            continue
        entry={'path':path,'compatible':compat[:20]}
        if 'reg' in props:
            entry['reg'] = decode_reg(path,props['reg'])
        if 'reg-names' in props:
            entry['reg_names']=cstrings(props['reg-names'])[:16]
        if 'interrupt-names' in props:
            entry['interrupt_names']=cstrings(props['interrupt-names'])[:16]
        # Phandle cells are unresolved until the referenced provider is decoded.
        # Do NOT mistake GPU SID / IRQ phandle data for raw IRQ or context-bank IDs.
        for pname in ('iommus','interrupts','interrupts-extended','qcom,chipid'):
            raw=props.get(pname)
            if raw is not None:
                entry[pname.replace(',','_').replace('-','_')+'_raw_cells'] = [be32(raw,i) for i in range(0,min(len(raw)//4,32)*4,4)] if len(raw)%4==0 else []
        if 'no-map' in props:
            entry['no_map']=True
        if 'status' in props:
            entry['status']=cstrings(props['status'])[:1]
        if 'qcom,pas-id' in props and len(props['qcom,pas-id']) >= 4:
            entry['pas_id']=be32(props['qcom,pas-id'],0)
        out['selected_nodes'].append(entry)
    return out

def inspect_bytes(data: bytes) -> dict:
    if data.startswith(b'ANDROID!'):
        version=le32(data,40)
        if version not in (3,4):
            return {'format':'android-boot','header_version':version,'supported':False}
        if len(data) < 1580:
            raise FormatError('truncated Android boot header')
        kernel_size, ramdisk_size, header_size=le32(data,8),le32(data,12),le32(data,20)
        if header_size not in (1580,1584):
            raise FormatError(f'unexpected boot header size {header_size}')
        if header_size != (1584 if version == 4 else 1580):
            raise FormatError('boot header version/size mismatch')
        end=4096+align(kernel_size,4096)+align(ramdisk_size,4096)
        if end > len(data):
            raise FormatError('boot payload truncated')
        return {'format':'android-boot','header_version':version,'header_size':header_size,
                'kernel_size':kernel_size,'ramdisk_size':ramdisk_size,'payload_end':end}
    if data.startswith(b'VNDRBOOT'):
        if len(data) < 2112:
            raise FormatError('truncated vendor_boot header')
        version,page=le32(data,8),le32(data,12)
        if version not in (3,4):
            return {'format':'android-vendor-boot','header_version':version,'supported':False}
        header_size=le32(data,2096)
        if header_size != (2112 if version == 3 else 2128):
            raise FormatError('vendor_boot version/size mismatch')
        align(header_size,page)
        ramdisk,dtbsize=le32(data,24),le32(data,2100)
        dtb_offset=align(header_size,page)+align(ramdisk,page)
        dtb=span(data,dtb_offset,dtbsize,'vendor_boot DTB')
        out={'format':'android-vendor-boot','header_version':version,'page_size':page,
             'vendor_ramdisk_size':ramdisk,'dtb_size':dtbsize,'dtb_offset':dtb_offset}
        if dtbsize >= 40 and dtb.startswith(struct.pack('>I',DTB_MAGIC)):
            out['embedded_dtb']=parse_fdt(dtb)
        if version==4:
            out['vendor_ramdisk_table_size']=le32(data,2112)
            out['vendor_ramdisk_table_entries']=le32(data,2116)
            out['bootconfig_size']=le32(data,2124)
        return out
    if data.startswith(struct.pack('>I',DTB_MAGIC)):
        return {'format':'fdt','fdt':parse_fdt(data)}
    if data.startswith(struct.pack('>I',DTBO_MAGIC)):
        if len(data) < 32:
            raise FormatError('truncated DTBO table header')
        _,total,header_size,entry_size,count,entry_off,page,version=struct.unpack_from('>8I',data)
        if total>len(data) or total<32 or header_size<32 or entry_size<32 or count>MAX_ENTRIES or entry_off<header_size:
            raise FormatError('invalid DTBO table header')
        if entry_off+entry_size*count>total:
            raise FormatError('DTBO entry table out of bounds')
        entries=[]
        for index in range(count):
            size,offset,hwid,rev,*custom=struct.unpack_from('>8I',data,entry_off+index*entry_size)
            blob=span(data[:total],offset,size,'DTBO entry')
            entries.append({'index':index,'id':hwid,'rev':rev,'custom':custom,
                'offset':offset,'size':size,'dtb_sha256':hashlib.sha256(blob).hexdigest(),
                'fdt':parse_fdt(blob)})
        return {'format':'dtbo-table','version':version,'total_size':total,
                'page_size':page,'entry_count':count,'entries':entries}
    raise FormatError('unknown format (expected ANDROID!, VNDRBOOT, DTB or DTBO table)')

def inspect_file(path: Path) -> dict:
    if not path.is_file():
        raise FormatError('file not found')
    if path.stat().st_size > 512*1024*1024:
        raise FormatError('image exceeds 512 MiB inspection limit')
    # 512MiB cap is deliberate; no partition writes or extraction performed.
    data=path.read_bytes()
    parsed=inspect_bytes(data)
    return {'schema':1,'source_filename':path.name,'sha256':hashlib.sha256(data).hexdigest(),
            'file_size_bytes':len(data),'verified_real_device':False,
            'not_flashable':True,**parsed}

def main():
    p=argparse.ArgumentParser(description='Offline read-only firmware metadata inspection (no flashing)')
    p.add_argument('files',type=Path,nargs='+',help='local DTB / DTBO / boot / vendor_boot files')
    p.add_argument('--out',type=Path,help='JSON output; omit for stdout')
    args=p.parse_args()
    results=[]
    for f in args.files:
        try:
            results.append(inspect_file(f))
        except (OSError,FormatError,struct.error) as exc:
            results.append({'source_filename':f.name,'error':str(exc),'not_flashable':True})
    payload={'schema':1,'type':'offline-image-inspection','images':results}
    txt=json.dumps(payload,ensure_ascii=False,indent=2)+'\n'
    if args.out:
        args.out.parent.mkdir(parents=True,exist_ok=True)
        args.out.write_text(txt,encoding='utf-8')
    else:print(txt,end='')
    return 1 if any('error' in x for x in results) else 0

if __name__=='__main__':
    raise SystemExit(main())
