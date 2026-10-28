"""S6 host tests; fake DMA/cache and tokens do not prove phone hardware status."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]

class S6Native(unittest.TestCase):
    def test_ring_and_allocator_session(self):
        compiler=shutil.which('clang') or shutil.which('gcc')
        if not compiler:self.skipTest('No native C compiler')
        files=['waffle_hfi_alloc.c','waffle_hfi_ring.c','waffle_hfi_runtime.c',
               'waffle_gpu_lease.c','host_s6_ring_runtime_test.c']
        with tempfile.TemporaryDirectory(prefix='waffle-s6-') as tmp:
            executable=Path(tmp)/'run'
            args=[compiler,'-std=c11','-Wall','-Wextra','-Werror','-pedantic',
                  '-O1','-g']
            if os.getenv('WAFFLE_SANITIZE')=='1':
                args+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            args += [str(ROOT/'gpu'/f) for f in files]+['-o',str(executable)]
            build=subprocess.run(args,capture_output=True,text=True,timeout=30)
            self.assertEqual(build.returncode,0,build.stderr)
            test=subprocess.run([str(executable)],capture_output=True,text=True,timeout=30)
            self.assertEqual(test.returncode,0,test.stderr or test.stdout)
            self.assertIn('passed',test.stdout)

    def test_no_production_mmio_or_firmware_embedded(self):
        for name in ('waffle_hfi_ring.c','waffle_hfi_runtime.c'):
            src=(ROOT/'gpu'/name).read_text(encoding='utf-8')
            for token in ('/dev/mem','/dev/block','ioctl(', 'mmap('):
                self.assertNotIn(token,src)

    def test_unsafe_hardware_claims_explicitly_false(self):
        import json
        data=json.loads((ROOT/'boards/gpu/waffle_a750_verified_16_501.json').read_text())
        for field in ('gpu_hardware_powered_by_xnu','gpu_smmu_ownership_acquired',
                      'gmu_hfi_handshake_verified','gpu_command_submitted',
                      'hardware_fence_completed','ios_metal_acceleration_verified'):
            self.assertFalse(data[field],field)

    def test_manual_actions_only(self):
        wf=(ROOT/'.github/workflows/board-ci.yml').read_text()
        self.assertIn('workflow_dispatch:',wf)
        self.assertNotIn('  push:',wf)
        self.assertNotIn('  pull_request:',wf)

if __name__=='__main__':unittest.main()
