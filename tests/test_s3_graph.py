"""Synthetic FDT-only provider-resolution tests. Nothing here is from a real device."""
import struct
import subprocess
import shutil
import tempfile
import sys
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from inspect_firmware import parse_fdt
from dt_graph import build_gpu_graph
from gpu_probe import gpu_dt_inventory


class FdtFactory:
    def __init__(self):
        self.buf=bytearray()
        self.string_table=bytearray()
        self.names={}
    def word(self,n): self.buf.extend(struct.pack('>I',n))
    def start(self,name):
        self.word(1);self.buf.extend(name.encode()+b'\0')
        self.buf.extend(bytes(-len(self.buf)%4))
    def end(self): self.word(2)
    def prop(self,name,data):
        if name not in self.names:
            self.names[name]=len(self.string_table)
            self.string_table.extend(name.encode()+b'\0')
        self.word(3);self.word(len(data));self.word(self.names[name]);self.buf.extend(data)
        self.buf.extend(bytes(-len(self.buf)%4))
    def nums(self,name,*nums): self.prop(name,struct.pack(f'>{len(nums)}I',*nums))
    def strs(self,name,*strings): self.prop(name,b'\0'.join(x.encode() for x in strings)+b'\0')
    def finish(self):
        self.word(9)
        h=struct.pack('>10I',0xd00dfeed,56+len(self.buf)+len(self.string_table),56,
                      56+len(self.buf),40,17,16,0,len(self.string_table),len(self.buf))
        return h+bytes(16)+self.buf+self.string_table


def fdt_with_providers(dangling=False, wrong_width=False):
    d=FdtFactory()
    d.start('');d.strs('compatible','qcom,pineapple');d.nums('#address-cells',2);d.nums('#size-cells',2)
    d.start('soc');d.nums('#address-cells',2);d.nums('#size-cells',2);d.prop('ranges',b'')
    d.start('intc@1');d.strs('compatible','arm,gic-v3');d.nums('phandle',3);d.nums('#interrupt-cells',3);d.end()
    d.start('gpu-smmu@2');d.strs('compatible','arm,smmu-v2');d.nums('phandle',2)
    d.nums('#iommu-cells',1 if not wrong_width else 32);d.nums('reg',0,0x2000,0,0x1000);d.end()
    d.start('gpucc@3');d.strs('compatible','qcom,sm8650-gpucc');d.nums('phandle',4);d.nums('#clock-cells',1);d.end()
    d.start('pwr@4');d.strs('compatible','qcom,sm8650-gdsc');d.nums('phandle',5);d.nums('#power-domain-cells',1);d.end()
    d.start('kgsl-3d0');d.strs('compatible','qcom,kgsl-3d0');d.nums('reg',0,0x10000000,0,0x40000)
    d.strs('reg-names','kgsl_3d0_reg_memory');d.nums('iommus',99 if dangling else 2,0)
    d.nums('clocks',4,7);d.nums('power-domains',5,1)
    d.nums('interrupts-extended',3,0,300,4)
    d.start('gmu@10001000');d.strs('compatible','qcom,adreno-gmu');d.nums('reg',0,0x10001000,0,0x1000);d.end()
    d.end();d.end();d.end()
    return d.finish()

class GraphTests(unittest.TestCase):
    def test_valid_provider_graph_is_syntactic_only(self):
        f=parse_fdt(fdt_with_providers())
        graph=f['gpu_graph']
        self.assertEqual(graph['candidate_count'],3)
        self.assertEqual(graph['phandle_count'],4)
        self.assertFalse(graph['usable_for_gpu_hw_programming'])
        gpu=next(n for n in graph['nodes'] if n['role_candidate']=='gpu')
        self.assertEqual(gpu['registers']['entries'][0]['address'],'0x10000000')
        self.assertEqual(gpu['registers']['entries'][0]['translation']['cpu_physical_address'],'0x10000000')
        self.assertEqual(gpu['links']['iommus']['items'][0]['provider_path'],'/soc/gpu-smmu@2')
        self.assertEqual(gpu['links']['iommus']['items'][0]['argument_cells'],[0])
        self.assertEqual(gpu['links']['clocks']['items'][0]['argument_cells'],[7])
        self.assertEqual(gpu['links']['power-domains']['items'][0]['argument_cells'],[1])
        self.assertEqual(gpu['interrupts']['items'][0]['specifier_cells'],[0,300,4])
        self.assertFalse(gpu['hardware_owner_verified'])
        self.assertFalse(gpu['safe_to_access_mmio'])
        out=gpu_dt_inventory({'format':'fdt','fdt':f})
        self.assertEqual(out['resource_graphs'][0]['candidate_count'],3)
    def test_missing_provider_does_not_get_guessed(self):
        graph=parse_fdt(fdt_with_providers(dangling=True))['gpu_graph']
        gpu=next(n for n in graph['nodes'] if n['role_candidate']=='gpu')
        self.assertEqual(gpu['links']['iommus']['status'],'unresolved-provider')
        self.assertFalse(graph['smmu_stream_context_owned'])
    def test_unsupported_provider_width_is_not_fake_success(self):
        graph=parse_fdt(fdt_with_providers(wrong_width=True))['gpu_graph']
        gpu=next(n for n in graph['nodes'] if n['role_candidate']=='gpu')
        self.assertEqual(gpu['links']['iommus']['status'],'unresolved-cells')
    def test_no_nodes_stays_unqualified(self):
        self.assertEqual(build_gpu_graph({'/':{}})['candidate_count'],0)
    def test_interrupt_parent_unresolved_does_not_forge_irq(self):
        n={'/':{},'/gpu@1':{'compatible':b'qcom,adreno-gpu\0',
                            'interrupts':struct.pack('>3I',0,35,4)}}
        result=build_gpu_graph(n)['nodes'][0]['interrupts']
        self.assertEqual(result['status'],'interrupt-parent-absent-or-unresolved')
    def test_missing_bus_ranges_is_not_assumed_to_be_physical(self):
        v=struct.pack('>I',2)
        n={'/':{'#address-cells':struct.pack('>I',2),'#size-cells':v},
           '/soc':{'#address-cells':struct.pack('>I',2),'#size-cells':v},
           '/soc/gpu@100':{'compatible':b'qcom,adreno-gpu\0',
                           'reg':struct.pack('>4I',0,0x100,0,0x80)}}
        addr=build_gpu_graph(n)['nodes'][0]['registers']['entries'][0]['translation']
        self.assertEqual(addr['status'],'bus-ranges-missing')
    def test_explicit_bus_ranges_translate(self):
        w=lambda *args:struct.pack(f'>{len(args)}I',*args)
        n={'/':{'#address-cells':w(2),'#size-cells':w(2)},
           '/soc':{'#address-cells':w(2),'#size-cells':w(2),
                   'ranges':w(0,0x10000000,0,0x90000000,0,0x10000)},
           '/soc/gpu@10001000':{'compatible':b'qcom,adreno-gpu\0',
                                'reg':w(0,0x10001000,0,0x1000)}}
        addr=build_gpu_graph(n)['nodes'][0]['registers']['entries'][0]['translation']
        self.assertEqual(addr['cpu_physical_address'],'0x90001000')
    def test_reg_cells_reject_bad_stride(self):
        n={'/':{'#address-cells':struct.pack('>I',2),'#size-cells':struct.pack('>I',2)},
           '/gpu@1':{'compatible':b'qcom,adreno-gpu\0','reg':struct.pack('>3I',0,1,2)}}
        self.assertEqual(build_gpu_graph(n)['nodes'][0]['registers']['status'],'invalid-reg-length')

class FirmwareCPlannerTests(unittest.TestCase):
    def test_native_gmu_parser(self):
        cc=shutil.which('cc')
        if not cc:self.skipTest('C compiler unavailable')
        with tempfile.TemporaryDirectory() as directory:
            binary=Path(directory)/'test-gmu'
            r=subprocess.run([cc,'-std=c11','-Wall','-Wextra','-Werror','-pedantic',
                '-I'+str(ROOT/'gpu'),str(ROOT/'gpu/waffle_gmu_fw_plan.c'),
                str(ROOT/'gpu/host_fw_plan_test.c'),'-o',str(binary)],
                capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            self.assertIn('passed',run.stdout)

if __name__=='__main__':unittest.main()
