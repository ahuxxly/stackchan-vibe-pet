"""Validate the actual JPEG headers, required frames and manifest without executing upstream tools."""
import json
import struct
import zlib
from release_guard import ROOT, require, sha256


def jpeg_size(data):
    require(data[:2] == b"\xff\xd8", "Not JPEG")
    pos = 2
    while pos < len(data):
        require(data[pos] == 255, "Invalid JPEG marker")
        while data[pos] == 255:
            pos += 1
        marker = data[pos]
        pos += 1
        length = struct.unpack_from(">H", data, pos)[0]
        if marker in (0xc0, 0xc1, 0xc2):
            height, width = struct.unpack_from(">HH", data, pos + 3)
            return width, height
        require(length >= 2 and pos + length <= len(data), "Invalid JPEG segment")
        pos += length
    raise ValueError("Missing JPEG dimensions")


def validate_lumi(root=ROOT):
    directory=root/"data/lumi"
    manifest=json.loads((directory/"manifest.json").read_text())
    require(manifest.get("schema")==1 and manifest.get("complete") is True and
            manifest.get("frame_size")==[168,168] and manifest.get("fps")==8,
            "Wrong Lumi resource profile")
    expected_keys=["idle","blink","look","listen","think","focus","wait","celebrate","blocked",
                   "disconnected","cached","sleep","pet","dizzy","dance","stretch","coffee","welcome"]
    require([s.get("key") for s in manifest["states"]]==expected_keys,"Incomplete or reordered Lumi states")
    expected=set();total=0
    for index,state in enumerate(manifest["states"]):
        require(state["id"]==index and len(state["frames"])==8,"Wrong Lumi state/frame IDs")
        for frame,item in enumerate(state["frames"]):
            name=f"lumi/{state['key']}_{frame}.jpg"
            require(item["file"]==name,"Unexpected Lumi filename")
            path=root/"data"/name;raw=path.read_bytes();expected.add(path.name)
            require(len(raw)==item["bytes"] and sha256(path)==item["sha256"] and
                    zlib.crc32(raw)==item["crc32"],f"Lumi resource checksum mismatch: {name}")
            require(jpeg_size(raw)==(168,168),f"Wrong Lumi dimensions: {name}")
            require(len(raw)<=8192,f"Lumi frame exceeds the per-frame limit: {name}")
            total+=len(raw)
    require({p.name for p in directory.glob("*.jpg")}==expected,"Missing or extra Lumi frames")
    require(total==manifest["total_bytes"] and total<2400000,"Lumi capacity mismatch")
    require(sum(p.stat().st_size for p in (root/"data").rglob("*") if p.is_file())<3400000,
            "Combined resources leave insufficient LittleFS headroom")
    header=(root/"src/MuseAssets.h").read_text()
    for state in manifest["states"]:
        row="{"+",".join("{%u,0x%08xU}"%(f["bytes"],f["crc32"]) for f in state["frames"])+"}"
        require(row in header,"Compiled resource checksums disagree with manifest")
    return {"status":"PASS","states":18,"frames":144,"dimensions":[168,168],"total_bytes":total,
            "decoded_cache_bytes":8*168*168*2,"retained_tile_bytes":168*168*2,"encoded_ram_bytes":total,"internal_decode_bytes":168*168*2,
            "source":"native ImageGen, pixel-reviewed selected sheets"}


def main():
    directory = ROOT / "data"
    manifest = json.loads((directory / "face_assets.json").read_text())
    require(manifest["schemaVersion"] == 2 and manifest["target"] == "cores3" and
            manifest["renderer"] == "animated" and manifest["canvas"] == {"width": 240, "height": 240},
            "Wrong face asset profile")
    expected = {f"base_m{m}_e{e}.jpg" for m in range(4) for e in range(4)}
    expected |= {f"pet_anim_{i}.jpg" for i in range(16)}
    expected |= {f"dir{i}.jpg" for i in range(17)}
    expected |= {f"dizzy_{i:02d}.jpg" for i in range(1, 16)} | {"blink16.jpg"}
    require({p.name for p in directory.glob("*.jpg")} == expected and manifest["assetCount"] == 65,
            "Missing or extra face frames")
    files = []
    for name in sorted(expected):
        path = directory / name
        require(jpeg_size(path.read_bytes()) == (240, 240), f"Wrong dimensions: {name}")
        files.append({"file": name, "bytes": path.stat().st_size, "sha256": sha256(path)})
    result = {"status": "PASS", "frames": 65, "dimensions": [240, 240],
              "total_bytes": sum(item["bytes"] for item in files), "files": files}
    (ROOT / "evidence/assets.json").write_text(json.dumps(result, indent=2) + "\n")
    print("PASS: 65 required CoreS3 v2 JPEG frames, all 240 x 240")
    lumi=validate_lumi()
    (ROOT/"design/muse/resource-validation.json").write_text(json.dumps(lumi,indent=2)+"\n")
    print(f"PASS: {lumi['frames']} Lumi JPEGs, SHA-256/CRC32, 168 x 168, {lumi['total_bytes']} bytes; original skin retained")


if __name__ == "__main__":
    main()
