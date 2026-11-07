"""Apply a narrow, hash-guarded quiet-boot patch to the pinned official BSP.

No dependency fetch or installation. Original files are retained verbatim.
Motion initialization is deferred until the existing explicit power warm-up.
"""
import argparse
import hashlib
import json
from pathlib import Path
from motion_diagnostics_patch import board as diagnostic_board, transport as diagnostic_transport
from power_bias_patch import patch as bias_patch

ROOT=Path(__file__).resolve().parents[1]
REL=Path(".pio/libdeps/stackchan-vibe-pet/StackChan-BSP/src")
PINS={"M5StackChan.cpp":"5c0fa80a98396bb24614bcdb4e6f9991e6515625d82392a18e8c73aad27c8c62",
      "M5StackChan.h":"24f0d5e2e96709bad4e35b18790399674430db0768d2d45f194761bcd11d2e1e",
      "utils/motion/servo.h":"35cb7ee88ed74500711d5ade185d70878882a5a86568ad8f8fdd4121b829660b",
      "utils/motion/servo.cpp":"8db05dabd2b18e58fabe956982f5c78a00a14c8fd9e65aa8beda0599a1e9fc3a",
      "utils/motion/motion.cpp":"b79a642585fb457ee7d8330527179ea29d7903c84c6717ea626b51634b7e7e30",
      "utils/motion/motion.h":"b60c09ed367107bb968e8c88167ea3659ca194a3c64aaf8eb76ebd5bd78a5cf6",
      "drivers/FTServo_Arduino/src/SCSerial.cpp":"02c108fc008c476816e978ac757c8a7b534f126e246484540b337a1dda5ed665",
      "drivers/FTServo_Arduino/src/SCSerial.h":"6805c664a794bfc8813b1b9f5f3f7c01aa198702ac38ffb15ad4afce3df4e7bb"}
PRIOR={"M5StackChan.h":("54210d64f02ea416ce7036a2b9b433e7591bc5703752a0d9847ad609f8977d1b",),
       "utils/motion/motion.cpp":("0c80b975f5c96995834c6851c99a1b621766cc5b14a6da1cbf1d28ffcf732a3f",),
       "M5StackChan.cpp":("089dfbc52d1b8d8e489444e1d7f46f7f23bf82762ba9b3ff52b4d6378f468730","e8edbf1a522dea4049c2ba0ad818af88166da7b501d124ae3d6083a4b66cf3b5","6fc361fe9d25ad823d0ab1517c0468d3f6cd8b1d6df37bcc3934638919a0ac93")}

for _name,_hash in {'M5StackChan.cpp': '98cfc9d63d12600c93186e159211e89634433523fe5dff2e1fe53bb1685f1ba0', 'utils/motion/motion.cpp': 'ad3ae1dab583046eb4c518fed41dff6fc2d94901544caabf4699a39473fa1606', 'utils/motion/motion.h': 'c5660f90f1dcbea8e1ca7972f4160d65985439321df70f8ba23f37f7d667ad50'}.items():
    PRIOR[_name]=PRIOR.get(_name,())+(_hash,)
for _name,_hash in {'M5StackChan.cpp':'c392c7f830acb5a952664ba769d07402916970173afa6c3b794b35b4f789e17c',
                   'M5StackChan.h':'ed573160b056561b2ac8c31c81ed83bbb3badf519169a4a2a3415a19abb89461'}.items():
    PRIOR[_name]=PRIOR.get(_name,())+(_hash,)

for _name,_hash in {'M5StackChan.cpp': '7eee98166890552af30fa42c195121b8f9cfcf45033643a2131e768350245f69', 'M5StackChan.h': 'b8cdffdb0b13e131350c93b26011065ff9a13775377e294d6d2c4b6e45dbb4c4'}.items():
    PRIOR[_name]=PRIOR.get(_name,())+(_hash,)

for _name,_hash in {'M5StackChan.cpp': 'bb2e4701d4944ec3f7dd2a285fb3518b7a9323d683f22c38c1abd4ef550d11a4', 'M5StackChan.h': 'a4630c2b55fff7f31989e8b5af9033d91394959ea3ee35371bb8775900dd3a41'}.items():
    PRIOR[_name]=PRIOR.get(_name,())+(_hash,)

for _name,_hash in {'M5StackChan.cpp': 'd8e3fea79d21cc56cacd67ce75e1a7b3927c80353e9307b92789e707ef47a92b', 'M5StackChan.h': 'f856801bedca888d08733aeff71a980efc539998871d3133072f44c8e3d0198d'}.items():
    PRIOR[_name]=PRIOR.get(_name,())+(_hash,)

PRIOR["M5StackChan.cpp"] += ('103de766ad5a77f82ff8e7e6e04d9c8632b502eeeb52d62f723496f4512ef8d7',)

PRIOR["M5StackChan.cpp"] += ('02043c13eae0b7dff8d73ec689f7b3e8791a5f47ec82a40c8f0e8e367b9c2c27',)

def digest(raw):return hashlib.sha256(raw).hexdigest()


def patched(name,original):
    text=original.decode().replace("\r\n","\n")
    if name.startswith('drivers/FTServo_Arduino/src/SCSerial.'):
        return diagnostic_transport(name,text).encode()
    replacements={
      "M5StackChan.cpp":[
        ("        int current_pos = _scs_bus.ReadPos(_config.id);", "        int current_pos = _scs_bus.ReadPos(_config.id);\n        // Invalid feedback is not a clipped physical zero. Never calibrate from it.\n        if (current_pos < _config.rawPosLimit.x || current_pos > _config.rawPosLimit.y) return -32768;"),
        ("    M5.begin();","    auto quiet = M5.config();\n    // Configure pins/callbacks only; M5Unified does not start either driver here.\n    quiet.internal_mic = true;\n    quiet.internal_spk = true;\n    M5.begin(quiet);"),
        ("    servo_init();\n    ina226_init();","    // No servo bus or motion task is started during visual-only boot.\n    ina226_init();"),
        ("        _io_expander->setDirection(0, true);  // Output\n        _io_expander->setPullMode(0, true);   // Pull-up\n        setServoPowerEnabled(true);",
         "        _io_expander->digitalWrite(0, false); // Set the latch LOW before output.\n        _io_expander->setPullMode(0, false);  // Pull-down, never boot-time pull-up.\n        _io_expander->setDirection(0, true);  // Output\n        setServoPowerEnabled(false);"),
        ("void M5StackChan_Class::servo_init()", "int M5StackChan_Class::servoPowerGateLevel()\n{\n    return _io_expander ? (_io_expander->digitalRead(0) ? 1 : 0) : -1;\n}\n\nvoid M5StackChan_Class::prepareMotion()\n{\n    if (_motion_prepared) return;\n    servo_init();\n    _motion_prepared = true;\n}\n\nvoid M5StackChan_Class::servo_init()")],
      "M5StackChan.h":[
        ("    void begin();","    void begin();\n    // Call only after the deliberate, guarded servo-power warm-up.\n    void prepareMotion();\n    bool motionPrepared() const { return _motion_prepared; }\n    int servoPowerGateLevel();"),
        ("protected:\n", "protected:\n    bool _motion_prepared = false;\n")]
      ,"utils/motion/servo.h":[
        ("    bool isMoving();", "    bool isMoving();\n    // Cancel interpolation only; safe after cutting power, no bus access.\n    void cancelAnimation() { _angle_anim.teleport(_angle_anim.directValue()); _snap_to_target_on_rest = false; }")],
      "utils/motion/servo.cpp":[
        ("    _angle_anim.teleport(getCurrentAngle());\n    update();", "    const int initial = getCurrentAngle();\n    _angle_anim.teleport(initial >= _angle_limit.x && initial <= _angle_limit.y ? initial : _angle_limit.x);\n    update();"),
        ("        _angle_anim.teleport(getCurrentAngle());  // Use current angle as start", "        const int current = getCurrentAngle();\n        if (current < _angle_limit.x || current > _angle_limit.y) return;\n        _angle_anim.teleport(current);  // Use validated physical feedback as start")],
      "utils/motion/motion.cpp":[
        ("    _yaw_servo->move(_yaw_servo->getCurrentAngle());\n    _pitch_servo->move(_pitch_servo->getCurrentAngle());", "    if (_yaw_servo) _yaw_servo->cancelAnimation();\n    if (_pitch_servo) _pitch_servo->cancelAnimation();")]
    }
    replacements["M5StackChan.cpp"] += [
      ("#include <esp_log.h>", "#include <esp_log.h>\n#include <atomic>"),
      ("static SCSCL _scs_bus;", "static SCSCL _scs_bus;\nstatic std::atomic<int> _servo_raw[2]{{-32768},{-32768}}, _servo_error[2]{{0},{0}}, _servo_status[2]{{0},{0}};\nint M5StackChan_Class::servoFeedbackRaw(int axis) const {return axis>=0 && axis<2 ? _servo_raw[axis].load() : -32768;}\nint M5StackChan_Class::servoFeedbackError(int axis) const {return axis>=0 && axis<2 ? _servo_error[axis].load() : -1;}\nint M5StackChan_Class::servoFeedbackStatus(int axis) const {return axis>=0 && axis<2 ? _servo_status[axis].load() : -1;}"),
      ("        int current_pos = _scs_bus.ReadPos(_config.id);", "        int current_pos = _scs_bus.ReadPos(_config.id);\n        const int axis = _config.id-1;\n        if(axis>=0 && axis<2) {\n            _servo_raw[axis].store(current_pos);\n            _servo_error[axis].store(_scs_bus.getLastError());\n            _servo_status[axis].store(_scs_bus.getState());\n        }"),
      ("void M5StackChan_Class::prepareMotion()\n{\n    if (_motion_prepared) return;\n    servo_init();\n    _motion_prepared = true;\n}", "bool M5StackChan_Class::prepareMotion()\n{\n    if (_motion_prepared) return true;\n    _motion_prepared = servo_init();\n    if (_motion_prepared) Motion.setOutputEnabled(true);\n    return _motion_prepared;\n}"),
      ("void M5StackChan_Class::servo_init()", "bool M5StackChan_Class::servo_init()"),
      ("    _scs_bus.begin(UART_NUM_1, 1000000, 6, 7);", "    if (!_motion_uart_ready) _motion_uart_ready = _scs_bus.begin(UART_NUM_1, 1000000, 6, 7);\n    if (!_motion_uart_ready) return false;"),
      ("    Motion.init(std::move(yaw_servo), std::move(pitch_servo));", "    return Motion.init(std::move(yaw_servo), std::move(pitch_servo));"),
      ("    _io_expander->digitalWrite(0, enabled ? true : false);", "    _io_expander->digitalWrite(0, enabled ? true : false);\n    // Do not poll an unpowered UART bus during local audio or idle.\n    if (_motion_prepared) Motion.setOutputEnabled(enabled);")]
    replacements["M5StackChan.h"] += [
      ("    void prepareMotion();", "    bool prepareMotion();\n    bool motionUartReady() const { return _motion_uart_ready; }\n    int servoFeedbackRaw(int axis) const;\n    int servoFeedbackError(int axis) const;\n    int servoFeedbackStatus(int axis) const;"),
      ("    bool _motion_prepared = false;", "    bool _motion_prepared = false, _motion_uart_ready = false;"),
      ("    void servo_init();", "    bool servo_init();")]
    replacements["utils/motion/motion.h"] = [
      ("    void init(std::unique_ptr<Servo> yawServo, std::unique_ptr<Servo> pitchServo);", "    bool init(std::unique_ptr<Servo> yawServo, std::unique_ptr<Servo> pitchServo);"),
      ("    void stop();", "    void stop();\n    void setOutputEnabled(bool enabled) { _output_enabled.store(enabled); }"),
      ("    std::atomic<bool> _running{false};", "    std::atomic<bool> _running{false};\n    std::atomic<bool> _output_enabled{false};")]
    replacements["utils/motion/motion.cpp"] += [
      ("void Motion::init(std::unique_ptr<Servo> yawServo, std::unique_ptr<Servo> pitchServo)", "bool Motion::init(std::unique_ptr<Servo> yawServo, std::unique_ptr<Servo> pitchServo)"),
      ('        xTaskCreate(update_task, "motion_task", 4096, this, 10, &_task_handle);', '        if (xTaskCreate(update_task, "motion_task", 4096, this, 10, &_task_handle) != pdPASS || !_task_handle) {\n            _running = false;\n            return false;\n        }'),
      ('    }\n}\n\nvoid Motion::update_task', '    }\n    return true;\n}\n\nvoid Motion::update_task'),
      ("void Motion::update()\n{\n    std::lock_guard<std::mutex> lock(_mutex);", "void Motion::update()\n{\n    if (!_output_enabled.load()) return;\n    std::lock_guard<std::mutex> lock(_mutex);\n    if (!_output_enabled.load()) return;")]
    for before,after in replacements[name]:
        if text.count(before)!=1:raise ValueError(f"Pinned BSP patch context mismatch: {name}")
        text=text.replace(before,after)
    return bias_patch(name,diagnostic_board(name,text)).encode()


def prepare(root=ROOT,check=False):
    archive=root/"vendor/stackchan-bsp-quiet-original";result=[];plans=[]
    # Validate both files fully before writing either dependency file.
    for name,expected in PINS.items():
        path=root/REL/name;current=path.read_bytes();saved=archive/name
        original=saved.read_bytes() if saved.is_file() else current
        if digest(original)!=expected:raise ValueError(f"Unrecognized pinned original BSP: {name}")
        target=patched(name,original)
        if current not in (original,target) and digest(current) not in PRIOR.get(name,()):raise ValueError(f"Unexpected local BSP edits; refusing overwrite: {name}")
        if check and current!=target:raise ValueError(f"Quiet BSP patch not installed: {name}")
        plans.append((path,saved,original,target))
        result.append({"file":str(REL/name),"original_sha256":expected,"sha256":digest(target)})
    if not check:
        archive.mkdir(parents=True,exist_ok=True)
        for path,saved,original,target in plans:
            saved.parent.mkdir(parents=True,exist_ok=True)
            if not saved.exists():saved.write_bytes(original)
            if path.read_bytes()!=target:path.write_bytes(target)
        (root/"evidence/bsp-quiet-boot.json").write_text(json.dumps({"status":"PASS","files":result},indent=2)+"\n")
    return result


if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--check",action="store_true");args=p.parse_args()
    prepare(check=args.check)
    print("PASS pinned BSP: boot servo rail LOW, lazy servo init, audio pins/callbacks configured without starting either driver")
