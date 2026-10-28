"""Fixture images contain synthetic bytes only; no OEM or Apple material."""
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from inspect_firmware import inspect_bytes,inspect_file,parse_fdt,FormatError
from ingest_report import parse_report,ReportError


def build_fdt():
    strings=b'compatible\0model\0#address-cells\0#size-cells\0reg\0no-map\0'
    offs={s:strings.index(s.encode()+b'\0') for s in ('compatible','model','#address-cells','#size-cells','reg','no-map')}
    structbytes=bytearray()
    def u(n):structbytes.extend(struct.pack('>I',n))
    def node(name):
        u(1);structbytes.extend(name.encode()+b'\0')
        structbytes.extend(b'\0'*(-len(structbytes)%4))
    def prop(name,data):
        u(3);u(len(data));u(offs[name]);structbytes.extend(data)
        structbytes.extend(b'\0'*(-len(structbytes)%4))
    node('')
    prop('compatible',b'qcom,pineapple\0qcom,mtp\0')
    prop('model',b'Pineapple MTP,waffle\0')
    prop('#address-cells',struct.pack('>I',2))
    prop('#size-cells',struct.pack('>I',2))
    node('reserved-memory')
    prop('#address-cells',struct.pack('>I',2))
    prop('#size-cells',struct.pack('>I',2))
    node('mpss@90000000')
    prop('reg',struct.pack('>4I',0,0x90000000,0,0x400000))
    prop('no-map',b'')
    u(2);u(2)
    node('soc')
    prop('#address-cells',struct.pack('>I',2))
    prop('#size-cells',struct.pack('>I',2))
    node('ipcc@408000')
    prop('compatible',b'qcom,ipcc\0')
    prop('reg',struct.pack('>4I',0,0x408000,0,0x1000))
    u(2);u(2);u(2);u(9)
    while len(structbytes)%4:structbytes.append(0)
    reserved=b'\0'*16
    off_rsv=40
    off_struct=off_rsv+len(reserved)
    off_strings=off_struct+len(structbytes)
    total=off_strings+len(strings)
    header=struct.pack('>10I',0xd00dfeed,total,off_struct,off_strings,off_rsv,17,16,0,len(strings),len(structbytes))
    return header+reserved+structbytes+strings

class FirmwareTests(unittest.TestCase):
    def setUp(self):self.dtb=build_fdt()
    def test_dtb_root(self):
        result=inspect_bytes(self.dtb)
        self.assertEqual(result['format'],'fdt')
        self.assertEqual(result['fdt']['root_compatible'][0],'qcom,pineapple')
        self.assertEqual(result['fdt']['node_count'],5)
    def test_reserved_region(self):
        nodes=parse_fdt(self.dtb)['selected_nodes']
        reserve=next(x for x in nodes if 'mpss' in x['path'])
        self.assertEqual(reserve['reg'][0]['address'],'0x90000000')
        self.assertEqual(reserve['reg'][0]['size_bytes'],0x400000)
        self.assertTrue(reserve['no_map'])
        ipcc=next(x for x in nodes if 'ipcc' in x['path'])
        self.assertEqual(ipcc['reg'][0]['address'],'0x408000')
    def test_fdt_truncated(self):
        with self.assertRaises(FormatError):parse_fdt(self.dtb[:50])
    def test_fdt_bad_magic(self):
        with self.assertRaises(FormatError):parse_fdt(b'JUNK'+self.dtb[4:])
    def test_dtbo_table(self):
        off=64; total=off+len(self.dtb)
        header=struct.pack('>8I',0xd7b7ab1e,total,32,32,1,32,4096,0)
        entry=struct.pack('>8I',len(self.dtb),off,12,3,0,0,0,0)
        r=inspect_bytes(header+entry+self.dtb)
        self.assertEqual(r['format'],'dtbo-table')
        self.assertEqual(r['entries'][0]['rev'],3)
        self.assertEqual(r['entries'][0]['fdt']['root_model'][0],'Pineapple MTP,waffle')
    def test_dtbo_bad_entry(self):
        header=struct.pack('>8I',0xd7b7ab1e,64,32,32,1,32,4096,0)
        entry=struct.pack('>8I',400,64,0,0,0,0,0,0)
        with self.assertRaises(FormatError):inspect_bytes(header+entry)
    def test_boot_v4(self):
        data=bytearray(4096)
        data[:8]=b'ANDROID!'
        struct.pack_into('<I',data,20,1584)
        struct.pack_into('<I',data,40,4)
        r=inspect_bytes(bytes(data))
        self.assertEqual(r['header_version'],4)
        self.assertEqual(r['payload_end'],4096)
    def test_boot_truncated(self):
        data=bytearray(1584)
        data[:8]=b'ANDROID!'
        struct.pack_into('<I',data,8,12345)
        struct.pack_into('<I',data,20,1584)
        struct.pack_into('<I',data,40,4)
        with self.assertRaises(FormatError):inspect_bytes(bytes(data))
    def test_vendor_boot_v4(self):
        data=bytearray(4096)+bytearray(self.dtb)
        data[:8]=b'VNDRBOOT'
        struct.pack_into('<I',data,8,4)
        struct.pack_into('<I',data,12,4096)
        struct.pack_into('<I',data,2096,2128)
        struct.pack_into('<I',data,2100,len(self.dtb))
        r=inspect_bytes(bytes(data))
        self.assertEqual(r['format'],'android-vendor-boot')
        self.assertEqual(r['embedded_dtb']['root_compatible'][0],'qcom,pineapple')
    def test_vendor_bad_page_size(self):
        data=bytearray(4096)
        data[:8]=b'VNDRBOOT'
        struct.pack_into('<I',data,8,4)
        struct.pack_into('<I',data,12,777)
        struct.pack_into('<I',data,2096,2128)
        with self.assertRaises(FormatError):inspect_bytes(bytes(data))
    def test_safe_manifest(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'test.dtb';p.write_bytes(self.dtb)
            doc=inspect_file(p)
            self.assertEqual(doc['sha256'],__import__('hashlib').sha256(self.dtb).hexdigest())
            self.assertTrue(doc['not_flashable'])
            self.assertFalse(doc['verified_real_device'])

class ReportTests(unittest.TestCase):
    def test_waffle_report(self):
        fixture='''snapshot.root_ok: true\n## BUILD_AND_BOOT\nro.product.model: PJD110\nro.soc.model: SM8650\nro.build.display.id: TEST\nsecret: serial-123\n## DEVICE_TREE\n/proc/device-tree/compatible\nqcom,pineapple\n\n/proc/device-tree/model\nPineapple MTP,waffle\n## DISPLAY\nPhysical size: 1440x3168\ndevice: /sys/class/drm/card0-DSI-1\nstatus: connected\nmodes: 1440x3168x60cmd\n1440x3168x120cmd\n## GPU\n/sys/class/kgsl/kgsl-3d0/gpu_model: Adreno750v2\n## PARTITIONS_ALL_NAMES\nboot_a\nboot_b\n## PARTITION_SIZES_BYTES\nboot_a: 201326592\n## STORAGE\n/sys/class/scsi_host/host0/proc_name: ufshcd\n'''
        obj=parse_report(fixture)
        self.assertEqual(obj['partition_count'],2)
        self.assertEqual(obj['display']['modes']['DSI-1'],['1440x3168x60cmd','1440x3168x120cmd'])
        self.assertNotIn('secret',json.dumps(obj))
    def test_wrong_soc(self):
        with self.assertRaises(ReportError):parse_report('snapshot.root_ok: true\n## BUILD_AND_BOOT\nro.product.model: PJD110\nro.soc.model: SM8550\n## DEVICE_TREE')
    def test_root_denied(self):
        with self.assertRaises(ReportError):parse_report('snapshot.root_ok: false\n## BUILD_AND_BOOT\nro.product.model: PJD110\nro.soc.model: SM8650\n## DEVICE_TREE')

if __name__=='__main__':unittest.main()
