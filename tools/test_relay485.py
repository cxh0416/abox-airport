import importlib.util
import pathlib
import unittest


MODULE_PATH = pathlib.Path(__file__).with_name("relay485.py")
SPEC = importlib.util.spec_from_file_location("relay485", MODULE_PATH)
relay485 = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(relay485)


class Relay485CliTests(unittest.TestCase):
    def test_cycle_bounds(self):
        self.assertEqual(relay485.MIN_CYCLE_MS, 100)
        self.assertEqual(relay485.MAX_CYCLE_MS, 600_000)

    def test_error_line_detection(self):
        self.assertTrue(relay485.is_error("ERR BAD_ARG"))
        self.assertTrue(relay485.is_error("err busy"))
        self.assertFalse(relay485.is_error("OK STATUS RELAY_CMD=OFF"))

    def test_normalize_line(self):
        self.assertEqual(relay485.normalize_line(b"\xff STATUS\r\n"), "� STATUS")


if __name__ == "__main__":
    unittest.main()
