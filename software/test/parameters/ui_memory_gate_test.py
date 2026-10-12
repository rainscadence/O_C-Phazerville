"""Standalone negative tests for the actual applet UI memory gate."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "parameter_memory", Path(__file__).resolve().parents[2] / "res/parameter_memory.py")
assert spec is not None and spec.loader is not None
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)

ITCM = (0x20, 0x80000)


def fixture(target="T41"):
    address = gate.FLASH[target][0] + 0x1000
    rows = [(address, 16, "t", name) for name in gate.UI_FUNCTIONS]
    rows += [(0x1000, 16, "t", name) for name in gate.HOT_FUNCTIONS]
    return rows


def nm(rows):
    return "\n".join(f"{a:08x} {s:08x} {k} {n}" for a, s, k, n in rows)


def expected(target="T41"):
    return (len(gate.UI_FUNCTIONS), 16 * len(gate.UI_FUNCTIONS), len(gate.HOT_FUNCTIONS))


class UiMemoryGateTest(unittest.TestCase):
    def test_both_active_slots(self):
        for target in gate.FLASH:
            self.assertEqual(gate.validate_ui(nm(fixture(target)), target), expected(target))

    def test_ui_in_itcm(self):
        rows = fixture()
        rows[0] = (0x1000, *rows[0][1:])
        with self.assertRaisesRegex(ValueError, "outside required code region"):
            gate.validate_ui(nm(rows), "T41")

    def test_wrong_slot(self):
        with self.assertRaisesRegex(ValueError, "outside required code region"):
            gate.validate_ui(nm(fixture("T41")), "T41_audio")

    def test_crossing_slot_end(self):
        rows = fixture()
        # A 16-byte body fits exactly at end-16, not end-8.
        rows[0] = (gate.FLASH["T41"][1] - rows[0][1], *rows[0][1:])
        gate.validate_ui(nm(rows), "T41")
        rows[0] = (rows[0][0] + 1, *rows[0][1:])
        with self.assertRaisesRegex(ValueError, "outside required code region"):
            gate.validate_ui(nm(rows), "T41")

    def test_hot_controller_in_flash(self):
        rows = fixture()
        index = next(i for i, row in enumerate(rows) if row[3].endswith("::Controller()"))
        rows[index] = (gate.FLASH["T41"][0] + 0x1000, *rows[index][1:])
        with self.assertRaisesRegex(ValueError, "outside required code region"):
            gate.validate_ui(nm(rows), "T41")

    def test_missing_name(self):
        with self.assertRaisesRegex(ValueError, "Missing UI/hot function"):
            gate.validate_ui(nm(fixture()[1:]), "T41")

    def test_duplicate(self):
        rows = fixture()
        with self.assertRaisesRegex(ValueError, "Duplicate UI/hot symbol"):
            gate.validate_ui(nm(rows + [rows[0]]), "T41")

    def test_zero_size(self):
        rows = fixture()
        rows[0] = (rows[0][0], 0, *rows[0][2:])
        with self.assertRaisesRegex(ValueError, "outside required code region"):
            gate.validate_ui(nm(rows), "T41")

    def test_non_code_kind(self):
        rows = fixture()
        rows[0] = (*rows[0][:2], "r", rows[0][3])
        with self.assertRaisesRegex(ValueError, "outside required code region"):
            gate.validate_ui(nm(rows), "T41")

    def test_bad_lto_clone(self):
        rows = fixture()
        # An extra clone in the *wrong region*, while the original remains valid.
        clone = (0x1000, 16, "t", rows[0][3] + " [clone .constprop.0]")
        with self.assertRaisesRegex(ValueError, "outside required code region"):
            gate.validate_ui(nm(fixture() + [clone]), "T41")

    def test_valid_lto_clone_is_counted(self):
        rows = fixture()
        clone = (rows[0][0] + 0x100, 16, "t", rows[0][3] + " [clone .constprop.0]")
        self.assertEqual(gate.validate_ui(nm(rows + [clone]), "T41"), (34, 544, 14))

    def test_hot_bounds_and_interrupt_placement(self):
        for name in gate.HOT_FUNCTIONS:
            rows = fixture()
            index = next(i for i, row in enumerate(rows) if row[3] == name)
            for address in (ITCM[0] - 1, ITCM[1] - 15, gate.FLASH["T41"][0]):
                rows[index] = (address, 16, "t", name)
                with self.assertRaisesRegex(ValueError, "outside required code region"):
                    gate.validate_ui(nm(rows), "T41")

    def test_missing_hot_and_unknown_target(self):
        with self.assertRaisesRegex(ValueError, "Missing UI/hot function"):
            gate.validate_ui(nm(fixture()[:-1]), "T41")
        with self.assertRaises(KeyError):
            gate.validate_ui(nm(fixture()), "T40")

    def test_malformed_or_empty_input(self):
        with self.assertRaisesRegex(ValueError, "Missing UI/hot function"):
            gate.validate_ui("not symbol data\n00000000 T no_size", "T41")
        with self.assertRaisesRegex(ValueError, "Missing UI/hot function"):
            gate.validate_ui("", "T41")


if __name__ == "__main__":
    unittest.main()
