"""Narrow additions to the hash-pinned BSP; no electrical configuration changes."""


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise ValueError('Motion diagnostic patch context mismatch: '+before[:90])
    return text.replace(before, after)


def replace_function(text, signature, replacement):
    start = text.index(signature)
    end = text.index('{', start)+1
    depth = 1
    while depth:
        depth += (text[end] == '{')-(text[end] == '}')
        end += 1
    return text[:start]+replacement+text[end:]


def transport(name, text):
    if name.endswith('.h'):
        return replace_once(text, '\tvoid end();', '''\tvoid end();
    // Cached initialization result. Does not access UART hardware.
    const char* initStage() const { return _init_stage; }
    int initError() const { return _init_error; }
private:
    const char* _init_stage = "not_started";
    int _init_error = 0;
public:''')
    text = replace_function(text, 'bool SCSerial::begin(', '''bool SCSerial::begin(uart_port_t requested, int baud_rate, int tx_pin, int rx_pin, int buf_size)
{
    // A failed install must never confer ownership of an existing driver.
    if (uart_num != UART_NUM_MAX) {
        _init_stage = "already_open"; _init_error = ESP_ERR_INVALID_STATE;
        return false;
    }
    uart_config_t config = {
        .baud_rate = baud_rate, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };
    _init_stage = "install";
    _init_error = uart_driver_install(requested, buf_size, buf_size, 0, NULL, 0);
    if (_init_error != ESP_OK) {
        ESP_LOGE(TAG, "UART install failed: %s", esp_err_to_name(_init_error));
        return false;
    }
    uart_num = requested; // Only now is this instance entitled to use/delete it.
    _init_stage = "configure";
    _init_error = uart_param_config(uart_num, &config);
    if (_init_error == ESP_OK) {
        _init_stage = "pins";
        _init_error = uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (_init_error != ESP_OK) {
        ESP_LOGE(TAG, "UART setup failed: %s", esp_err_to_name(_init_error));
        // Retain ownership if cleanup itself fails; never delete a foreign port.
        const esp_err_t cleanup = uart_driver_delete(uart_num);
        if (cleanup == ESP_OK) uart_num = UART_NUM_MAX;
        else { _init_stage = "cleanup"; _init_error = cleanup; }
        return false;
    }
    _init_stage = "ready";
    return true;
}''')
    return replace_function(text, 'void SCSerial::end()', '''void SCSerial::end()
{
    if (uart_num < UART_NUM_MAX) {
        const esp_err_t result = uart_driver_delete(uart_num);
        if (result == ESP_OK) uart_num = UART_NUM_MAX;
        else { _init_stage = "cleanup"; _init_error = result; }
    }
}''')


def board(name, text):
    if name == 'M5StackChan.h':
        return replace_once(text, '    int servoPowerGateLevel();', '''    int servoPowerGateLevel();
    // -1 unavailable, 0 transaction failed, 1 completed; not physical voltage.
    int setServoPowerEnabledChecked(bool enabled);
    bool readServoPowerRegister(uint8_t reg, uint8_t* value);
    // One checked read per call: input, direction, output, pull-up/down, drive.
    // GPIO state only, NOT a VM voltage measurement. -1 means failed/unavailable.
    int servoPowerDiagnosticField(unsigned field);
    const char* servoUartInitStage() const;
    int servoUartInitError() const;''')
    if name != 'M5StackChan.cpp':
        return text
    text = replace_function(text, 'void M5StackChan_Class::setServoPowerEnabled(bool enabled)', '''void M5StackChan_Class::setServoPowerEnabled(bool enabled)
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
}''')
    text = replace_function(text, 'int M5StackChan_Class::servoPowerGateLevel()' , '''int M5StackChan_Class::servoPowerGateLevel()
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
int M5StackChan_Class::servoUartInitError() const { return _scs_bus.initError(); }''')
    return text
