"""Narrow, hash-pinned M5Unified lifecycle fixes; no fetch/install/hardware access."""
from pathlib import Path
import argparse, hashlib, json
ROOT=Path(__file__).resolve().parents[1]
REL=Path('.pio/libdeps/stackchan-vibe-pet/M5Unified/src/utility')
PINS={
 'Mic_Class.cpp':'9f0b0403b889e0ee2848b75b6ff6fb1b242c72c8b1f0b1d7282e3332d05bac60',
 'Mic_Class.hpp':'4fe3af3f6256d7a9be2777e99375fddf987fd7986774943091a7f786856a66cd',
 'Speaker_Class.cpp':'e5f5ab2abdd569eefd0ae14b0e1f4ef114de148fb616479fa8037ff172b8c2a9'}
def digest(data):return hashlib.sha256(data).hexdigest()
def replace(text,old,new):
 if text.count(old)!=1:raise ValueError('Pinned audio context differs: '+old[:90])
 return text.replace(old,new)
def patched(name,raw):
 s=raw.decode()
 if name=='Mic_Class.hpp':
  s=replace(s,'#include "m5unified_common.h"','#include "m5unified_common.h"\n#include <atomic>')
  s=replace(s,'    bool begin(void);','    bool begin(void);\n    size_t pendingSamples() const { return _pending_samples.load(std::memory_order_acquire); }\n    bool captureFailed() const { return _capture_failed.load(); }')
  s=replace(s,'    recording_info_t _rec_info[2];','    recording_info_t _rec_info[2];\n    std::atomic<size_t> _pending_samples{0};\n    std::atomic<bool> _capture_failed{false};')
 elif name=='Mic_Class.cpp':
  s=replace(s,'      for (;;)\n      {','      const size_t completed_length = dst_remain;\n      for (; self->_task_running;)\n      {')
  s=replace(s,'          _i2s_read(self->_cfg.i2s_port, src_buf, dma_buf_len, &src_len, 100 / portTICK_PERIOD_MS);',
   '''          src_len = 0;
          const auto read_result = _i2s_read(self->_cfg.i2s_port, src_buf, dma_buf_len, &src_len, pdMS_TO_TICKS(20));
          // Never consume stale/OOB samples after a timeout or incomplete stereo pair.
          if (read_result != ESP_OK || src_len < 4 || src_len > dma_buf_len || src_len % 4) {
            self->_capture_failed.store(true);
            self->_task_running = false;
            break;
          }''')
  s=replace(s,'          current_rec->length = 0;\n          break;','          current_rec->length = 0;\n          self->_pending_samples.fetch_sub(completed_length, std::memory_order_release);\n          break;')
  s=replace(s,'    if (_task_semaphore == nullptr) { _task_semaphore = xSemaphoreCreateBinary(); }',
   '    if (_task_semaphore == nullptr) { _task_semaphore = xSemaphoreCreateBinary(); }\n    if (_task_semaphore == nullptr) return false;\n    _capture_failed.store(false);')
  s=replace(s,'    return res;\n  }\n\n  void Mic_Class::end',
   '''    res = res && (_task_handle != nullptr);
    if (!res) {
      _task_running = false;
      if (_cb_set_enabled) _cb_set_enabled(_cb_set_enabled_args, false);
      _i2s_driver_uninstall(_cfg.i2s_port);
    } else {
      _rec_sample_rate = _calc_rec_rate();
    }
    return res;
  }

  void Mic_Class::end''')
  s=replace(s,'    if (!_task_running) { return; }\n    _task_running = false;',
   '    // Owner calls end even after a failed DMA read or partial startup.\n    _task_running = false;')
  s=replace(s,'    _i2s_driver_uninstall(_cfg.i2s_port);\n  }\n\n  bool Mic_Class::_rec_raw',
   '''    _i2s_driver_uninstall(_cfg.i2s_port);
    _rec_info[0] = recording_info_t{};
    _rec_info[1] = recording_info_t{};
    _pending_samples.store(0, std::memory_order_release);
    _is_recording = false;
  }

  bool Mic_Class::_rec_raw''')
  s=replace(s,'    _rec_info[_rec_flip] = info;',
   '    _pending_samples.fetch_add(array_len, std::memory_order_release);\n    _rec_info[_rec_flip] = info;')
 elif name=='Speaker_Class.cpp':
  s=replace(s,'    if (!begin() || (_task_handle == nullptr)) { return true; }',
   '    if (!begin() || (_task_handle == nullptr)) { return false; }')
  s=replace(s,'    if (_task_semaphore == nullptr) { _task_semaphore = xSemaphoreCreateBinary(); }',
   '    if (_task_semaphore == nullptr) { _task_semaphore = xSemaphoreCreateBinary(); }\n    if (_task_semaphore == nullptr) return false;')
  s=replace(s,'    return res;\n  }\n\n  void Speaker_Class::end',
   '''    res = res && (_task_handle != nullptr);
    if (!res) {
      _task_running = false;
      if (_cb_set_enabled) _cb_set_enabled(_cb_set_enabled_args, false);
#if !defined (SDL_h_)
      _i2s_driver_uninstall(_cfg.i2s_port);
#endif
    }
    return res;
  }

  void Speaker_Class::end''')
 return s.encode()
def prepare(root=ROOT,check=False):
 archive=root/'vendor/m5unified-audio-original';plans=[];result=[]
 for name,expected in PINS.items():
  target=root/REL/name;saved=archive/name;current=target.read_bytes()
  original=saved.read_bytes() if saved.exists() else current
  if digest(original)!=expected:raise ValueError('Unexpected M5Unified original '+name)
  output=patched(name,original)
  if current not in (original,output):raise ValueError('Unreviewed local audio changes '+name)
  if check and current!=output:raise ValueError('Audio patch missing '+name)
  plans.append((target,saved,original,output))
  result.append({'file':str(REL/name),'original_sha256':expected,'sha256':digest(output)})
 if not check:
  archive.mkdir(parents=True,exist_ok=True)
  for target,saved,original,output in plans:
   if not saved.exists():saved.write_bytes(original)
   if target.read_bytes()!=output:target.write_bytes(output)
  (root/'evidence/local-audio-driver.json').write_text(json.dumps({'files':result},indent=2)+'\n')
 return result
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');a=p.parse_args()
 print(json.dumps(prepare(check=a.check),indent=2))
