"""S9 C checks, native clock lease simulation only; no hardware."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]

class S9NativeTests(unittest.TestCase):
    def test_clock_and_smmu_readback_gate(self):
        cc=shutil.which('clang') or shutil.which('gcc')
        if not cc:self.skipTest('Host C compiler unavailable')
        with tempfile.TemporaryDirectory(prefix='waffle-s9-') as d:
            cmd=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-pedantic']
            if os.getenv('WAFFLE_SANITIZE')=='1':
                cmd+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            cmd+=['-I',str(ROOT/'gpu')]
            cmd += [str(ROOT/'gpu'/f) for f in (
                'waffle_s9_clock_runtime.c','waffle_a750_hw_s5.c',
                'waffle_gpu_lease.c','host_s9_runtime_test.c')]
            cmd+=['-o',str(Path(d)/'s9-test')]
            r=subprocess.run(cmd,capture_output=True,text=True,timeout=40)
            self.assertEqual(r.returncode,0,r.stderr)
            p=subprocess.run([str(Path(d)/'s9-test')],capture_output=True,text=True,timeout=40)
            self.assertEqual(p.returncode,0,p.stdout+'\n'+p.stderr)
            self.assertIn('no MMIO',p.stdout)
    def test_no_unsafe_register_backend(self):
        c=(ROOT/'gpu/waffle_s9_clock_runtime.c').read_text()
        for forbidden in ('writel(', 'fastboot', '/dev/mem', 'ioremap(', 'mmio_write('):
            self.assertNotIn(forbidden,c)

if __name__=='__main__':unittest.main()
