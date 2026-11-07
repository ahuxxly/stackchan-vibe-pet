import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"scripts"))
from validate_assets import validate_lumi


class LumiResources(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);(self.root/"data").mkdir();(self.root/"src").mkdir()
        shutil.copytree(ROOT/"data/lumi",self.root/"data/lumi")
        shutil.copy2(ROOT/"src/MuseAssets.h",self.root/"src/MuseAssets.h")
    def test_complete_native_frame_set(self):
        result=validate_lumi(self.root)
        self.assertEqual(result["frames"],144)
        self.assertEqual(result["decoded_cache_bytes"],451584)
        self.assertEqual(result["retained_tile_bytes"],56448)
        self.assertEqual(result["encoded_ram_bytes"],599184)
    def test_corrupt_frame_cannot_ship(self):
        path=self.root/"data/lumi/focus_3.jpg";data=bytearray(path.read_bytes());data[-5]^=1;path.write_bytes(data)
        with self.assertRaisesRegex(ValueError,"checksum"):validate_lumi(self.root)
    def test_missing_frame_cannot_ship(self):
        (self.root/"data/lumi/cached_4.jpg").unlink()
        with self.assertRaises(FileNotFoundError):validate_lumi(self.root)
    def test_state_order_is_protocol_contract(self):
        path=self.root/"data/lumi/manifest.json";d=json.loads(path.read_text());d["states"].reverse();path.write_text(json.dumps(d))
        with self.assertRaisesRegex(ValueError,"reordered"):validate_lumi(self.root)
    def test_stale_compiled_checksums_cannot_ship(self):
        (self.root/"src/MuseAssets.h").write_text("// stale header")
        with self.assertRaisesRegex(ValueError,"Compiled"):validate_lumi(self.root)
    def test_extra_resource_is_detected(self):
        shutil.copy2(self.root/"data/lumi/idle_0.jpg",self.root/"data/lumi/extra.jpg")
        with self.assertRaisesRegex(ValueError,"extra"):validate_lumi(self.root)


if __name__=="__main__":unittest.main()
