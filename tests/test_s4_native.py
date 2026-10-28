"""Compile and run S4 native code. Host fakes never read MMIO or flash."""
from __future__ import annotations
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class S4Native(unittest.TestCase):
    def _run(self, *names: str) -> str:
        compiler=shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not compiler: self.skipTest('C compiler not found on host')
        with tempfile.TemporaryDirectory(prefix='waffle-s4-') as tmp:
            exe=Path(tmp)/'driver_test'
            cmd=[compiler,'-std=c11','-Wall','-Wextra','-Werror','-pedantic']
            if os.environ.get('WAFFLE_SANITIZE') == '1':
                cmd += ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            cmd += [str(ROOT/'gpu'/n) for n in names] + ['-o',str(exe)]
            build=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
            self.assertEqual(build.returncode,0,build.stderr)
            p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(p.returncode,0,p.stderr or p.stdout)
            return p.stdout

    def test_gen7_scratch_allocator(self):
        self.assertIn('passed',self._run('waffle_hfi_alloc.c','host_hfi_alloc_test.c'))

    def test_power_and_smmu_lease(self):
        self.assertIn('passed',self._run('waffle_gpu_lease.c','host_lease_test.c'))

    def test_unverified_waffle_hardware_not_claimed(self):
        import json
        pins=json.loads((ROOT/'boards/gpu/hfi_source_pins_s4.json').read_text())
        self.assertFalse(pins['matched_pjd110_fdt_binary_acquired'])
        self.assertFalse(pins['matched_pjd110_gmu_firmware_acquired'])
        self.assertFalse(pins['waffle_gpu_hardware_started'])
        self.assertFalse(pins['actual_fence_observed'])
        self.assertFalse(pins['metal_acceleration_verified'])
        stock=json.loads((ROOT/'boards/gpu/stock_release_16_501.json').read_text())
        self.assertTrue(all(a['sha256'] is None for a in stock['assets']))

    def test_no_production_mmio_backend(self):
        core=(ROOT/'gpu'/'waffle_hfi_alloc.c').read_text(encoding='utf-8')
        lease=(ROOT/'gpu'/'waffle_gpu_lease.c').read_text(encoding='utf-8')
        for candidate in ('/dev/mem','/dev/kgsl','/dev/block','ioctl(', 'mmap(', 'iowrite'):
            self.assertNotIn(candidate,core+lease)

    def test_manual_only_workflow(self):
        workflow=(ROOT/'.github/workflows/board-ci.yml').read_text(encoding='utf-8')
        self.assertIn('workflow_dispatch:',workflow)
        self.assertNotIn('  push:',workflow)
        self.assertNotIn('  pull_request:',workflow)

if __name__=='__main__': unittest.main()
