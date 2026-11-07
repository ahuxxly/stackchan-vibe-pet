"""Build the original-format LittleFS packer natively; no Rosetta or system install."""
import hashlib
import io
import json
import pathlib
import platform
import subprocess
import tarfile
import tempfile
from release_guard import ROOT, require, sha256

SOURCES = [
    ("earlephilhower/mklittlefs", "943d2f76736eef6630edd4933e42c3016194770b",
     "49cefbcd829c90840aed2fd2c080d745f9ae6a83f773d2d61def05f689980d2f", ""),
    ("littlefs-project/littlefs", "abd90cb84c818a663b584575b019258d01d0065e",
     "2ecc25ee466e15ec642256faad6d90d1d73367d3afe7bdc3fcb52b05d1e9f6f7", "littlefs"),
]


def prepare_sources():
    source = ROOT / "vendor/mklittlefs"
    for repo, commit, checksum, subdir in SOURCES:
        label = repo.split("/")[-1]
        archive = ROOT / "downloads" / f"{label}-source.tar.gz"
        if not archive.is_file():
            import requests
            result = requests.get(f"https://codeload.github.com/{repo}/tar.gz/{commit}", timeout=(20, 60))
            result.raise_for_status()
            archive.parent.mkdir(exist_ok=True)
            archive.write_bytes(result.content)
        require(sha256(archive) == checksum, "Official tool source archive checksum mismatch")
        dest = source / subdir
        # Re-extract the pinned archive, excluding all executable hooks and symlinks.
        with tarfile.open(archive, "r:gz") as bundle:
            for item in bundle.getmembers():
                parts = pathlib.PurePosixPath(item.name).parts[1:]
                if not parts:
                    continue
                require(".." not in parts and not item.issym() and not item.islnk(), "Unsafe archive path")
                target = dest.joinpath(*parts)
                if item.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    require(item.isfile(), "Unsupported archive entry")
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(bundle.extractfile(item).read())
    return source


def main():
    source = prepare_sources()
    output = ROOT / ".native-tools"
    output.mkdir(exist_ok=True)
    binary = output / "mklittlefs"
    fingerprint = hashlib.sha256(json.dumps(SOURCES).encode()).hexdigest() + platform.machine()
    stamp = output / "mklittlefs.json"
    cached = json.loads(stamp.read_text()) if stamp.exists() else {}
    if not binary.exists() or cached.get("fingerprint") != fingerprint or cached.get("sha256") != sha256(binary):
        include = ["-I" + str(source / p) for p in ("tclap", "include", "littlefs", ".")]
        defines = ['-DVERSION="0.2.3-35-g943d2f7-native"', "-DLFS_NAME_MAX=32", "-D__NO_INLINE__"]
        for name in ("lfs", "lfs_util"):
            subprocess.run(["clang", "-std=gnu99", "-Os", *defines, *include,
                            "-c", str(source / "littlefs" / f"{name}.c"), "-o", str(output / f"{name}.o")], check=True)
        subprocess.run(["clang++", "-std=gnu++11", "-Os", *defines, *include, str(source / "main.cpp"),
                        str(output / "lfs.o"), str(output / "lfs_util.o"), "-o", str(binary)], check=True)
        stamp.write_text(json.dumps({"fingerprint": fingerprint, "sha256": sha256(binary),
                                     "arch": platform.machine(), "disk_format": "2.0"}, indent=2) + "\n")
    image = ROOT / ".pio/build/stackchan-vibe-pet/littlefs.bin"
    image.parent.mkdir(parents=True, exist_ok=True)
    common = [str(binary), "-b", "4096", "-p", "256", "-s", str(0x400000)]
    subprocess.run([*common, "-c", str(ROOT / "data"), str(image)], check=True)
    # Mount/unpack the generated image and compare every source asset byte-for-byte.
    with tempfile.TemporaryDirectory(prefix="stackchan-littlefs-") as temp:
        subprocess.run([*common, "-u", temp, str(image)], check=True)
        expected = {str(p.relative_to(ROOT / "data")): sha256(p) for p in (ROOT / "data").rglob("*") if p.is_file()}
        actual = {str(p.relative_to(temp)): sha256(p) for p in pathlib.Path(temp).rglob("*") if p.is_file()}
        require(actual == expected, "LittleFS round-trip changed or omitted assets")
    result = {"status": "PASS", "arch": platform.machine(), "disk_format": "2.0",
              "packer_sha256": sha256(binary), "image_sha256": sha256(image),
              "image_bytes": image.stat().st_size, "round_trip_files": len(expected),
              "sources": [{"repo": r, "commit": c, "archive_sha256": h} for r, c, h, _ in SOURCES]}
    (ROOT / "evidence/littlefs.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"PASS: native {platform.machine()} LittleFS 2.0, {len(expected)} files round-trip SHA-256 match")


if __name__ == "__main__":
    main()
