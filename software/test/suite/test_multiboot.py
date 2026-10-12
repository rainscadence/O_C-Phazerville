"""Exercise the real SCons post-build callback without compiling firmware."""
import os
from pathlib import Path
import runpy
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

SOFTWARE = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(SOFTWARE / "res"))


def record(address, kind, data=b""):
    raw = bytes([len(data), address >> 8, address & 255, kind]) + data
    return ":" + (raw + bytes([-sum(raw) & 255])).hex().upper() + "\n"


def fixture(base):
    # Pinned core bootdata.c: header 0x43, fixed physical start, 3 KiB CSF.
    ivt = struct.pack("<8I", 0x432000D1, base + 0x1401, 0, 0,
                      base + 0x1020, base + 0x1000, base + 0x1800, 0)
    boot = struct.pack("<3I", 0x60000000, 0x2400, 0)
    return (record(0, 4, (base >> 16).to_bytes(2, "big")) +
            record(0, 0, b"FCFB") +
            record(0x1000, 0, ivt + boot) + record(0x1400, 0, b"\x00\xbf") +
            "".join(record(offset, 0, bytes(128)) for offset in range(0x1800, 0x2400, 128)) +
            record(0, 5, (base + 0x1000).to_bytes(4, "big")) + record(0, 1))


class Env(dict):
    def __init__(self, directory):
        super().__init__(BUILD_FLAGS=[], PIOENV="T41_MTP")
        self.directory = directory
    def Append(self, **kwargs):
        pass
    def Replace(self, **kwargs):
        self.update(kwargs)
    def ParseFlags(self, flags):
        return {"CPPDEFINES": [("OC_VERSION_EXTRA", '"_T41"')]}
    def subst(self, value):
        return value.replace("${PROGNAME}", self.get("PROGNAME", "firmware")).replace(
            "$BUILD_DIR", str(self.directory / "T41_MTP")).replace(
            "$PROJECT_DIR", str(SOFTWARE))
    def PioPlatform(self):
        return self
    def get_package_dir(self, name):
        return str(self.directory)
    def AddPostAction(self, target, callback):
        self.callback = callback


class HookTests(unittest.TestCase):
    def test_merge_failure_must_fail_build(self):
        with tempfile.TemporaryDirectory() as directory:
            env = Env(Path(directory))
            for profile, base in [("T41_audio", 0x60000000),
                                  ("T41", 0x60100000), ("T41_MTP", 0x60200000)]:
                folder = env.directory / profile
                folder.mkdir()
                (folder / "firmware.hex").write_text(fixture(base))
            previous = Path.cwd()
            try:
                os.chdir(SOFTWARE)
                with patch("subprocess.check_output", return_value=b"test\n"):
                    runpy.run_path(str(SOFTWARE / "res/assemble-multiboot.py"),
                                   init_globals={"Import": lambda _: None, "env": env})
                    with patch("subprocess.call", return_value=7), patch(
                            "subprocess.run", side_effect=subprocess.CalledProcessError(7, "srec_cat")):
                        with self.assertRaises(subprocess.CalledProcessError):
                            env.callback([], [], env)
            finally:
                os.chdir(previous)



class ImageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.inputs = {}
        import suite_image
        self.image = suite_image
        for profile, base in suite_image.SLOTS.items():
            path = self.directory / (profile + ".hex")
            path.write_text(fixture(base))
            self.inputs[profile] = path

    def tearDown(self):
        self.temporary.cleanup()

    def combined(self):
        output = self.directory / "suite.hex"
        lines = []
        for profile in self.image.SLOTS:
            for line in self.inputs[profile].read_text().splitlines():
                if line[7:9] == "01" or (profile != "T41_audio" and line[7:9] == "05"):
                    continue
                lines.append(line + "\n")
        output.write_text("".join(lines) + record(0, 1))
        return output

    def test_valid_exact_union(self):
        result = self.image.validate_suite(self.inputs, self.combined())
        self.assertEqual(result["validation"], "STRUCTURAL_PASS_ONLY")
        self.assertFalse(result["hardwareValidated"])
        self.assertEqual(len(result["profiles"]), 3)

    def test_corrupt_checksum_rejected(self):
        path = self.inputs["T41"]
        text = path.read_text()
        path.write_text(text[:10] + ("0" if text[10] != "0" else "1") + text[11:])
        with self.assertRaisesRegex(ValueError, "checksum"):
            self.image.validate_slot("T41", path)

    def test_wrong_slot_rejected(self):
        with self.assertRaisesRegex(ValueError, "outside"):
            self.image.validate_slot("T41_audio", self.inputs["T41"])

    def test_missing_profile_rejected(self):
        with self.assertRaisesRegex(ValueError, "exactly"):
            self.image.validate_suite({"T41": self.inputs["T41"]}, self.combined())

    def test_missing_eof_rejected(self):
        path = self.inputs["T41"]
        path.write_text(path.read_text().replace(record(0, 1), ""))
        with self.assertRaisesRegex(ValueError, "missing EOF"):
            self.image.read_hex(path)

    def test_data_after_eof_rejected(self):
        path = self.inputs["T41"]
        path.write_text(path.read_text() + record(0, 1))
        with self.assertRaisesRegex(ValueError, "after EOF"):
            self.image.read_hex(path)

    def test_duplicate_data_rejected(self):
        path = self.inputs["T41"]
        text = path.read_text()
        path.write_text(text.replace(record(0, 1), record(0x1400, 0, b"\x00\xbf") + record(0, 1)))
        with self.assertRaisesRegex(ValueError, "duplicate"):
            self.image.read_hex(path)

    def test_wrong_boot_pointer_rejected(self):
        path = self.inputs["T41"]
        path.write_text(path.read_text().replace(
            record(0x1400, 0, b"\x00\xbf"), record(0x1500, 0, b"\x00\xbf")))
        with self.assertRaisesRegex(ValueError, "reset entry"):
            self.image.validate_slot("T41", path)

    def test_valid_checksum_changed_output_rejected(self):
        path = self.combined()
        text = path.read_text()
        path.write_text(text.replace(record(0x1400, 0, b"\x00\xbf"),
                                     record(0x1400, 0, b"\x01\xbf"), 1))
        with self.assertRaisesRegex(ValueError, "exact three-slot union"):
            self.image.validate_suite(self.inputs, path)

    def test_wrong_combined_start_rejected(self):
        path = self.combined()
        path.write_text(path.read_text().replace(record(0, 5, bytes.fromhex("60001000")),
                                                record(0, 5, bytes.fromhex("60201000"))))
        with self.assertRaisesRegex(ValueError, "slot-0"):
            self.image.validate_suite(self.inputs, path)

    def test_failure_preserves_prior_output(self):
        path = self.combined()
        previous = path.read_bytes()
        with patch("subprocess.run", side_effect=subprocess.CalledProcessError(7, "srec_cat")):
            with self.assertRaises(subprocess.CalledProcessError):
                self.image.assemble("srec_cat", self.inputs, path)
        self.assertEqual(previous, path.read_bytes())

    def test_invalid_tool_output_not_published(self):
        path = self.directory / "not-published.hex"
        def bad_tool(args, **kwargs):
            Path(args[args.index("-o") + 1]).write_text(self.inputs["T41"].read_text())
        with patch("subprocess.run", side_effect=bad_tool):
            with self.assertRaisesRegex(ValueError, "exact three-slot union"):
                self.image.assemble("srec_cat", self.inputs, path)
        self.assertFalse(path.exists())

if __name__ == "__main__":
    unittest.main()
