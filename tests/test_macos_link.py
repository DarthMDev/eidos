"""Exercise optional Mach-O symbols with the actual host compiler/linker."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import macos_link


class Symbols(unittest.TestCase):
    def test_nm(self):
        weak, required = macos_link.weak_symbols("""
                 (undefined) weak external _optional
                 (undefined) external _required
0000000000000000 (__TEXT,__text) weak external _defined
""")
        self.assertEqual(weak, {"_optional"})
        self.assertEqual(required, {"_required"})

    @unittest.skipUnless(sys.platform == "darwin", "requires Apple ld")
    def test_native_link(self):
        wrapper = Path(macos_link.__file__)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "main.c"
            source.write_text("extern int optional(void) __attribute__((weak));\n"
                              "int main(void) { return optional ? optional() : 0; }\n")
            obj = root / "main.o"
            subprocess.run(["cc", "-c", str(source), "-o", str(obj)], check=True)
            exe = root / "probe"
            command = [sys.executable, str(wrapper), "--nm", "/usr/bin/nm", "--",
                       "cc", str(obj), "-o", str(exe)]
            subprocess.run(command, check=True)
            self.assertEqual(subprocess.run([str(exe)]).returncode, 0)
            provider = root / "provider.c"
            provider.write_text("int optional(void) { return 7; }\n")
            subprocess.run(command + [str(provider)], check=True)
            self.assertEqual(subprocess.run([str(exe)]).returncode, 7)
            source.write_text("extern int required(void);\n"
                              "int main(void) { return required(); }\n")
            subprocess.run(["cc", "-c", str(source), "-o", str(obj)], check=True)
            failed = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(failed.returncode, 0)
            self.assertIn("_required", failed.stderr)


if __name__ == "__main__":
    unittest.main()
