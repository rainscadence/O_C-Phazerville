"""Negative tests for the actual PlatformIO final-ELF gate."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "parameter_memory", Path(__file__).resolve().parents[2] / "res/parameter_memory.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


def fixture(target="T41"):
    address = gate.FLASH[target][0] + 0x1000
    rows = [(address, 16, "r", f"Applet{i}::Parameters() const::entries")
            for i in range(8)]
    rows += [(address, 16, "R", "phz::parameters::CvSourceOptions")]
    rows += [(address, 8, "t", f"Applet{i}::Parameters() const")
             for i in range(8)]
    rows += [(address, 8, "T", "phz::parameters::" + name + ")")
             for name in gate.REQUIRED]
    rows += [(address, 8, "t", "phz::parameters::Field<App, int>::Read(void const*)")]
    return rows


def nm(rows):
    return "\n".join(f"{a:08x} {s:08x} {k} {n}" for a, s, k, n in rows)


class MemoryGateTest(unittest.TestCase):
    def test_both_active_slots(self):
        for target in gate.FLASH:
            self.assertEqual(gate.validate(nm(fixture(target)), target), (8, 144, 15, 120))

    def test_wrong_slot(self):
        with self.assertRaisesRegex(ValueError, "outside active flash slot"):
            gate.validate(nm(fixture("T41")), "T41_audio")

    def test_metadata_in_ram(self):
        rows = fixture()
        rows[0] = (0x20001000, 16, "r", rows[0][3])
        with self.assertRaisesRegex(ValueError, "outside active flash slot"):
            gate.validate(nm(rows), "T41")

    def test_writable_metadata(self):
        rows = fixture()
        rows[0] = (*rows[0][:2], "D", rows[0][3])
        with self.assertRaisesRegex(ValueError, "not read-only"):
            gate.validate(nm(rows), "T41")

    def test_template_code_in_itcm(self):
        rows = fixture()
        rows[-1] = (0x1000, *rows[-1][1:])
        with self.assertRaisesRegex(ValueError, "outside active flash slot"):
            gate.validate(nm(rows), "T41")

    def test_missing_table(self):
        with self.assertRaisesRegex(ValueError, "Missing parameter"):
            gate.validate(nm(fixture()[1:]), "T41")

    def test_missing_cv_options(self):
        with self.assertRaisesRegex(ValueError, "Missing parameter"):
            gate.validate(nm([s for s in fixture() if not s[3].endswith("CvSourceOptions")]), "T41")

    def test_missing_matching_getter(self):
        with self.assertRaisesRegex(ValueError, "Missing schema getter"):
            gate.validate(nm([s for s in fixture() if s[3] != "Applet0::Parameters() const"]), "T41")

    def test_missing_common_helper(self):
        with self.assertRaisesRegex(ValueError, "Missing control function"):
            gate.validate(nm([s for s in fixture() if "Owner::Find(" not in s[3]]), "T41")

    def test_duplicate(self):
        rows = fixture()
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            gate.validate(nm(rows + [rows[0]]), "T41")

    def test_zero_size(self):
        rows = fixture()
        rows[0] = (rows[0][0], 0, *rows[0][2:])
        with self.assertRaisesRegex(ValueError, "outside active flash slot"):
            gate.validate(nm(rows), "T41")

    def test_slot_boundary(self):
        rows = fixture()
        rows[0] = (gate.FLASH["T41"][1] - 16, *rows[0][1:])
        gate.validate(nm(rows), "T41")
        rows[0] = (rows[0][0] + 1, *rows[0][1:])
        with self.assertRaisesRegex(ValueError, "outside active flash slot"):
            gate.validate(nm(rows), "T41")

    def test_unknown_target_is_not_a_pass(self):
        with self.assertRaises(KeyError):
            gate.validate(nm(fixture()), "T40")

    def test_empty_or_malformed_symbols(self):
        with self.assertRaisesRegex(ValueError, "Missing parameter"):
            gate.validate("not symbol data\n00000000 T no_size", "T41")


if __name__ == "__main__":
    unittest.main()
