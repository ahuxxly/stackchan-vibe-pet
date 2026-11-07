/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "M5StackChan.h"
#include "drivers/PY32IOExpander/PY32IOExpander.hpp"
#include "drivers/FTServo_Arduino/src/SCSCL.h"
#include "utils/compat/make_unique.h"
#include "utils/settings/settings.h"
#include "utility/power/INA226_Class.hpp"
#include <esp_log.h>
#include <atomic>

using namespace m5;

M5StackChan_Class M5StackChan;

static const char* TAG = "M5StackChan";

void M5StackChan_Class::begin()
{
    auto quiet = M5.config();
    // Configure pins/callbacks only; M5Unified does not start either driver here.
    quiet.internal_mic = true;
    quiet.internal_spk = true;
    M5.begin(quiet);
    TouchSensor.begin();
    io_expander_init();
    // No servo bus or motion task is started during visual-only boot.
    ina226_init();
}

void M5StackChan_Class::update()
{
    M5.update();
    TouchSensor.update();
}

/* -------------------------------------------------------------------------- */
/*                                 IO Expander                                */
/* -------------------------------------------------------------------------- */
std::unique_ptr<PY32IOExpander_Class> _io_expander;

void M5StackChan_Class::io_expander_init()
{
    _io_expander = std::make_unique<m5::PY32IOExpander_Class>();

    // PY32 IO Expander may boot slowly, wait for it
    uint32_t start_tick = millis();
    while (1) {
        delay(200);

        if (millis() - start_tick > 1200) {
            ESP_LOGE(TAG, "IO expander init timeout");
            _io_expander.reset();
            break;
        }

        if (_io_expander->begin()) {
            break;
        }
    }

    if (_io_expander) {
        // VM EN
        _io_expander->digitalWrite(0, false); // Set the latch LOW before output.
        _io_expander->setPullMode(0, false);  // Pull-down, never boot-time pull-up.
        _io_expander->setDirection(0, true);  // Output
        setServoPowerEnabled(false);
        delay(200);

        // RGB
        _io_expander->setDirection(13, true);   // Output
        _io_expander->setPullMode(13, true);    // Pull-up
        _io_expander->setDriveMode(13, false);  // Push-pull
        _io_expander->setLedCount(12);
        delay(200);
        showRgbColor(0, 0, 0);
        delay(50);
        showRgbColor(0, 0, 0);
    }
}

void M5StackChan_Class::setServoPowerEnabled(bool enabled)
{
    (void)setServoPowerEnabledChecked(enabled);
}

int M5StackChan_Class::setServoPowerEnabledChecked(bool enabled)
{
    // Preserve the pinned PY32 _writeBit transactions and expose their result.
    // Failed ON must not enable a previously initialized interpolation task.
    const int result = !_io_expander ? -1 :
        (enabled ? _io_expander->bitOn(0x05, 1) : _io_expander->bitOff(0x05, 1)) ? 1 : 0;
    if (_motion_prepared) Motion.setOutputEnabled(enabled && result == 1);
    return result;
}

bool M5StackChan_Class::readServoPowerRegister(uint8_t reg, uint8_t* value)
{
    // Only GPIO configuration/input/output and global GPIO mode, never ADC/IRQ.
    switch(reg) {
        case 0x02: case 0x03: case 0x05: case 0x07:
        case 0x09: case 0x0b: case 0x13: break;
        default: return false;
    }
    return value && _io_expander && _io_expander->readRegister(reg, value, 1);
}

void M5StackChan_Class::setRgbColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (!_io_expander) {
        return;
    }
    _io_expander->setLedColor(index, r, g, b);
}

void M5StackChan_Class::refreshRgb()
{
    if (!_io_expander) {
        return;
    }
    _io_expander->refreshLeds();
}

void M5StackChan_Class::showRgbColor(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < 12; i++) {
        setRgbColor(i, r, g, b);
    }
    refreshRgb();
}

/* -------------------------------------------------------------------------- */
/*                                    Servo                                   */
/* -------------------------------------------------------------------------- */
static SCSCL _scs_bus;
static std::atomic<int> _servo_raw[2]{{-32768},{-32768}}, _servo_error[2]{{0},{0}}, _servo_status[2]{{0},{0}};
int M5StackChan_Class::servoFeedbackRaw(int axis) const {return axis>=0 && axis<2 ? _servo_raw[axis].load() : -32768;}
int M5StackChan_Class::servoFeedbackError(int axis) const {return axis>=0 && axis<2 ? _servo_error[axis].load() : -1;}
int M5StackChan_Class::servoFeedbackStatus(int axis) const {return axis>=0 && axis<2 ? _servo_status[axis].load() : -1;}

struct ServoConfig_t {
    int id             = -1;
    int defaultZeroPos = 0;
    uitk_intl::Vector2i angleLimit;
    uitk_intl::Vector2i rawPosLimit;
    std::string settingNs;
    std::string settingZeroPositionKey;
    bool enablePwmMode = false;
};

/**
 * @brief Servo class implement
 *
 */
class ScsServo : public stackchan::motion::Servo {
public:
    static inline const std::string _tag = "ScsServo";

    ScsServo(const ServoConfig_t& config) : _config(config)
    {
    }

    void init() override
    {
        set_angle_limit(_config.angleLimit);
        get_zero_pos_from_nvs();
        Servo::init();
    }

    void get_zero_pos_from_nvs()
    {
        _zero_pos     = _config.defaultZeroPos;
        bool is_valid = false;

        {
            Settings settings(_config.settingNs, false);
            int nvs_zero_pos = settings.GetInt(_config.settingZeroPositionKey, -1);

            // Limit check
            if (nvs_zero_pos >= _config.rawPosLimit.x && nvs_zero_pos <= _config.rawPosLimit.y) {
                _zero_pos = nvs_zero_pos;
                is_valid  = true;
                ESP_LOGI(TAG, "Servo ID: %d get zero pos: %d from settings", _config.id, _zero_pos);
            } else {
                is_valid = false;
                ESP_LOGW(TAG, "Servo ID: %d get invalid zero pos: %d from settings", _config.id, nvs_zero_pos);
            }
        }

        if (!is_valid) {
            _zero_pos = _config.defaultZeroPos;
            ESP_LOGI(TAG, "Servo ID: %d override zero pos to default: %d", _config.id, _zero_pos);

            Settings settings(_config.settingNs, true);
            settings.SetInt(_config.settingZeroPositionKey, _zero_pos);
        }
    }

    void set_angle_impl(int angle) override
    {
        int mapped_angle = _zero_pos + angle * 16 / 5 / 10;  // 一步对应 0.3125度, 0.3125 = 5/16
        mapped_angle     = uitk_intl::clamp(mapped_angle, _config.rawPosLimit.x, _config.rawPosLimit.y);

        // ESP_LOGI(TAG, "Servo ID: %d mapped angle: %d", _config.id, mapped_angle);

        check_mode(Mode::Position);
        _scs_bus.WritePos(_config.id, mapped_angle, 20, 0);
    }

    int getCurrentAngle() override
    {
        int current_pos = _scs_bus.ReadPos(_config.id);
        const int axis = _config.id-1;
        if(axis>=0 && axis<2) {
            _servo_raw[axis].store(current_pos);
            _servo_error[axis].store(_scs_bus.getLastError());
            _servo_status[axis].store(_scs_bus.getState());
        }
        // Invalid feedback is not a clipped physical zero. Never calibrate from it.
        if (current_pos < _config.rawPosLimit.x || current_pos > _config.rawPosLimit.y) return -32768;
        int angle       = (current_pos - _zero_pos) * 5 * 10 / 16;
        angle           = uitk_intl::clamp(angle, getAngleLimit().x, getAngleLimit().y);
        // ESP_LOGI(TAG, "Servo ID: %d current pos: %d angle: %d", _id, current_pos, angle);
        return angle;
    }

    bool is_moving_impl() override
    {
        int moving = _scs_bus.ReadMove(_config.id);
        // ESP_LOGI(TAG, "Servo ID: %d moving: %d", _id, moving);
        return moving != 0;
    }

    void setTorqueEnabled(bool enabled) override
    {
        Servo::setTorqueEnabled(enabled);
        _scs_bus.EnableTorque(_config.id, enabled ? 1 : 0);
        // ESP_LOGI(TAG, "Servo ID: %d set torque: %d", _id, enabled);
    }

    bool getTorqueEnabled() override
    {
        int torque_enable = _scs_bus.ReadToqueEnable(_config.id);
        // ESP_LOGI(TAG, "Servo ID: %d torque enable: %d", _id, torque_enable);
        return torque_enable > 0;
    }

    void setCurrentAngleAsZero() override
    {
        _zero_pos = _scs_bus.ReadPos(_config.id);

        Settings settings(_config.settingNs, true);
        settings.SetInt(_config.settingZeroPositionKey, _zero_pos);

        ESP_LOGI(TAG, "Servo ID: %d set zero pos: %d to settings", _config.id, _zero_pos);
    }

    void rotate(int velocity) override
    {
        velocity = uitk_intl::clamp(velocity, -1000, 1000);

        if (!_config.enablePwmMode) {
            return;
        }

        int mapped_velocity = uitk_intl::map_range(velocity, 0, 1000, 0, 1023);

        check_mode(Mode::PWM);
        _scs_bus.WritePWM(_config.id, mapped_velocity);
    }

private:
    enum class Mode { Position = 0, PWM = 1 };

    ServoConfig_t _config;
    int _zero_pos      = 0;
    Mode _current_mode = Mode::Position;

    void check_mode(Mode targetMode)
    {
        if (targetMode == _current_mode) {
            return;
        }

        _scs_bus.SwitchMode(_config.id, static_cast<uint8_t>(targetMode));
        _current_mode = targetMode;
    }
};

int M5StackChan_Class::servoPowerGateLevel()
{
    return servoPowerDiagnosticField(0);
}

int M5StackChan_Class::servoPowerDiagnosticField(unsigned field)
{
    // Read only documented GPIO registers, never IRQ/ADC or a broad bus scan.
    const uint8_t regs[]={0x07,0x03,0x05,0x09,0x0b,0x13};
    uint8_t value=0;
    if(field>=6 || !readServoPowerRegister(regs[field], &value)) return -1;
    return value & 1;
}

const char* M5StackChan_Class::servoUartInitStage() const { return _scs_bus.initStage(); }
int M5StackChan_Class::servoUartInitError() const { return _scs_bus.initError(); }

bool M5StackChan_Class::prepareMotion()
{
    if (_motion_prepared) return true;
    _motion_prepared = servo_init();
    if (_motion_prepared) Motion.setOutputEnabled(true);
    return _motion_prepared;
}

bool M5StackChan_Class::servo_init()
{
    if (!_motion_uart_ready) _motion_uart_ready = _scs_bus.begin(UART_NUM_1, 1000000, 6, 7);
    if (!_motion_uart_ready) return false;

    uitk_intl::ui_hal::on_delay([](uint32_t ms) { delay(ms); });
    uitk_intl::ui_hal::on_get_tick([]() { return millis(); });

    ServoConfig_t yaw_servo_config;
    yaw_servo_config.id                     = 1;
    yaw_servo_config.defaultZeroPos         = 460;
    yaw_servo_config.angleLimit             = uitk_intl::Vector2i(-1280, 1280);
    yaw_servo_config.rawPosLimit            = uitk_intl::Vector2i(0, 1000);
    yaw_servo_config.settingNs              = "servo";
    yaw_servo_config.settingZeroPositionKey = "zero_pos_1";
    yaw_servo_config.enablePwmMode          = true;

    ServoConfig_t pitch_servo_config;
    pitch_servo_config.id                     = 2;
    pitch_servo_config.defaultZeroPos         = 620;
    pitch_servo_config.angleLimit             = uitk_intl::Vector2i(0, 900);
    pitch_servo_config.rawPosLimit            = uitk_intl::Vector2i(0, 1000);
    pitch_servo_config.settingNs              = "servo";
    pitch_servo_config.settingZeroPositionKey = "zero_pos_2";

    auto yaw_servo   = std::make_unique<ScsServo>(yaw_servo_config);
    auto pitch_servo = std::make_unique<ScsServo>(pitch_servo_config);

    return Motion.init(std::move(yaw_servo), std::move(pitch_servo));
}

/* -------------------------------------------------------------------------- */
/*                                   INA226                                   */
/* -------------------------------------------------------------------------- */
std::unique_ptr<m5::INA226_Class> _ina226;

void M5StackChan_Class::ina226_init()
{
    _ina226 = std::make_unique<m5::INA226_Class>(0x41);

    m5::INA226_Class::config_t config;
    config.shunt_res            = 0.01;
    config.max_expected_current = 8.19;
    _ina226->config(config);

    if (!_ina226->begin()) {
        ESP_LOGE(TAG, "INA226 init failed");
        _ina226.reset();
    }
}

float M5StackChan_Class::getBatteryVoltage()
{
    float result = 0.0f;
    if (_ina226) {
        result = _ina226->getBusVoltage();
    }
    return result;
}

float M5StackChan_Class::getBatteryCurrent()
{
    float result = 0.0f;
    if (_ina226) {
        result = _ina226->getShuntCurrent();
    }
    return result;
}
