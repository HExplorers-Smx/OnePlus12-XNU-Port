"""S10 portable WFLG parser and fail-closed runtime observation contract."""
from pathlib import Path
import importlib.util
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
_spec=importlib.util.spec_from_file_location("s10_decode_log", ROOT/"tools/s10_decode_log.py")
_decoder=importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_decoder)


class S10NativeTests(unittest.TestCase):
    @staticmethod
    def build(directory):
        cc=shutil.which("clang") or shutil.which("gcc")
        if cc is None:
            raise unittest.SkipTest("Host compiler not available")
        output=Path(directory)/"s10-host"
        cmd=[cc,"-std=c11","-O1","-g","-Wall","-Wextra","-Werror","-pedantic"]
        if os.getenv("WAFFLE_SANITIZE")=="1":
            cmd += ["-fsanitize=address,undefined","-fno-omit-frame-pointer"]
        cmd += ["-I",str(ROOT/"boot"),*(str(ROOT/"boot"/x) for x in
                ("waffle_s10_log.c","waffle_s10_observer.c","host_s10_log_observer_test.c")),
                "-o",str(output)]
        p=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
        if p.returncode != 0:
            raise AssertionError(p.stderr)
        return output

    def test_host_native_and_python_cross_decode(self):
        with tempfile.TemporaryDirectory(prefix="s10-") as d:
            exe=self.build(d)
            log=Path(d)/"boot.bin"
            p=subprocess.run([str(exe),str(log)],capture_output=True,text=True,timeout=30)
            self.assertEqual(0,p.returncode,p.stderr)
            self.assertIn("PASS",p.stdout)
            obj=_decoder.decode(log.read_bytes())
            self.assertEqual(obj["frames"][0]["sequence"],13)
            self.assertEqual(obj["frames"][0]["payload_utf8"],"UEFI: read-only probe")
            self.assertFalse(obj["transport_proven"])
            self.assertFalse(obj["hardware_started_proven"])

    def test_crc_and_truncated_payload_rejected(self):
        import binascii
        payload=b"test"
        header=b"WFLG"+bytes([1,1])+b"\x00\x00"+(1).to_bytes(4,"little") +len(payload).to_bytes(2,"little")+b"\x00\x00"
        crc=binascii.crc32(header+payload).to_bytes(4,"little")
        framed=header+crc+payload
        self.assertEqual(_decoder.decode(framed)["frames"][0]["payload_utf8"],"test")
        with self.assertRaisesRegex(ValueError,"CRC"):
            _decoder.decode(framed[:-1]+b"X")
        with self.assertRaisesRegex(ValueError,"truncated"):
            _decoder.decode(framed[:-1])
        with self.assertRaisesRegex(ValueError,"bad magic"):
            _decoder.decode(b"x"+framed)
        recovered=_decoder.decode(b"x"+framed,strict=False)
        self.assertEqual(recovered["discarded_prefix_bytes"],1)

    def test_unsupported_headers_and_bounds_rejected(self):
        import binascii
        h=bytearray(b"WFLG"+bytes([1,1])+b"\x00\x00"+bytes(4)+bytes(2)+bytes(2))
        h[14]=1
        fake=bytes(h)+binascii.crc32(bytes(h)).to_bytes(4,"little")
        with self.assertRaisesRegex(ValueError,"unsupported"):
            _decoder.decode(fake)
        with self.assertRaisesRegex(ValueError,"32 MiB"):
            _decoder.decode(bytes(32*1024*1024+1))

    def test_uefi_is_information_only(self):
        c=(ROOT/"uefi/WaffleS10Probe/WaffleS10Probe.c").read_text()
        self.assertIn("LocateHandleBuffer",c)
        for token in ("SetVariable(", "WriteBlocks(", "SetAttributes(", "IoWrite", "PciWrite", "fastboot flash"):
            self.assertNotIn(token,c)

    def test_no_mmio_write_or_usb_success_claim(self):
        for name in ("waffle_s10_observer.c","waffle_s10_log.c"):
            c=(ROOT/"boot"/name).read_text()
            for token in ("writel(", "ioremap(", "/dev/mem", "fastboot flash"):
                self.assertNotIn(token,c)

if __name__=="__main__":
    unittest.main()
