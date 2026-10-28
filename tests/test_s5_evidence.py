"""Offline proof checks. Synthetic fixtures do not embed vendor firmware."""
import hashlib
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from qualify_evidence_s5 import (EvidenceError,read_zip_bundle,capture_properties,
                                 parse_gmu,qualify,BUILD)

class EvidenceS5(unittest.TestCase):
    def test_gmu_synthetic_two_pass_format(self):
        def dw(*vs):return b''.join(v.to_bytes(4,'little') for v in vs)
        raw=dw(0x60000000,0,7,0x1000)+dw(0x4000,4,0,0)+b'ABCD'
        result=parse_gmu(raw)
        self.assertEqual(result['block_count'],2)
        self.assertEqual(result['persistent_prealloc_count'],1)
        self.assertFalse(result['firmware_authenticated'])

    def test_gmu_out_of_range_rejected(self):
        raw=(0xffffffff).to_bytes(4,'little')+(4).to_bytes(4,'little')+bytes(8)+b'ABCD'
        with self.assertRaises(EvidenceError):parse_gmu(raw)

    def test_gmu_overlap_rejected(self):
        def block(addr):return addr.to_bytes(4,'little')+(8).to_bytes(4,'little')+bytes(8)+b'abcdefgh'
        with self.assertRaisesRegex(EvidenceError,'overlapping'):
            parse_gmu(block(0x4000)+block(0x4004))

    def test_wrong_os_version_rejected(self):
        lines=('root.uid: 0\ncapture.complete: true\n'
               'prop.ro.product.model: PJD110\n'
               'prop.ro.board.platform: pineapple\n'
               'prop.ro.soc.model: SM8650\n'
               'prop.ro.boot.slot_suffix: _a\n'
               'dtbo.partition: dtbo_a\n'
               'prop.ro.build.display.id: PJD110_17.0.1(CN01)\n')
        with self.assertRaisesRegex(EvidenceError,'incompatible build'):
            capture_properties(lines.encode())

    def test_untrusted_extra_file_rejected(self):
        with tempfile.TemporaryDirectory() as p:
            zpath=Path(p)/'x.zip'
            with zipfile.ZipFile(zpath,'w') as z:
                z.writestr('capture.txt','root.uid: 0')
                z.writestr('userdata.img',b'NO')
            with self.assertRaises(EvidenceError):read_zip_bundle(zpath)

    def test_duplicate_file_name_rejected(self):
        import warnings
        with tempfile.TemporaryDirectory() as p:
            zpath=Path(p)/'x.zip'
            with warnings.catch_warnings():
                warnings.simplefilter('ignore',UserWarning)
                with zipfile.ZipFile(zpath,'w') as z:
                    z.writestr('fdt.dtb',b'123')
                    z.writestr('fdt.dtb',b'456')
            with self.assertRaises(EvidenceError):read_zip_bundle(zpath)

    def test_public_manifest_no_binary_payload(self):
        import json
        manifest=json.loads((ROOT/'boards/gpu/waffle_a750_verified_16_501.json').read_text())
        self.assertEqual(manifest['model'],'PJD110')
        self.assertEqual(manifest['dtbo']['entry_count'],12)
        self.assertEqual(manifest['gmu_firmware']['block_count'],15)
        self.assertFalse(manifest['contains_firmware_bytes'])
        self.assertFalse(manifest['gpu_hardware_powered_by_xnu'])
        self.assertFalse(manifest['ios_metal_acceleration_verified'])
        self.assertEqual(manifest['runtime_fdt']['smmu_stream_ids_not_context_banks']['gmu'],5)

    def test_native_identity_pin_and_no_hardware_promotion(self):
        cc=shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not cc:self.skipTest('No local C compiler')
        with tempfile.TemporaryDirectory() as d:
            exe=Path(d)/'test'
            args=[cc,'-std=c11','-Wall','-Wextra','-Werror','-pedantic',
                  str(ROOT/'gpu/waffle_a750_hw_s5.c'),
                  str(ROOT/'gpu/host_waffle_pin_s5_test.c'),'-o',str(exe)]
            b=subprocess.run(args,capture_output=True,text=True,timeout=20)
            self.assertEqual(b.returncode,0,b.stderr)
            r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20)
            self.assertEqual(r.returncode,0,r.stderr)
            self.assertIn('passed',r.stdout)

    def test_real_private_zip_when_available(self):
        path=os.getenv('ONEPLUS12_PRIVATE_EVIDENCE')
        if not path:self.skipTest('Private firmware ZIP deliberately not bundled')
        x=qualify(read_zip_bundle(Path(path)))
        self.assertEqual(x['gmu_firmware']['block_count'],15)
        self.assertEqual(x['runtime_fdt']['gpu_mmio']['gmu']['cpu_physical_base'],'0x3d68000')
        self.assertFalse(x['gpu_command_submitted'])

if __name__=='__main__':unittest.main()
