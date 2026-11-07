"""Check tracked public source for private-file boundaries and identity patterns."""
import pathlib
import re
import subprocess
ROOT = pathlib.Path(__file__).resolve().parents[1]
paths = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode().split("\0")
for name in filter(None, paths):
    p = pathlib.PurePosixPath(name)
    assert not set(p.parts) & {"backups", "evidence", "design", ".pio", ".venv", ".toolchain", ".test-bin", "downloads", "dist", "__pycache__"}, name
    assert p.suffix not in {".bin", ".elf", ".pem", ".key", ".pyc"}, name
    assert p.name != "config_private.h" and not p.name.startswith(".env"), name
    raw = (ROOT / name).read_bytes()
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError:
        assert p.suffix in {".jpg", ".png"}, name
        continue
    assert not re.search(r"/(?:Users|home)/[^ /\n]+", text), name
    for mac in re.findall(r"(?i)\b(?:[0-9a-f]{2}:){5}[0-9a-f]{2}\b", text):
        assert mac in {"00:00:00:00:00:00", "ff:ff:ff:ff:ff:ff", "00:11:22:33:44:55"}, name
    assert not re.search(r"(?:gh[pousr]_|github_pat_|sk-proj-)[A-Za-z0-9_]{20,}", text), name
print(f"PASS: {len(list(filter(None, paths)))} tracked files, public boundaries and identity patterns")
