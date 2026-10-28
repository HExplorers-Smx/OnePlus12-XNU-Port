"""S9 pinned PJD110 OEM named clock topology; no power or SMMU writes."""
from pathlib import Path
import json,sys,unittest,copy,os,zipfile,subprocess
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from s9_clock_inventory import (analyze_nodes,ClockError,cells,names,inventory_fdt,
                                fdt_nodes,CLOCK_PATHS)
from generate_s9_clock_header import render
from qualify_evidence_s5 import read_zip_bundle


def prototype():
    """Tiny synthetic providers; only positive tests for phandle indexing."""
    def u(n):return n.to_bytes(4,'big')
    p={
        '/soc/clock-controller@100000':{'phandle':u(1),'#clock-cells':u(1)},
        '/soc/clock-controller@3d90000':{'phandle':u(2),'#clock-cells':u(1)},
        '/soc/power-controller@c300000':{'phandle':u(3),'#clock-cells':u(0)},
    }
    p[CLOCK_PATHS['gpu']]={'clock-names':b'gcc_gpu_memnoc_gfx\0gpu_cc_ahb\0apb_pclk\0',
        'clocks':u(1)+u(37)+u(2)+u(0)+u(3)+u(0)}
    p[CLOCK_PATHS['gmu']]={'clock-names':b'gmu_clk\0', 'clocks':u(2)+u(4)}
    p[CLOCK_PATHS['gpu_smmu']]={'clock-names':b'gpu_cc_hlos1_vote_gpu_smmu\0',
        'clocks':u(2)+u(18)}
    return p

class S9InventoryTests(unittest.TestCase):
    def test_pinned_clock_manifest(self):
        m=json.loads((ROOT/'boards/gpu/waffle_a750_s9_named_clocks.json').read_text())
        self.assertEqual(m['schema'],1)
        self.assertFalse(m['safe_to_write_mmio'])
        self.assertFalse(m['runtime_clock_ownership_proven'])
        gpu=m['clock_groups']['gpu']
        self.assertEqual([x['name'] for x in gpu['clocks']],
            ['gcc_gpu_memnoc_gfx','gcc_gpu_snoc_dvm_gfx','gpu_cc_ahb','apb_pclk'])
        self.assertEqual(gpu['zero_trailer_cells'],1)
        self.assertEqual(gpu['clocks'][-1]['provider_path'],'/soc/power-controller@c300000')
        self.assertEqual(gpu['clocks'][-1]['specifier'],[])
        self.assertEqual(len(m['clock_groups']['gmu']['clocks']),6)
        self.assertEqual(len(m['clock_groups']['gpu_smmu']['clocks']),4)
    def test_header_deterministic(self):
        manifest=json.loads((ROOT/'boards/gpu/waffle_a750_s9_named_clocks.json').read_text())
        self.assertEqual(render(manifest),(ROOT/'gpu/waffle_s9_clock_ids.h').read_text())
        self.assertIn('WA_S9_CLOCK_COUNT 9',render(manifest))
    def test_named_zero_cell_provider_and_padding(self):
        o=analyze_nodes(prototype())
        x=o['clock_groups']['gpu']
        self.assertEqual([e['name'] for e in x['clocks']],
            ['gcc_gpu_memnoc_gfx','gpu_cc_ahb','apb_pclk'])
        self.assertEqual(x['zero_trailer_cells'],1)
        self.assertEqual(x['clocks'][2]['specifier'],[])
    def test_nonzero_padding_must_reject(self):
        p=prototype();p[CLOCK_PATHS['gpu']]['clocks']+=bytes.fromhex('00000001')
        with self.assertRaises(ClockError):analyze_nodes(p)
    def test_duplicate_provider_must_reject(self):
        p=prototype();p['/soc/clock-controller@100000']['phandle']=(2).to_bytes(4,'big')
        with self.assertRaises(ClockError):analyze_nodes(p)
    def test_missing_clock_name_reject(self):
        p=prototype();p[CLOCK_PATHS['gpu']]['clock-names']=b'only\0too-many\0names\0here\0now\0'
        with self.assertRaises(ClockError):analyze_nodes(p)
    def test_missing_provider_reject(self):
        p=prototype();del p['/soc/power-controller@c300000']
        with self.assertRaises(ClockError):analyze_nodes(p)
    def test_invalid_clock_str_reject(self):
        with self.assertRaises(ClockError):names(b'abc')
        with self.assertRaises(ClockError):names(b'abc\0\0')
        with self.assertRaises(ClockError):cells(b'bad')
    def test_wrong_fdt_or_build_rejected(self):
        with self.assertRaises(ClockError):inventory_fdt(b'garbage')
        with self.assertRaises(ClockError):inventory_fdt(b'garbage',build='PJD110_17.0')
        with self.assertRaises(ClockError):fdt_nodes(b'bad')
    def test_private_fdt_optional(self):
        loc=os.getenv('ONEPLUS12_PRIVATE_EVIDENCE')
        if not loc:self.skipTest('Private evidence not in public source tree')
        b=read_zip_bundle(Path(loc))
        x=inventory_fdt(b['fdt.dtb'])
        expected=json.loads((ROOT/'boards/gpu/waffle_a750_s9_named_clocks.json').read_text())
        self.assertEqual(x,expected)

if __name__=='__main__':unittest.main()
