"""Patch provenance/rollback and fail-closed preparation; temporary files only."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from prepare_usb_tx import ARCHIVE, REL, original, patched, prepare


class UsbTxPatchTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.path = self.root / REL
        self.path.parent.mkdir(parents=True)
        (self.root / 'evidence').mkdir()
        self.raw = original()
        self.path.write_bytes(self.raw)

    def tearDown(self):
        self.temp.cleanup()

    def test_missing_patch_cannot_validate(self):
        with self.assertRaisesRegex(ValueError, 'patch missing'):
            prepare(self.root, check=True)
        self.assertEqual(self.path.read_bytes(), self.raw)

    def test_idempotent_and_original_preserved(self):
        proof = prepare(self.root)
        self.assertEqual(prepare(self.root), proof)
        self.assertEqual(prepare(self.root, check=True), proof)
        self.assertEqual((self.root / ARCHIVE).read_bytes(), self.raw)
        self.assertEqual(self.path.read_bytes(), patched(self.raw))

    def test_foreign_original_rejected_without_writes(self):
        self.path.write_bytes(b'unknown source')
        with self.assertRaises(ValueError):
            prepare(self.root)
        self.assertEqual(self.path.read_bytes(), b'unknown source')
        self.assertFalse((self.root / ARCHIVE).exists())

    def test_user_changes_never_overwritten(self):
        prepare(self.root)
        changed = self.path.read_bytes() + b'\n// user change\n'
        self.path.write_bytes(changed)
        for check in (True, False):
            with self.assertRaisesRegex(ValueError, 'will not overwrite'):
                prepare(self.root, check=check)
        self.assertEqual(self.path.read_bytes(), changed)


if __name__ == '__main__':
    unittest.main()
