"""Offline validation shared by packaging, flashing, recovery and regression tests."""
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Synthetic fixture identity only. Device-bound flash/backup tools are not distributed.
TARGET_MAC = "00:00:00:00:00:00"
FLASH_SIZE = 0x1000000
GEOMETRY = [(1, 2, 0x9000, 0x4000), (1, 0, 0xd000, 0x2000),
            (1, 1, 0xf000, 0x1000), (0, 16, 0x20000, 0x4f0000),
            (0, 17, 0x510000, 0x4f0000), (1, 130, 0xa00000, 0x400000),
            (1, 3, 0xe00000, 0x10000)]
IMAGES = {"bootloader.bin": (0, 0x8000), "partitions.bin": (0x8000, 0x1000),
          "ota-reset.bin": (0xd000, 0x2000), "firmware.bin": (0x20000, 0x4f0000),
          "littlefs.bin": (0xa00000, 0x400000)}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha256(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def within(directory, name):
    target = (Path(directory) / name).resolve()
    require(target.is_relative_to(Path(directory).resolve()), "Path escapes artifact directory")
    return target


def partition_geometry(data):
    result = []
    for offset in range(0, min(len(data), 0x1000), 32):
        entry = data[offset:offset + 32]
        if entry[:2] in (b"\xff\xff", b"\xeb\xeb"):
            break
        require(len(entry) == 32 and entry[:2] == b"\xaa\x50", "Invalid partition record")
        _, kind, subtype, start, size, _, flags = struct.unpack("<HBBII16sI", entry)
        require(flags == 0, "Encrypted partition is unsupported")
        result.append((kind, subtype, start, size))
    require(result == GEOMETRY, "Partition geometry differs from the original StackChan")
    return result


def source_hashes(root=ROOT):
    paths = sorted((root / "src").glob("*")) + [root / "platformio.ini", root / "partitions.csv"]
    paths += sorted((root / "scripts").glob("*.py")) + sorted((root / "scripts").glob("*.sh"))
    paths += sorted((root / "data").rglob("*"))
    return {str(p.relative_to(root)): sha256(p) for p in paths if p.is_file()}


def validate_release(directory=ROOT / "dist", root=ROOT):
    directory = Path(directory)
    require((directory / "manifest.json").is_file(), "Build and package the firmware first")
    manifest = json.loads((directory / "manifest.json").read_text())
    require(manifest.get("chip") == "esp32s3" and manifest.get("flash_size") == FLASH_SIZE,
            "Wrong release chip or capacity")
    require(manifest.get("source_sha256") == source_hashes(root), "Source changed; rebuild release")
    if (Path(root)/"src/FirmwareVersion.h").is_file():
        from version_source import validate_identity
        require(manifest.get("version_identity")==validate_identity(directory/"firmware.bin",manifest.get("version"),root),
                "Release identity proof differs from the compiled image")
    if manifest.get("bsp_quiet_boot"):
        # The current build must still use the exact reviewed dependency patch.
        # Archived split releases are self-contained and checked by image hashes.
        if Path(root).resolve()==ROOT.resolve():
            from prepare_safe_bsp import prepare
            require(manifest["bsp_quiet_boot"].get("files")==prepare(check=True),"Quiet BSP patch changed; rebuild")
    if manifest.get("local_audio_driver") and Path(root).resolve()==ROOT.resolve():
        from prepare_local_audio import prepare as prepare_audio
        require(manifest["local_audio_driver"].get("files")==prepare_audio(check=True),"Audio driver patch changed; rebuild")
    if (Path(root)/"scripts/prepare_usb_tx.py").is_file():
        from prepare_usb_tx import prepare as prepare_usb
        require(manifest.get("usb_tx_driver",{}).get("files")==prepare_usb(Path(root),check=True),
                "USB TX driver proof missing or changed; rebuild")
    entries = manifest.get("images", [])
    require(len(entries) == len(IMAGES) and {e["file"] for e in entries} == set(IMAGES),
            "Incomplete or unexpected image set")
    plan = []
    for entry in entries:
        path = within(directory, entry["file"])
        start, limit = IMAGES[entry["file"]]
        size = path.stat().st_size
        require(entry["offset"] == start and 0 < size <= limit, "Wrong offset or oversized image")
        require(entry["size"] == size and entry["sha256"] == sha256(path), "Image checksum mismatch")
        data = path.read_bytes()
        if path.name in ("firmware.bin", "bootloader.bin"):
            require(len(data) >= 24 and data[0] == 0xe9 and struct.unpack_from("<H", data, 12)[0] == 9,
                    "Image is not for ESP32-S3")
        if path.name == "ota-reset.bin":
            require(data == b"\xff" * 0x2000, "Unexpected OTA selector reset")
        if path.name == "littlefs.bin":
            require(size == 0x400000, "Wrong asset filesystem capacity")
        if path.name == "partitions.bin":
            partition_geometry(data)
        plan.append((start, path))
    # Only the allowlisted ranges above may be written: never NVS/PHY/app1/coredump.
    return sorted(plan)


def validate_backup(directory=ROOT / "backups"):
    directory = Path(directory)
    require((directory / "manifest.json").is_file(), "No verified full backup; hardware writing is blocked")
    manifest = json.loads((directory / "manifest.json").read_text())
    require(manifest.get("mac") == TARGET_MAC and manifest.get("chip") == "ESP32-S3",
            "Backup belongs to a different device")
    require(manifest.get("size") == FLASH_SIZE and manifest.get("double_read_match") is True,
            "Complete, matching double-read backup is required")
    images = [within(directory, manifest.get(key, "")) for key in ("first", "second")]
    require(images[0] != images[1], "Two distinct read results are required")
    for path in images:
        require(path.stat().st_size == FLASH_SIZE, "Backup is incomplete")
        require(sha256(path) == manifest.get("sha256"), "Backup checksum mismatch")
    with images[0].open("rb") as stream:
        stream.seek(0x8000)
        partition_geometry(stream.read(0x1000))
    return images[0]


def run_cli(main):
    try:
        main()
    except (ValueError, OSError) as error:
        print(f"BLOCKED: {error}", file=sys.stderr)
        raise SystemExit(2) from None
