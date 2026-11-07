"""Version regression: real compiled telemetry writer plus stale build/image failures."""
import json,os,subprocess,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'scripts'))
from version_source import NAME,MARKER,build_version,validate_wiring,validate_identity

class VersionIdentityTest(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
  (self.root/'src').mkdir();(self.root/'src/main.cpp').write_text('void telemetry(){firmware_version::stampTelemetry(d);}')
  self.version=build_version(ROOT);self.config()
  self.image=self.root/'firmware.bin';self.image.write_bytes(MARKER+self.version.encode()+b'\0v'+self.version.encode()+b'\0')
 def tearDown(self):self.temp.cleanup()
 def config(self,version=None,extra=''):
  version=version or self.version
  (self.root/'platformio.ini').write_text('[env:stackchan-vibe-pet]\nbuild_flags =\n  -D '+NAME+'=\\"'+version+'\\"\n'+extra)
 def test_compiled_production_packet_and_hid_alias(self):
  source=self.root/'version.cpp';binary=self.root/'version-test'
  source.write_text('''#include "FirmwareVersion.h"
#include "HidProtocol.h"
#include "config.h"
#include <ArduinoJson.h>
#include <string>
#include <cstdio>
int main(){JsonDocument d;firmware_version::stampTelemetry(d);
 d["hid"]=hid_protocol::kFirmwareVersion;d["compat"]=STACKCHAN_FIRMWARE_VERSION;
 d["descriptor_bytes"]=sizeof(hid_protocol::kReportMap);
 std::string json;serializeJson(d,json);std::puts(json.c_str());}
''')
  subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',f'-D{NAME}="{self.version}"',
   '-I'+str(ROOT/'src'),'-I'+str(ROOT/'.pio/libdeps/stackchan-vibe-pet/ArduinoJson/src'),str(source),'-o',str(binary)],check=True,capture_output=True,text=True)
  packet=json.loads(subprocess.check_output([str(binary)],text=True))
  self.assertEqual(packet['build'],self.version);self.assertEqual(packet['version'],self.version)
  self.assertEqual(packet['hid'],'v'+self.version);self.assertEqual(packet['compat'],self.version)
  validate_wiring(ROOT);identity=validate_identity(binary,self.version,ROOT)
  self.assertEqual(identity['image_record'],self.version)
  evidence=Path(os.environ.get('VIBE_TEST_EVIDENCE_DIR',str(ROOT/'.test-bin/version-evidence')));evidence.mkdir(parents=True,exist_ok=True)
  (evidence/'compiled-packet.json').write_text(json.dumps({'packet':packet,'identity':identity,'scope':'host compiled actual shared writer/HID header; no device'},indent=2)+'\n')
 def test_valid_matching_identity(self):
  self.assertEqual(validate_identity(self.image,self.version,self.root)['compiler'],self.version)
 def test_manifest_mismatch_rejected(self):
  with self.assertRaisesRegex(ValueError,'Manifest'):validate_identity(self.image,'0.1.5-repair2',self.root)
 def test_stale_compiled_image_rejected(self):
  self.image.write_bytes(MARKER+b'0.1.6-audio1\0v0.1.6-audio1\0')
  with self.assertRaisesRegex(ValueError,'Compiled firmware'):validate_identity(self.image,self.version,self.root)
 def test_missing_or_duplicate_record_rejected(self):
  good=self.image.read_bytes()
  for data in (b'no version record',good+good):
   self.image.write_bytes(data)
   with self.assertRaisesRegex(ValueError,'record'):validate_identity(self.image,self.version,self.root)
 def test_wrong_hid_rejected(self):
  self.image.write_bytes(MARKER+self.version.encode()+b'\0v0.1.5-repair2\0')
  with self.assertRaisesRegex(ValueError,'HID'):validate_identity(self.image,self.version,self.root)
 def test_original_failure_and_later_override_rejected(self):
  for source in ('d["version"]="0.1.5";d["build"]="0.1.5-repair2";',
   'firmware_version::stampTelemetry(d);d["build"]="0.1.5-repair2";'):
   (self.root/'src/main.cpp').write_text(source)
   with self.assertRaisesRegex(ValueError,'Telemetry'):validate_identity(self.image,self.version,self.root)
 def test_duplicate_or_missing_compile_definition_rejected(self):
  self.config(extra='  -D '+NAME+'=\\"'+self.version+'\\"\n')
  with self.assertRaisesRegex(ValueError,'Exactly one'):build_version(self.root)
  (self.root/'platformio.ini').write_text('[env:stackchan-vibe-pet]\nbuild_flags=-D OTHER=1\n')
  with self.assertRaisesRegex(ValueError,'Exactly one'):build_version(self.root)
 def test_dev_compile_rejected(self):
  self.config('dev')
  with self.assertRaisesRegex(ValueError,'release literal'):build_version(self.root)

if __name__=='__main__':unittest.main(verbosity=2)
