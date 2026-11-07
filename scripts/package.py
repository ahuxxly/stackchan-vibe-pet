"""Package split images; deliberately never produce an unsafe generic factory image."""
import json
import shutil
from datetime import datetime, timezone
from release_guard import ROOT, FLASH_SIZE, IMAGES, sha256, source_hashes, validate_release
from version_source import build_version,validate_identity

build = ROOT / ".pio/build/stackchan-vibe-pet"
dest = ROOT / "dist"
dest.mkdir(exist_ok=True)
for name in IMAGES:
    if name == "ota-reset.bin":
        (dest / name).write_bytes(b"\xff" * 0x2000)
    else:
        shutil.copy2(build / name, dest / name)
packages = {}
for base in [ROOT / ".toolchain/packages", ROOT / ".pio/libdeps/stackchan-vibe-pet"]:
    if base.exists():
        for path in sorted(base.iterdir()):
            for metadata in ("package.json", "library.json", ".piopm"):
                candidate = path / metadata
                if candidate.is_file():
                    info = json.loads(candidate.read_text())
                    packages[path.name] = info.get("version", "unspecified")
                    break
version=build_version()
identity=validate_identity(dest/"firmware.bin",version)
manifest = {
    "format": 1, "version": version, "version_identity": identity, "chip": "esp32s3", "flash_size": FLASH_SIZE,
    "built_at": datetime.now(timezone.utc).isoformat(), "source_sha256": source_hashes(),
    "packages": packages,
    "bsp_quiet_boot": json.loads((ROOT / "evidence/bsp-quiet-boot.json").read_text()),
    "local_audio_driver": json.loads((ROOT / "evidence/local-audio-driver.json").read_text()),
    "usb_tx_driver": json.loads((ROOT / "evidence/usb-tx-driver.json").read_text()),
    "filesystem_tool": json.loads((ROOT / "evidence/littlefs.json").read_text()), "images": [
        {"file": name, "offset": values[0], "size": (dest / name).stat().st_size,
         "sha256": sha256(dest / name)} for name, values in IMAGES.items()],
}
(dest / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
validate_release()
print("PASS: ESP32-S3 split images, original partition geometry, capacities and SHA-256")
