"""Fault-injection tests for flash protection. Uses generated fixtures, never USB."""
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from release_guard import (FLASH_SIZE, GEOMETRY, IMAGES, TARGET_MAC, sha256,
                           source_hashes, validate_backup, validate_release)


class ReleaseGuardTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.dist = self.root / "dist"
        self.dist.mkdir()
        (self.root / "src").mkdir()
        (self.root / "src/main.cpp").write_text("fixture source")
        (self.root / "platformio.ini").write_text("fixture config")
        (self.root / "partitions.csv").write_text("fixture geometry")
        table = b"".join(struct.pack("<HBBII16sI", 0x50aa, *g, b"test", 0) for g in GEOMETRY)
        self.table = table + b"\xff" * (0x1000 - len(table))
        for name in IMAGES:
            if name == "partitions.bin":
                data = self.table
            elif name == "ota-reset.bin":
                data = b"\xff" * 0x2000
            elif name == "littlefs.bin":
                data = b"\xff" * 0x400000
            else:
                data = bytearray(64)
                data[0] = 0xe9
                struct.pack_into("<H", data, 12, 9)
            (self.dist / name).write_bytes(data)
        self.manifest = {"chip": "esp32s3", "flash_size": FLASH_SIZE,
                         "source_sha256": source_hashes(self.root), "images": [
                             {"file": name, "offset": value[0],
                              "size": (self.dist / name).stat().st_size,
                              "sha256": sha256(self.dist / name)} for name, value in IMAGES.items()]}
        self.save()

    def tearDown(self):
        self.temp.cleanup()

    def save(self):
        (self.dist / "manifest.json").write_text(json.dumps(self.manifest))

    def test_valid_release_and_nvs_exclusion(self):
        plan = validate_release(self.dist, self.root)
        self.assertEqual([offset for offset, _ in plan], [0, 0x8000, 0xd000, 0x20000, 0xa00000])
        for offset, path in plan:
            end = offset + path.stat().st_size
            self.assertFalse(offset < 0xd000 and end > 0x9000)
            self.assertFalse(offset < 0x10000 and end > 0xf000)

    def test_generic_app_offset_rejected(self):
        self.manifest["images"][3]["offset"] = 0x10000
        self.save()
        with self.assertRaisesRegex(ValueError, "offset"):
            validate_release(self.dist, self.root)

    def test_corrupt_image_rejected(self):
        (self.dist / "firmware.bin").write_bytes(b"corrupt")
        with self.assertRaises(ValueError):
            validate_release(self.dist, self.root)

    def test_stale_release_rejected(self):
        (self.root / "src/main.cpp").write_text("new unbuilt source")
        with self.assertRaisesRegex(ValueError, "rebuild"):
            validate_release(self.dist, self.root)

    def test_wrong_chip_even_with_matching_hash_rejected(self):
        path = self.dist / "firmware.bin"
        data = bytearray(path.read_bytes())
        struct.pack_into("<H", data, 12, 5)  # ESP32-C3 is not this StackChan.
        path.write_bytes(data)
        self.manifest["images"][3]["sha256"] = sha256(path)
        self.save()
        with self.assertRaisesRegex(ValueError, "ESP32-S3"):
            validate_release(self.dist, self.root)

    def test_partition_that_overlaps_calibration_rejected(self):
        path = self.dist / "partitions.bin"
        data = bytearray(path.read_bytes())
        struct.pack_into("<I", data, 8, 0x5000)  # Extend original NVS into OTA data.
        path.write_bytes(data)
        self.manifest["images"][1]["sha256"] = sha256(path)
        self.save()
        with self.assertRaisesRegex(ValueError, "geometry"):
            validate_release(self.dist, self.root)

    def make_backup(self):
        directory = self.root / "backups"
        directory.mkdir()
        for name in ("a.bin", "b.bin"):
            with (directory / name).open("wb") as stream:
                stream.truncate(FLASH_SIZE)
                stream.seek(0x8000)
                stream.write(self.table)
        manifest = {"mac": TARGET_MAC, "chip": "ESP32-S3", "size": FLASH_SIZE,
                    "double_read_match": True, "first": "a.bin", "second": "b.bin",
                    "sha256": sha256(directory / "a.bin")}
        (directory / "manifest.json").write_text(json.dumps(manifest))
        return directory, manifest

    def test_double_backup_requires_both_actual_images(self):
        directory, _ = self.make_backup()
        self.assertEqual(validate_backup(directory), (directory / "a.bin").resolve())
        with (directory / "b.bin").open("r+b") as stream:
            stream.seek(0xf00000)
            stream.write(b"x")
        with self.assertRaisesRegex(ValueError, "checksum"):
            validate_backup(directory)

    def test_partial_backup_rejected(self):
        directory, _ = self.make_backup()
        with (directory / "b.bin").open("r+b") as stream:
            stream.truncate(0x10000)
        with self.assertRaisesRegex(ValueError, "incomplete"):
            validate_backup(directory)

    def test_foreign_device_backup_rejected(self):
        directory, manifest = self.make_backup()
        manifest["mac"] = "00:11:22:33:44:55"
        (directory / "manifest.json").write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "different device"):
            validate_backup(directory)

    def test_manifest_cannot_escape_private_backup_directory(self):
        directory, manifest = self.make_backup()
        manifest["second"] = "../outside.bin"
        (directory / "manifest.json").write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError, "escapes"):
            validate_backup(directory)


if __name__ == "__main__":
    unittest.main(verbosity=2)
