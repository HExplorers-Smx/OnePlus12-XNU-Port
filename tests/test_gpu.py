"""Tests use synthetic DTBs and firmware, not OEM GPU blobs."""
import hashlib
import json
import struct
import zipfile
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from gpu_probe import gpu_kind,gpu_dt_inventory,firmware_inventory,report,hash_file
from test_firmware import build_fdt

def make_synthetic_gpu_fdt():
    """Minimal synthetic DT; physical addresses have no relationship to a phone."""
    names=('compatible','model','#address-cells','#size-cells','reg','reg-names',
           'iommus','interrupts','interrupt-names')
    strings=b''.join(n.encode()+b'\0' for n in names)
    offsets={n:strings.index(n.encode()+b'\0') for n in names}
    ops=bytearray()
    def u32(v): ops.extend(struct.pack('>I',v))
    def node(n):
        u32(1);ops.extend(n.encode()+b'\0');ops.extend(bytes(-len(ops)%4))
    def prop(n,d):
        u32(3);u32(len(d));u32(offsets[n]);ops.extend(d);ops.extend(bytes(-len(ops)%4))
    node('')
    prop('compatible',b'qcom,pineapple\0')
    prop('model',b'Pineapple MTP,waffle\0')
    prop('#address-cells',struct.pack('>I',2))
    prop('#size-cells',struct.pack('>I',2))
    node('soc')
    prop('#address-cells',struct.pack('>I',2))
    prop('#size-cells',struct.pack('>I',2))
    node('gpu@20000000')
    prop('compatible',b'qcom,adreno-gpu-gen7-9-1\0')
    prop('reg',struct.pack('>4I',0,0x20000000,0,0x1000))
    prop('reg-names',b'kgsl_3d0_reg_memory\0')
    prop('iommus',struct.pack('>2I',77,0))
    prop('interrupts',struct.pack('>3I',0,123,4))
    prop('interrupt-names',b'kgsl-3d0-irq\0')
    u32(2)
    node('gmu@20001000')
    prop('compatible',b'qcom,adreno-gmu\0')
    prop('reg',struct.pack('>4I',0,0x20001000,0,0x1000))
    u32(2)
    node('gpu-smmu@20002000')
    prop('compatible',b'arm,smmu-v2\0')
    prop('reg',struct.pack('>4I',0,0x20002000,0,0x1000))
    u32(2);u32(2);u32(2);u32(9)
    reserved=bytes(16)
    header=struct.pack('>10I',0xd00dfeed,40+16+len(ops)+len(strings),56,
                       56+len(ops),40,17,16,0,len(strings),len(ops))
    return header+reserved+ops+strings

class GpuProbeTests(unittest.TestCase):
    def test_zipped_stock_like_dtb(self):
        with tempfile.TemporaryDirectory() as t:
            bundle=Path(t)/'dtbo.zip'
            with zipfile.ZipFile(bundle,'w') as z:
                z.writestr('dtbo.img',make_synthetic_gpu_fdt())
            x=report(bundle,None,'PJD110_TEST')
            self.assertEqual(x['device_tree']['count'],3)
            self.assertEqual(x['dt_image_format'],'fdt')
            self.assertFalse(x['gpu_acceleration_available'])
    def test_zipped_ambiguous_images_rejected(self):
        with tempfile.TemporaryDirectory() as t:
            bundle=Path(t)/'ambiguous.zip'
            with zipfile.ZipFile(bundle,'w') as z:
                z.writestr('dtbo.img',make_synthetic_gpu_fdt())
                z.writestr('other.img',make_synthetic_gpu_fdt())
            with self.assertRaises(ValueError):report(bundle,None,'TEST')

    def test_positive_synthetic_gpu_dtb(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'synthetic.dtb';p.write_bytes(make_synthetic_gpu_fdt())
            out=report(p,None,'TEST')
            nodes=out['device_tree']['candidates']
            self.assertEqual({n['role_candidate'] for n in nodes},
                             {'gpu','gmu','gpu_smmu_candidate'})
            gpu=next(n for n in nodes if n['role_candidate']=='gpu')
            self.assertEqual(gpu['reg'][0]['address'],'0x20000000')
            self.assertEqual(gpu['reg_names'][0],'kgsl_3d0_reg_memory')
            self.assertEqual(gpu['iommus_raw_cells'],[77,0])
            self.assertEqual(gpu['interrupts_raw_cells'],[0,123,4])
            self.assertFalse(out['device_tree']['smmu_context_ownership_confirmed'])
            self.assertFalse(out['gpu_acceleration_available'])

    def test_gpu_role(self):
        self.assertEqual(gpu_kind({'path':'/soc/gpu@3d00000','compatible':['qcom,adreno-gpu-gen7-9-1']}),'gpu')
        self.assertEqual(gpu_kind({'path':'/soc/gpu@3d00000/gmu@3d68000','compatible':['qcom,adreno-gmu']}),'gmu')
        self.assertEqual(gpu_kind({'path':'/soc/gpu-smmu@3da0000','compatible':['arm,smmu-v2']}),'gpu_smmu_candidate')
        self.assertIsNone(gpu_kind({'path':'/soc/ufs@123','compatible':['qcom,ufshc']}))
    def test_no_device_address_is_guessed(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'test.dtb';p.write_bytes(build_fdt())
            x=report(p,None,'PJD110_16.0.10.501(CN01)')
            self.assertFalse(x['device_tree']['mmio_safe_to_access'])
            self.assertFalse(x['gpu_acceleration_available'])
            self.assertEqual(x['device_tree']['count'],0)
            self.assertEqual(x['dt_image_sha256'],hashlib.sha256(build_fdt()).hexdigest())
    def test_firmware_missing(self):
        x=firmware_inventory(None,'TEST')
        self.assertFalse(x['complete'])
        self.assertFalse(x['authenticated'])
        self.assertEqual(len(x['missing']),4)
    def test_firmware_metadata_not_content(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'gmu_gen70900.bin'; p.write_bytes(b'FAKE GMU FIXTURE')
            x=firmware_inventory(Path(t),'TEST')
            self.assertEqual(len(x['found']),1)
            self.assertFalse(x['complete'])
            self.assertNotIn('FAKE',json.dumps(x))
            self.assertFalse(x['found'][0]['verified_signature'])
    def test_empty_firmware_rejected(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'gmu_gen70900.bin';p.touch()
            with self.assertRaises(ValueError): hash_file(p)
    def test_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'test.fw';p.write_bytes(b'x')
            link=Path(t)/'gmu_gen70900.bin';link.symlink_to(p)
            with self.assertRaises(ValueError):hash_file(link)
    def test_build_required(self):
        with self.assertRaises(ValueError):report(None,None,'')
        with self.assertRaises(ValueError):report(None,None,'BUILD\nINJECTION')
    def test_cli_readonly(self):
        with tempfile.TemporaryDirectory() as t:
            dtb=Path(t)/'test.dtb';dtb.write_bytes(build_fdt())
            result=subprocess.run([sys.executable,str(ROOT/'tools/gpu_probe.py'),'--dt-image',str(dtb),'--build','SYNTHETIC'],capture_output=True,text=True,check=True)
            self.assertTrue(json.loads(result.stdout)['offline_only'])
            self.assertTrue(dtb.exists())
    def test_not_gpu_wrong_image(self):
        with tempfile.TemporaryDirectory() as t:
            p=Path(t)/'bad.dtb';p.write_bytes(b'NO DTB')
            with self.assertRaises(ValueError): report(p,None,'TEST')
    def test_phandle_unresolved(self):
        x=gpu_dt_inventory({'format':'fdt','fdt':{'root_compatible':['qcom,pineapple'],'selected_nodes':[{'path':'/soc/gpu@3d00000','compatible':['qcom,adreno-gpu-gen7-9-1'],'iommus_raw_cells':[5,0]}]}})
        self.assertEqual(x['candidates'][0]['iommus_raw_cells'],[5,0])
        self.assertFalse(x['phandles_resolved'])

class NativeGateTests(unittest.TestCase):
    def test_native_platform_contract(self):
        cc=shutil.which('cc')
        if cc is None: self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as t:
            exe=Path(t)/'adapter'
            result=subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror','-pedantic',
                '-I'+str(ROOT/'gpu'),str(ROOT/'gpu/waffle_gpu_gate.c'),
                str(ROOT/'gpu/waffle_gpu_platform.c'),
                str(ROOT/'gpu/host_platform_test.c'),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            self.assertIn('real backends still absent',run.stdout)

    def test_native_host_gate(self):
        cc=shutil.which('cc')
        if cc is None: self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as t:
            exe=Path(t)/'gate'
            result=subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror','-pedantic',
                '-I'+str(ROOT/'gpu'),str(ROOT/'gpu/waffle_gpu_gate.c'),
                str(ROOT/'gpu/host_gate_test.c'),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            self.assertIn('host tests passed',run.stdout)

if __name__=='__main__':unittest.main()
