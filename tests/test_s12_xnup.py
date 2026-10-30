"""HakiOS XNUP compatibility and malformed payload protection tests."""
from __future__ import annotations
import ctypes, importlib.util, os, shutil, struct, subprocess, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('s12',ROOT/'tools/s12_pack_xnup.py')
s12=importlib.util.module_from_spec(spec);spec.loader.exec_module(s12)
class View(ctypes.Structure):
    _fields_=[('kernel_offset',ctypes.c_size_t),('kernel_bytes',ctypes.c_size_t),
              ('cmdline_offset',ctypes.c_size_t),('cmdline_bytes',ctypes.c_size_t),
              ('entries',ctypes.c_uint),('usb',ctypes.c_int)]
# Minimal *synthetic* 64-bit ARM64 Mach-O, not a bootable XNU kernel.
K=struct.pack('<IIIIIIII',0xfeedfacf,0x0100000c,0,2,1,8,0,0)+struct.pack('<II',0x19,8)
class XNUPTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cc=shutil.which('clang') or shutil.which('gcc')
        if not cc:raise unittest.SkipTest('compiler unavailable')
        cls.temp=tempfile.TemporaryDirectory(prefix='waffle-s12-')
        cls.cc=cc
        cls.so=Path(cls.temp.name)/'libxnup.so'
        c=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',str(ROOT/'boot/waffle_s12_xnup.c'),'-o',str(cls.so)]
        r=subprocess.run(c,capture_output=True,text=True)
        if r.returncode:raise AssertionError(r.stderr)
        cls.lib=ctypes.CDLL(str(cls.so));cls.fn=cls.lib.wa_s12_xnup_parse
        cls.fn.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.POINTER(View)];cls.fn.restype=ctypes.c_int
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def parse(self,b):
        v=View();raw=bytes(b);return self.fn(raw,len(raw),ctypes.byref(v)),v
    def test_hakios_wire_format(self):
        b=s12.make({'kernel':K,'usb':b'1\0'},'cpus=1')
        rc,v=self.parse(b)
        self.assertEqual(rc,0)
        self.assertEqual(v.entries,3)
        self.assertEqual(v.usb,1)
        self.assertEqual(b[v.kernel_offset:v.kernel_offset+v.kernel_bytes],K)
    def test_reject_bad_names_and_no_kernel(self):
        for name in ('', 'n'*16):
            with self.assertRaises(ValueError):s12.make({'kernel':K,name:b'x'})
        with self.assertRaises(ValueError):s12.make({})
    def test_truncation_corruption_duplicate(self):
        b=bytearray(s12.make({'kernel':K},'cpus=1'))
        self.assertEqual(self.parse(b[:-1])[0],-2)
        x=bytearray(b);x[0]=0;self.assertNotEqual(self.parse(x)[0],0)
        x=bytearray(b);x[8+20]=1;self.assertNotEqual(self.parse(x)[0],0)
        x=bytearray(b);x[8+16:8+20]=struct.pack('<I',0xffffffff);self.assertNotEqual(self.parse(x)[0],0)
        # Overwrite the second entry name with kernel: duplicate must fail
        off=8+24+((len(K)+15)&~15)
        x=bytearray(b);x[off:off+16]=b'kernel\0'+bytes(9)
        self.assertEqual(self.parse(x)[0],-3)
    def test_wrong_macho(self):
        x=bytearray(s12.make({'kernel':K}))
        x[8+24]=0
        self.assertEqual(self.parse(x)[0],-5)
    def test_host_native_and_sanitizer(self):
        with tempfile.TemporaryDirectory() as d:
            payload=Path(d)/'payload.xnup';payload.write_bytes(s12.make({'kernel':K},'cpus=1'))
            exe=Path(d)/'xnuptest'
            cmd=[self.cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                 str(ROOT/'boot/host_s12_xnup_test.c'),str(ROOT/'boot/waffle_s12_xnup.c'),'-o',str(exe)]
            if os.environ.get('WAFFLE_SANITIZE')=='1':cmd[1:1]=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
            r=subprocess.run(cmd,capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(exe),str(payload)],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
    def test_efi_entrypoint_with_mock_services(self):
        from tests.test_s11_native import make_fdt
        with tempfile.TemporaryDirectory() as d:
            d=Path(d)
            source=[ROOT/'uefi/WaffleS12Loader/waffle_s12_efi.c',
                    ROOT/'boot/waffle_s12_xnup.c',ROOT/'boot/waffle_s11_fdt.c',
                    ROOT/'boot/host_s12_efi_mock.c']
            exe=d/'uefi-mock'
            cmd=[self.cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                 '-I'+str(ROOT/'uefi/WaffleS12Loader'),'-I'+str(ROOT/'boot'),
                 *map(str,source),'-o',str(exe)]
            r=subprocess.run(cmd,capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            good=d/'good.dtb';good.write_bytes(make_fdt())
            bad=d/'bad.dtb';bad.write_bytes(make_fdt(model=b'Another model\0'))
            payload=d/'xnup.bin';payload.write_bytes(s12.make({'kernel':K},'cpus=1'))
            for f,mode,code,text in [
                (good,None,0,'S12 PASS'),(bad,None,1,'S12 FAIL'),
                (good,'badmap',1,'S12 FAIL'),(good,'nooptions',1,'S12 FAIL')]:
                cmd=[str(exe),str(f),str(payload)]+([mode] if mode else [])
                run=subprocess.run(cmd,capture_output=True,text=True)
                self.assertEqual(run.returncode,code,run.stderr+run.stdout)
                self.assertIn(text,run.stdout)
            payload.write_bytes(b'not-XNUP-data'*10)
            run=subprocess.run([str(exe),str(good),str(payload)],capture_output=True,text=True)
            self.assertEqual(run.returncode,1)
            self.assertIn('invalid',run.stdout)
    def test_efi_binary_is_real_arm64_pe_coff(self):
        with tempfile.TemporaryDirectory() as d:
            import shutil as _shutil
            r=subprocess.run(['bash',str(ROOT/'tools/s12_build_efi.sh')],cwd=ROOT,
                             capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            efi=(ROOT/'output/s12/WaffleS12Preflight.efi').read_bytes()
            self.assertEqual(efi[:2],b'MZ')
            peoff=struct.unpack_from('<I',efi,0x3c)[0]
            self.assertEqual(efi[peoff:peoff+4],b'PE\0\0')
            self.assertEqual(struct.unpack_from('<H',efi,peoff+4)[0],0xaa64)
            self.assertEqual(struct.unpack_from('<H',efi,peoff+24+68)[0],10) # EFI application
    def test_cross_compilation(self):
        cc=shutil.which('clang')
        if not cc:raise unittest.SkipTest('clang unavailable')
        r=subprocess.run([cc,'--target=aarch64-windows-msvc','-ffreestanding','-fsyntax-only',
                          '-std=c11','-Wall','-Wextra','-Werror',str(ROOT/'boot/waffle_s12_xnup.c')],capture_output=True,text=True)
        self.assertEqual(r.returncode,0,r.stderr)
if __name__=='__main__':unittest.main()
