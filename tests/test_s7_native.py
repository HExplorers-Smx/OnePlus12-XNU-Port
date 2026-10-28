"""S7 only proves host-owned LPAE page tables and HFI-SCRATCH model semantics."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
FILES=("waffle_smmu_pt.c", "waffle_s7_scratch.c", "waffle_gpu_lease.c",
       "waffle_hfi_alloc.c", "waffle_hfi_ring.c", "waffle_hfi_runtime.c",
       "host_s7_smmu_scratch_test.c")

class S7Native(unittest.TestCase):
    def test_smmu_and_scratch(self):
        compiler=shutil.which('clang') or shutil.which('gcc')
        if not compiler:self.skipTest('No C compiler')
        with tempfile.TemporaryDirectory(prefix='waffle-s7-') as tmp:
            exe=Path(tmp)/'check'
            args=[compiler,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                  '-pedantic']
            if os.getenv('WAFFLE_SANITIZE')=='1':
                args+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            args += [str(ROOT/'gpu'/f) for f in FILES]+['-o',str(exe)]
            build=subprocess.run(args,capture_output=True,text=True,timeout=30)
            self.assertEqual(build.returncode,0,build.stderr)
            check=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(check.returncode,0,check.stderr or check.stdout)
            self.assertIn('host checks passed',check.stdout)

    def test_no_device_writes(self):
        for name in ('waffle_smmu_pt.c','waffle_s7_scratch.c'):
            text=(ROOT/'gpu'/name).read_text()
            for unsafe in ('/dev/mem','fastboot','/dev/block/', 'ioremap(', 'writel('):
                self.assertNotIn(unsafe,text)

    def test_no_false_hardware_proof(self):
        import json
        p=ROOT/'boards/gpu/waffle_a750_verified_16_501.json'
        board=json.loads(p.read_text())
        self.assertFalse(board['gpu_smmu_ownership_acquired'])
        self.assertFalse(board['gmu_hfi_handshake_verified'])
        self.assertFalse(board['ios_metal_acceleration_verified'])

if __name__=='__main__':unittest.main()
