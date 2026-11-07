"""Release identity from the actual compiler flag; reject stale runtime/image labels."""
import configparser,re,shlex
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
NAME='VIBESTACKCHAN_FIRMWARE_VERSION'
RELEASE=r'[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9]+(?:[.-][A-Za-z0-9]+)*)?'
MARKER=b'STACKCHAN_BUILD='

def build_version(root=ROOT):
 config=configparser.ConfigParser(interpolation=None);config.read(Path(root)/'platformio.ini')
 flags=shlex.split(config.get('env:stackchan-vibe-pet','build_flags'))
 definitions=[]
 for i,token in enumerate(flags):
  value=flags[i+1] if token=='-D' and i+1<len(flags) else token[2:] if token.startswith('-D') else ''
  if value.startswith(NAME+'='):definitions.append(value)
 if len(definitions)!=1:raise ValueError('Exactly one firmware compiler version required')
 match=re.fullmatch(NAME+r'="('+RELEASE+r')"',definitions[0])
 if not match:raise ValueError('Firmware compiler version must be a quoted release literal')
 return match.group(1)

def validate_wiring(root=ROOT):
 source=(Path(root)/'src/main.cpp').read_text()
 if source.count('firmware_version::stampTelemetry(d);')!=1 or re.search(r'd\s*\[\s*"(?:build|version)"\s*\]\s*=',source):
  raise ValueError('Telemetry must use the shared version writer, with no local build/version overrides')

def validate_identity(image,version,root=ROOT):
 expected=build_version(root)
 if version!=expected:raise ValueError('Manifest and compiler version differ')
 validate_wiring(root)
 data=Path(image).read_bytes()
 records=re.findall(re.escape(MARKER)+rb'([^\x00]{1,64})\x00',data)
 if records!=[expected.encode()]:raise ValueError('Compiled firmware version record missing, duplicated or stale')
 if b'v'+expected.encode()+b'\x00' not in data:raise ValueError('Compiled HID version differs or is absent')
 return {'compiler':expected,'telemetry':expected,'hid':'v'+expected,'image_record':records[0].decode()}
