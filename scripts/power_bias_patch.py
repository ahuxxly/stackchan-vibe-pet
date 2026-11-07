"""Restore factory VM_EN bias without enabling it; apply to the pinned BSP only."""
from motion_diagnostics_patch import replace_once, replace_function


def patch(name, text):
    if name == 'M5StackChan.h':
        text = replace_once(text, '    bool readServoPowerRegister(uint8_t reg, uint8_t* value);', '''    bool readServoPowerRegister(uint8_t reg, uint8_t* value);
    bool servoPowerConfigReady() const { return _servo_power_configured; }
    const char* servoPowerConfigStage() const { return _servo_power_stage; }
    const char* servoPowerConfigError() const { return _servo_power_error; }
    int servoPowerConfigRegister() const { return _servo_power_reg; }
    int servoPowerConfigActual() const { return _servo_power_actual; }''')
        return replace_once(text, 'protected:\n', '''protected:
    bool _servo_power_configured = false;
    const char* _servo_power_stage = "not_started";
    const char* _servo_power_error = "none";
    int _servo_power_reg = -1, _servo_power_actual = -1;
    bool initializeServoPowerBias();
    bool configureServoPowerBit(uint8_t reg, bool value, const char* stage);
    bool verifyServoPowerLow(const char* stage);
''')
    if name != 'M5StackChan.cpp':
        return text
    text = replace_once(text, '    _io_expander = std::make_unique<m5::PY32IOExpander_Class>();',
                        '    _servo_power_configured = false;\n    _servo_power_stage = "io_wait"; _servo_power_error = "none";\n    _io_expander = std::make_unique<m5::PY32IOExpander_Class>();')
    text = replace_once(text, '            ESP_LOGE(TAG, "IO expander init timeout");',
                        '            _servo_power_stage = "io_timeout"; _servo_power_error = "unavailable";\n            ESP_LOGE(TAG, "IO expander init timeout");')
    text = replace_once(text, '''        _io_expander->digitalWrite(0, false); // Set the latch LOW before output.
        _io_expander->setPullMode(0, false);  // Pull-down, never boot-time pull-up.
        _io_expander->setDirection(0, true);  // Output
        setServoPowerEnabled(false);''', '''        if (!initializeServoPowerBias()) {
            // One independent cutoff attempt; never continue into a pull-up after failure.
            // Preserve the original initialization error for runtime diagnostics.
            setServoPowerEnabled(false);
            ESP_LOGE(TAG, "VM_EN config failed at %s: %s", _servo_power_stage, _servo_power_error);
            return;
        }
        // initializeServoPowerBias already verified LOW; setup records its own checked cutoff.''')
    text = replace_once(text, 'void M5StackChan_Class::setServoPowerEnabled(bool enabled)', r'''// VM_EN drives a transistor base (official SCH_Power Q4/R16). Retain the
// factory open-drain/internal pull-up design; never select push-pull here.
// This is boot-only, before the foreground IMU loop. It does not enable power.
bool M5StackChan_Class::configureServoPowerBit(uint8_t reg, bool value, const char* stage)
{
    _servo_power_stage = stage; _servo_power_error = "none";
    _servo_power_reg = reg; _servo_power_actual = -1;
    uint8_t before = 0, after = 0;
    if (!_io_expander || !_io_expander->readRegister(reg, &before, 1)) {
        _servo_power_error = "read_failed"; return false;
    }
    const uint8_t expected = (before & uint8_t(0xfe)) | uint8_t(value);
    if (!_io_expander->writeRegister8(reg, expected)) {
        _servo_power_error = "write_failed"; return false;
    }
    if (!_io_expander->readRegister(reg, &after, 1)) {
        _servo_power_error = "verify_read_failed"; return false;
    }
    _servo_power_actual = after;
    if (after != expected) { _servo_power_error = "readback_mismatch"; return false; }
    return true;
}

bool M5StackChan_Class::verifyServoPowerLow(const char* stage)
{
    _servo_power_stage = stage; _servo_power_error = "none";
    _servo_power_reg = 0x07; _servo_power_actual = -1;
    uint8_t input = 0;
    if (!_io_expander || !_io_expander->readRegister(0x07, &input, 1)) {
        _servo_power_error = "input_read_failed"; return false;
    }
    _servo_power_actual = input;
    if (input & 1) { _servo_power_error = "input_not_low"; return false; }
    return true;
}

bool M5StackChan_Class::initializeServoPowerBias()
{
    _servo_power_configured = false;
    if (_motion_prepared) Motion.setOutputEnabled(false);
    // LOW must be acknowledged AND read back before any direction/bias change.
    if (!configureServoPowerBit(0x05, false, "latch_low")) return false;
    if (!configureServoPowerBit(0x13, true, "open_drain")) return false;
    if (!configureServoPowerBit(0x03, true, "output")) return false;
    if (!verifyServoPowerLow("before_pull_up")) return false;
    if (!configureServoPowerBit(0x0b, false, "pull_down_off")) return false;
    if (!configureServoPowerBit(0x09, true, "pull_up_on")) return false;
    if (!verifyServoPowerLow("final_low")) return false;
    _servo_power_stage = "ready_off";
    _servo_power_configured = true;
    return true;
}

void M5StackChan_Class::setServoPowerEnabled(bool enabled)''')
    text = replace_function(text, 'int M5StackChan_Class::setServoPowerEnabledChecked(', '''int M5StackChan_Class::setServoPowerEnabledChecked(bool enabled)
{
    // Cutoff remains available after a configuration failure. ON requires a
    // verified LOW-first initialization; existing input/IMU/pose gates remain.
    const int result = !_io_expander ? -1 :
        enabled && !_servo_power_configured ? 0 :
        (enabled ? _io_expander->bitOn(0x05, 1) : _io_expander->bitOff(0x05, 1)) ? 1 : 0;
    if (_motion_prepared) Motion.setOutputEnabled(enabled && result == 1);
    return result;
}''')
    text = replace_once(text, '    // Only GPIO configuration/input/output and global GPIO mode, never ADC/IRQ.',
                        '    // GPIO configuration/input/output and REG_VERSION (0x02), never ADC/IRQ.')
    return replace_once(text, '''bool M5StackChan_Class::prepareMotion()
{
    if (_motion_prepared) return true;''', '''bool M5StackChan_Class::prepareMotion()
{
    if (!_servo_power_configured) return false;
    if (_motion_prepared) return true;''')
