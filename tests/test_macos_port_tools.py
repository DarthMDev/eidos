"""Regressions for the current pointer-sized Aletheia source export."""
from pathlib import Path
import sys
import subprocess
import shutil
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import armdata
import dsaddr
import wasm32_layout
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/lp64"))
import layouts


class PortTools(unittest.TestCase):
    @unittest.skipUnless(shutil.which("clang"), "clang required for packet layout")
    def test_wasm_packet_alignment_is_scoped(self):
        original = ("typedef struct GdsPacketDownload {\n"
                    "unsigned short reserved0, reserved1;\nlong long key;\n"
                    "} GdsPacketDownload;\n"
                    "struct Other { unsigned value; long long key; };\n")
        patched = wasm32_layout.packet_header(original)
        # Packet is 12 bytes with key at byte 4. The following unrelated
        # record keeps the target's eight-byte alignment and size of 16.
        checks = ("typedef char packet_size[sizeof(GdsPacketDownload) == 12 ? 1 : -1];\n"
                  "typedef char packet_key[__builtin_offsetof(GdsPacketDownload, key) == 4 ? 1 : -1];\n"
                  "typedef char other_size[sizeof(struct Other) == 16 ? 1 : -1];\n")
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "packet.c"
            source.write_text(patched + checks)
            subprocess.run([shutil.which("clang"), "--target=wasm32-unknown-unknown",
                            "-fsyntax-only", str(source)], check=True)
        with self.assertRaises(ValueError):
            wasm32_layout.packet_header(original.replace("long long key;", "int key;"))

    def test_layout_compilation_arguments(self):
        self.assertEqual(layouts.flags_of({"arguments": [
            "clang", "-I", "dir with spaces", "-DABI=1", "-std=c99",
            "-c", "input.c", "-o", "output.o"]}),
            ["-I", "dir with spaces", "-DABI=1", "-std=c99"])

    @unittest.skipUnless(shutil.which("clang"), "clang required for ABI comparison")
    def test_layouts_compare_ds_mac_and_wasm(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "fixture.c"
            source.write_text("struct Item { void *pointer; int flag; };\nstruct Item item;\n")
            records = {}
            for target in (layouts.REFERENCE, "arm64-apple-macos11", "wasm32-unknown-unknown"):
                records[target], error = layouts.dump((shutil.which("clang"), target,
                    [], str(source), str(root / (target + ".ll")), str(root)))
                self.assertIsNone(error)
            self.assertEqual(records[layouts.REFERENCE]["struct Item"],
                             records["wasm32-unknown-unknown"]["struct Item"])
            self.assertNotEqual(records[layouts.REFERENCE]["struct Item"],
                                records["arm64-apple-macos11"]["struct Item"])
            source.write_text("this is deliberately invalid C;\n")
            result, error = layouts.dump((shutil.which("clang"), layouts.REFERENCE,
                [], str(source), str(root / "invalid.ll"), str(root)))
            self.assertIsNone(result)
            self.assertIn("error:", error)

    def test_shared_card_header_keeps_wire_layout(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_dir = root / "src/game/arm9"
            output_dir = root / "host"
            source_dir.mkdir(parents=True)
            header = source_dir / "card_common.h"
            original = ("typedef struct CARDiCommandArg {\n"
                        "    uptr src;\n    uptr dst;\n} CARDiCommandArg;\n"
                        "typedef struct CARDiCommon {\n"
                        "    uptr src;\n    uptr dst;\n} CARDiCommon;\n")
            header.write_text(original)
            source = source_dir / "fixture.c"
            source.write_text('#include "card_common.h"\n')
            output = output_dir / "fixture.c"
            subprocess.run([sys.executable, str(Path(dsaddr.__file__)),
                            str(source), "--cut-asm", "-o", str(output)], check=True)
            rewritten = (output_dir / "card_common.h").read_text()
            command, common = rewritten.split("typedef struct CARDiCommon")
            self.assertIn("u32 src;", command)
            self.assertIn("u32 dst;", command)
            self.assertIn("uptr src;", common)
            self.assertIn(str(output_dir / "card_common.h"), output.read_text())
            self.assertEqual(header.read_text(), original)

    def test_wrapped_register_address(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "ioreg.h").write_text(
                "#define IOREG_ADDR(a) ((uptr)(a))\n"
                "#define REG_SCFG_ROM_ADDR IOREG_ADDR(0x04004000)\n")
            source = '#include "ioreg.h"\nunsigned char f(void) {\n' \
                     'return *(volatile unsigned char *)REG_SCFG_ROM_ADDR;\n}\n'
            path = root / "test.c"
            path.write_text(source)
            result = dsaddr._rewrite_with_defines(source, str(path), str(path))
            self.assertIn("bw_ds_ptr(REG_SCFG_ROM_ADDR)", result)
            self.assertEqual(path.read_text(), source)

    def test_header_declared_data(self):
        source = """/*
 * symbols:
 *   gSharedTable 0x021D6338 data ov115
 */
#include "shared.h"
void *f(void) { return &gSharedTable; }
"""
        parsed = armdata.SourceFile("fixture.c", source, True)
        self.assertIn("gSharedTable", parsed.extern_objects)
        local = source.replace("return &gSharedTable;",
                               "int gSharedTable = 0; return 0;")
        self.assertNotIn("gSharedTable",
                         armdata.SourceFile("fixture.c", local, True).extern_objects)


if __name__ == "__main__":
    unittest.main()
