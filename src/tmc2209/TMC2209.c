// ----------------------------------------------------------------------------
// Authors:
// Peter Polidoro peter@polidoro.io
// ----------------------------------------------------------------------------

#include "pico/time.h"

#include "TMC2209.h"
#include "SerialUART.h"

#ifndef constrain
#define constrain(amt, low, high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))
#endif

long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

bool TMC2209_blocking_;
SerialUART_t *TMC2209_serial_ptr_;
uint32_t TMC2209_serial_baud_rate_;
uint8_t TMC2209_serial_address_;

TMC2209_DriverCurrent_t TMC2209_driver_current_;
TMC2209_CoolConfig_t TMC2209_cool_config_;
bool TMC2209_cool_step_enabled_;
TMC2209_PwmConfig_t TMC2209_pwm_config_;
uint8_t toff_ = TMC2209_TOFF_DEFAULT;
TMC2209_ChopperConfig_t TMC2209_chopper_config_;
TMC2209_GlobalConfig_t TMC2209_global_config_;

void TMC2209_setup(SerialUART_t serial, long serial_baud_rate, SerialAddress_t serial_address) {
    TMC2209_blocking_ = true;
    TMC2209_serial_ptr_ = NULL;
    TMC2209_serial_baud_rate_ = 500000;
    TMC2209_serial_address_ = serial_address;
    TMC2209_cool_step_enabled_ = false;

    TMC2209_blocking_ = false;
    TMC2209_serial_baud_rate_ = serial_baud_rate;
    TMC2209_setOperationModeToSerial(serial, serial_baud_rate, serial_address);

    TMC2209_setRegistersToDefaults();

    TMC2209_readAndStoreRegisters();

    TMC2209_minimizeMotorCurrent();

    TMC2209_disable();

    TMC2209_disableAutomaticCurrentScaling();

    TMC2209_disableAutomaticGradientAdaptation();

    if (!TMC2209_isSetupAndCommunicating()) {
        TMC2209_blocking_ = true;
    }
}

bool TMC2209_isCommunicating() {
    return (TMC2209_getVersion() == VERSION);
}

bool TMC2209_isSetupAndCommunicating() {
    return TMC2209_serialOperationMode();
}

bool TMC2209_isCommunicatingButNotSetup() {
    return (TMC2209_isCommunicating() && (!
            TMC2209_isSetupAndCommunicating()));
}

void TMC2209_enable() {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_chopper_config_.toff = toff_;
    TMC2209_writeStoredChopperConfig();
}

void TMC2209_disable() {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_chopper_config_.toff = TOFF_DISABLE;
    TMC2209_writeStoredChopperConfig();
}

bool TMC2209_disabledByInputPin() {
    if (TMC2209_blocking_) {
        return false;
    }
    TMC2209_Input_t input;
    input.bytes = TMC2209_read(ADDRESS_IOIN);

    return input.enn;
}

void TMC2209_setMicrostepsPerStep(uint16_t microsteps_per_step) {
    if (TMC2209_blocking_) {
        return;
    }
    uint16_t microsteps_per_step_shifted = constrain(microsteps_per_step,
                                                     MICROSTEPS_PER_STEP_MIN,
                                                     MICROSTEPS_PER_STEP_MAX);
    microsteps_per_step_shifted = microsteps_per_step >> 1;
    uint16_t exponent = 0;
    while (microsteps_per_step_shifted > 0) {
        microsteps_per_step_shifted = microsteps_per_step_shifted >> 1;
        ++exponent;
    }
    TMC2209_setMicrostepsPerStepPowerOfTwo(exponent);
}

void TMC2209_setMicrostepsPerStepPowerOfTwo(uint8_t exponent) {
    if (TMC2209_blocking_) {
        return;
    }
    switch (exponent) {
        case 0: {
            TMC2209_chopper_config_.mres = MRES_001;
            break;
        }
        case 1: {
            TMC2209_chopper_config_.mres = MRES_002;
            break;
        }
        case 2: {
            TMC2209_chopper_config_.mres = MRES_004;
            break;
        }
        case 3: {
            TMC2209_chopper_config_.mres = MRES_008;
            break;
        }
        case 4: {
            TMC2209_chopper_config_.mres = MRES_016;
            break;
        }
        case 5: {
            TMC2209_chopper_config_.mres = MRES_032;
            break;
        }
        case 6: {
            TMC2209_chopper_config_.mres = MRES_064;
            break;
        }
        case 7: {
            TMC2209_chopper_config_.mres = MRES_128;
            break;
        }
        case 8:
        default: {
            TMC2209_chopper_config_.mres = MRES_256;
            break;
        }
    }
    TMC2209_writeStoredChopperConfig();
}

uint16_t TMC2209_getMicrostepsPerStep() {
    if (TMC2209_blocking_) {
        return 0;
    }
    uint16_t microsteps_per_step_exponent;
    switch (TMC2209_chopper_config_.mres) {
        case MRES_001: {
            microsteps_per_step_exponent = 0;
            break;
        }
        case MRES_002: {
            microsteps_per_step_exponent = 1;
            break;
        }
        case MRES_004: {
            microsteps_per_step_exponent = 2;
            break;
        }
        case MRES_008: {
            microsteps_per_step_exponent = 3;
            break;
        }
        case MRES_016: {
            microsteps_per_step_exponent = 4;
            break;
        }
        case MRES_032: {
            microsteps_per_step_exponent = 5;
            break;
        }
        case MRES_064: {
            microsteps_per_step_exponent = 6;
            break;
        }
        case MRES_128: {
            microsteps_per_step_exponent = 7;
            break;
        }
        case MRES_256:
        default: {
            microsteps_per_step_exponent = 8;
            break;
        }
    }
    return 1 << microsteps_per_step_exponent;
}

void TMC2209_setRunCurrent(uint8_t percent) {
    if (TMC2209_blocking_) {
        return;
    }
    uint8_t run_current = TMC2209_percentToCurrentSetting(percent);
    TMC2209_driver_current_.irun = run_current;
    TMC2209_writeStoredDriverCurrent();
}

void TMC2209_setHoldCurrent(uint8_t percent) {
    if (TMC2209_blocking_) {
        return;
    }
    uint8_t hold_current = TMC2209_percentToCurrentSetting(percent);

    TMC2209_driver_current_.ihold = hold_current;
    TMC2209_writeStoredDriverCurrent();
}

void TMC2209_setHoldDelay(uint8_t percent) {
    if (TMC2209_blocking_) {
        return;
    }
    uint8_t hold_delay = TMC2209_percentToHoldDelaySetting(percent);

    TMC2209_driver_current_.iholddelay = hold_delay;
    TMC2209_writeStoredDriverCurrent();
}

void TMC2209_setAllCurrentValues(uint8_t run_current_percent,
                                 uint8_t hold_current_percent,
                                 uint8_t hold_delay_percent) {
    if (TMC2209_blocking_) {
        return;
    }
    uint8_t run_current = TMC2209_percentToCurrentSetting(run_current_percent);
    uint8_t hold_current = TMC2209_percentToCurrentSetting(hold_current_percent);
    uint8_t hold_delay = TMC2209_percentToHoldDelaySetting(hold_delay_percent);

    TMC2209_driver_current_.irun = run_current;
    TMC2209_driver_current_.ihold = hold_current;
    TMC2209_driver_current_.iholddelay = hold_delay;
    TMC2209_writeStoredDriverCurrent();
}

TMC2209_Settings_t TMC2209_getSettings() {
    TMC2209_Settings_t settings;
    settings.is_communicating = TMC2209_isCommunicating();

    if (settings.is_communicating) {
        TMC2209_readAndStoreRegisters();

        settings.is_setup = TMC2209_global_config_.pdn_disable;
        settings.enabled = (TMC2209_chopper_config_.toff > TOFF_DISABLE);
        settings.microsteps_per_step = TMC2209_getMicrostepsPerStep();
        settings.inverse_motor_direction_enabled = TMC2209_global_config_.shaft;
        settings.stealth_chop_enabled = !
                TMC2209_global_config_.enable_spread_cycle;
        settings.standstill_mode = TMC2209_pwm_config_.freewheel;
        settings.irun_percent = TMC2209_currentSettingToPercent(TMC2209_driver_current_.irun);
        settings.irun_register_value = TMC2209_driver_current_.irun;
        settings.ihold_percent = TMC2209_currentSettingToPercent(TMC2209_driver_current_.ihold);
        settings.ihold_register_value = TMC2209_driver_current_.ihold;
        settings.iholddelay_percent = TMC2209_holdDelaySettingToPercent(TMC2209_driver_current_.iholddelay);
        settings.iholddelay_register_value = TMC2209_driver_current_.iholddelay;
        settings.automatic_current_scaling_enabled = TMC2209_pwm_config_.pwm_autoscale;
        settings.automatic_gradient_adaptation_enabled = TMC2209_pwm_config_.pwm_autograd;
        settings.pwm_offset = TMC2209_pwm_config_.pwm_offset;
        settings.pwm_gradient = TMC2209_pwm_config_.pwm_grad;
        settings.cool_step_enabled = TMC2209_cool_step_enabled_;
        settings.analog_current_scaling_enabled = TMC2209_global_config_.i_scale_analog;
        settings.internal_sense_resistors_enabled = TMC2209_global_config_.internal_rsense;
    } else {
        settings.is_setup = false;
        settings.enabled = false;
        settings.microsteps_per_step = 0;
        settings.inverse_motor_direction_enabled = false;
        settings.stealth_chop_enabled = false;
        settings.standstill_mode = TMC2209_pwm_config_.freewheel;
        settings.irun_percent = 0;
        settings.irun_register_value = 0;
        settings.ihold_percent = 0;
        settings.ihold_register_value = 0;
        settings.iholddelay_percent = 0;
        settings.iholddelay_register_value = 0;
        settings.automatic_current_scaling_enabled = false;
        settings.automatic_gradient_adaptation_enabled = false;
        settings.pwm_offset = 0;
        settings.pwm_gradient = 0;
        settings.cool_step_enabled = false;
        settings.analog_current_scaling_enabled = false;
        settings.internal_sense_resistors_enabled = false;
    }

    return settings;
}

Status_t TMC2209_getStatus() {
    TMC2209_DriveStatus_t drive_status = {.bytes= 0};
    if (!TMC2209_blocking_) {
        drive_status.bytes = TMC2209_read(ADDRESS_DRV_STATUS);
    }
    return drive_status.status;
}

void TMC2209_enableInverseMotorDirection() {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_global_config_.shaft = 1;
    TMC2209_writeStoredGlobalConfig();
}

void TMC2209_disableInverseMotorDirection() {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_global_config_.shaft = 0;
    TMC2209_writeStoredGlobalConfig();
}

void TMC2209_setStandstillMode(TMC2209_StandstillMode_t mode) {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_pwm_config_.freewheel = mode;
    TMC2209_writeStoredPwmConfig();
}

void TMC2209_enableAutomaticCurrentScaling() {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_pwm_config_.pwm_autoscale = STEPPER_DRIVER_FEATURE_ON;
    TMC2209_writeStoredPwmConfig();
}

void TMC2209_disableAutomaticCurrentScaling() {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_pwm_config_.pwm_autoscale = STEPPER_DRIVER_FEATURE_OFF;
    TMC2209_writeStoredPwmConfig();
}

void TMC2209_enableAutomaticGradientAdaptation() {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_pwm_config_.pwm_autograd = STEPPER_DRIVER_FEATURE_ON;
    TMC2209_writeStoredPwmConfig();
}

void TMC2209_disableAutomaticGradientAdaptation() {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_pwm_config_.pwm_autograd = STEPPER_DRIVER_FEATURE_OFF;
    TMC2209_writeStoredPwmConfig();
}

void TMC2209_setPwmOffset(uint8_t pwm_amplitude) {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_pwm_config_.pwm_offset = pwm_amplitude;
    TMC2209_writeStoredPwmConfig();
}

void TMC2209_setPwmGradient(uint8_t pwm_amplitude) {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_pwm_config_.pwm_grad = pwm_amplitude;
    TMC2209_writeStoredPwmConfig();
}

void TMC2209_setPowerDownDelay(uint8_t delay) {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_write(ADDRESS_TPOWERDOWN, delay);
}

uint8_t TMC2209_getInterfaceTransmissionCounter() {
    if (TMC2209_blocking_) {
        return 0;
    }
    return TMC2209_read(ADDRESS_IFCNT);
}

void TMC2209_moveAtVelocity(int32_t microsteps_per_period) {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_write(ADDRESS_VACTUAL, microsteps_per_period);
}

void TMC2209_moveUsingStepDirInterface() {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_write(ADDRESS_VACTUAL, VACTUAL_STEP_DIR_INTERFACE);
}

void TMC2209_enableStealthChop() {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_global_config_.enable_spread_cycle = 0;
    TMC2209_writeStoredGlobalConfig();
}

void TMC2209_disableStealthChop() {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_global_config_.enable_spread_cycle = 1;
    TMC2209_writeStoredGlobalConfig();
}

uint32_t TMC2209_getInterstepDuration() {
    if (TMC2209_blocking_) {
        return 0;
    }
    return TMC2209_read(ADDRESS_TSTEP);
}

void TMC2209_setCoolStepDurationThreshold(uint32_t duration_threshold) {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_write(ADDRESS_TCOOLTHRS, duration_threshold);
}

void TMC2209_setStealthChopDurationThreshold(uint32_t duration_threshold) {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_write(ADDRESS_TPWMTHRS, duration_threshold);
}

uint16_t TMC2209_getStallGuardResult() {
    if (TMC2209_blocking_) {
        return 0;
    }
    return TMC2209_read(ADDRESS_SG_RESULT);
}

void TMC2209_setStallGuardThreshold(uint8_t stall_guard_threshold) {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_write(ADDRESS_SGTHRS, stall_guard_threshold);
}

uint8_t TMC2209_getPwmScaleSum() {
    if (TMC2209_blocking_) {
        return 0;
    }
    TMC2209_PwmScale_t pwm_scale;
    pwm_scale.bytes = TMC2209_read(ADDRESS_PWM_SCALE);

    return pwm_scale.pwm_scale_sum;
}

int16_t TMC2209_getPwmScaleAuto() {
    if (TMC2209_blocking_) {
        return 0;
    }
    TMC2209_PwmScale_t pwm_scale;
    pwm_scale.bytes = TMC2209_read(ADDRESS_PWM_SCALE);

    return pwm_scale.pwm_scale_auto;
}

uint8_t TMC2209_getPwmOffsetAuto() {
    if (TMC2209_blocking_) {
        return 0;
    }
    TMC2209_PwmAuto_t pwm_auto;
    pwm_auto.bytes = TMC2209_read(ADDRESS_PWM_AUTO);

    return pwm_auto.pwm_offset_auto;
}

uint8_t TMC2209_getPwmGradientAuto() {
    if (TMC2209_blocking_) {
        return 0;
    }
    TMC2209_PwmAuto_t pwm_auto;
    pwm_auto.bytes = TMC2209_read(ADDRESS_PWM_AUTO);

    return pwm_auto.pwm_gradient_auto;
}

void TMC2209_enableCoolStep(uint8_t lower_threshold,
                            uint8_t upper_threshold) {
    if (TMC2209_blocking_) {
        return;
    }
    lower_threshold = constrain(lower_threshold, SEMIN_MIN, SEMIN_MAX);
    TMC2209_cool_config_.semin = lower_threshold;
    upper_threshold = constrain(upper_threshold, SEMAX_MIN, SEMAX_MAX);
    TMC2209_cool_config_.semax = upper_threshold;
    TMC2209_write(ADDRESS_COOLCONF, TMC2209_cool_config_.bytes);
    TMC2209_cool_step_enabled_ = true;
}

void TMC2209_disableCoolStep() {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_cool_config_.semin = SEMIN_OFF;
    TMC2209_write(ADDRESS_COOLCONF, TMC2209_cool_config_.bytes);
    TMC2209_cool_step_enabled_ = false;
}

void TMC2209_setCoolStepCurrentIncrement(TMC2209_CurrentIncrement_t current_increment) {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_cool_config_.seup = current_increment;
    TMC2209_write(ADDRESS_COOLCONF, TMC2209_cool_config_.bytes);
}

void TMC2209_setCoolStepMeasurementCount(TMC2209_MeasurementCount_t measurement_count) {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_cool_config_.sedn = measurement_count;
    TMC2209_write(ADDRESS_COOLCONF, TMC2209_cool_config_.bytes);
}

uint16_t TMC2209_getMicrostepCounter() {
    if (TMC2209_blocking_) {
        return 0;
    }
    return TMC2209_read(ADDRESS_MSCNT);
}

void TMC2209_enableAnalogCurrentScaling() {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_global_config_.i_scale_analog = 1;
    TMC2209_writeStoredGlobalConfig();
}

void TMC2209_disableAnalogCurrentScaling() {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_global_config_.i_scale_analog = 0;
    TMC2209_writeStoredGlobalConfig();
}

void TMC2209_useExternalSenseResistors() {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_global_config_.internal_rsense = 0;
    TMC2209_writeStoredGlobalConfig();
}

void TMC2209_useInternalSenseResistors() {
    if (TMC2209_blocking_) {
        return;
    }
    TMC2209_global_config_.internal_rsense = 1;
    TMC2209_writeStoredGlobalConfig();
}

// private
void TMC2209_setOperationModeToSerial(SerialUART_t serial, long serial_baud_rate, SerialAddress_t serial_address) {
    TMC2209_serial_ptr_ = &serial;
    TMC2209_serial_address_ = serial_address;

    SerialUART_begin(serial_baud_rate, SERIAL_8N1);

    TMC2209_global_config_.
            bytes = 0;
    TMC2209_global_config_.
            i_scale_analog = 0;
    TMC2209_global_config_.
            pdn_disable = 1;
    TMC2209_global_config_.
            mstep_reg_select = 1;
    TMC2209_global_config_.
            multistep_filt = 1;

    TMC2209_writeStoredGlobalConfig();

}

void TMC2209_setRegistersToDefaults() {
    TMC2209_driver_current_.bytes = 0;
    TMC2209_driver_current_.ihold = IHOLD_DEFAULT;
    TMC2209_driver_current_.irun = IRUN_DEFAULT;
    TMC2209_driver_current_.iholddelay = IHOLDDELAY_DEFAULT;
    TMC2209_write(ADDRESS_IHOLD_IRUN, TMC2209_driver_current_.bytes);

    TMC2209_chopper_config_.bytes = CHOPPER_CONFIG_DEFAULT;
    TMC2209_chopper_config_.tbl = TBL_DEFAULT;
    TMC2209_chopper_config_.hend = HEND_DEFAULT;
    TMC2209_chopper_config_.hstart = HSTART_DEFAULT;
    TMC2209_chopper_config_.toff = TMC2209_TOFF_DEFAULT;
    TMC2209_write(ADDRESS_CHOPCONF, TMC2209_chopper_config_.bytes);

    TMC2209_pwm_config_.bytes = PWM_CONFIG_DEFAULT;
    TMC2209_write(ADDRESS_PWMCONF, TMC2209_pwm_config_.bytes);

    TMC2209_cool_config_.bytes = COOLCONF_DEFAULT;
    TMC2209_write(ADDRESS_COOLCONF, TMC2209_cool_config_.bytes);

    TMC2209_write(ADDRESS_TPOWERDOWN, TPOWERDOWN_DEFAULT);
    TMC2209_write(ADDRESS_TPWMTHRS, TPWMTHRS_DEFAULT);
    TMC2209_write(ADDRESS_VACTUAL, VACTUAL_DEFAULT);
    TMC2209_write(ADDRESS_TCOOLTHRS, TCOOLTHRS_DEFAULT);
    TMC2209_write(ADDRESS_SGTHRS, SGTHRS_DEFAULT);
    TMC2209_write(ADDRESS_COOLCONF, COOLCONF_DEFAULT);
}

void TMC2209_readAndStoreRegisters() {
    TMC2209_global_config_.bytes = TMC2209_readGlobalConfigBytes();
    TMC2209_chopper_config_.bytes = TMC2209_readChopperConfigBytes();
    TMC2209_pwm_config_.bytes = TMC2209_readPwmConfigBytes();
}

uint8_t TMC2209_getVersion() {
    TMC2209_Input_t input;
    input.bytes = TMC2209_read(ADDRESS_IOIN);

    return input.version;
}

bool TMC2209_serialOperationMode() {
    TMC2209_GlobalConfig_t global_config;
    global_config.bytes = TMC2209_readGlobalConfigBytes();

    return global_config.pdn_disable;
}

void TMC2209_minimizeMotorCurrent() {
    TMC2209_driver_current_.irun = CURRENT_SETTING_MIN;
    TMC2209_driver_current_.ihold = CURRENT_SETTING_MIN;
    TMC2209_writeStoredDriverCurrent();
}

uint32_t TMC2209_reverseData(uint32_t data) {
    uint32_t reversed_data = 0;
    uint8_t right_shift;
    uint8_t left_shift;
    for (uint8_t i = 0; i < DATA_SIZE; ++i) {
        right_shift = (DATA_SIZE - i - 1) * BITS_PER_BYTE;
        left_shift = i * BITS_PER_BYTE;
        reversed_data |= ((data >> right_shift) & BYTE_MAX_VALUE) << left_shift;
    }
    return reversed_data;
}

uint8_t TMC2209_calculateCrcRead(TMC2209_ReadRequestDatagram_t datagram, uint8_t datagram_size) {
    uint8_t crc = 0;
    uint8_t byte;
    for (uint8_t i = 0; i < (datagram_size - 1); ++i) {
        byte = (datagram.bytes >> (i * BITS_PER_BYTE)) & BYTE_MAX_VALUE;
        for (uint8_t j = 0; j < BITS_PER_BYTE; ++j) {
            if ((crc >> 7) ^ (byte & 0x01)) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc = crc << 1;
            }
            byte = byte >> 1;
        }
    }
    return crc;
}


void TMC2209_sendDatagramRead(TMC2209_ReadRequestDatagram_t datagram, uint8_t datagram_size) {
    if (!TMC2209_serial_ptr_) {
        return;
    }

    uint8_t byte;

// clear the serial receive buffer if necessary
    while (SerialUART_available() > 0) {
        byte = SerialUART_read();
    }

    for (uint8_t i = 0; i < datagram_size; ++i) {
        byte = (datagram.bytes >> (i * BITS_PER_BYTE)) & BYTE_MAX_VALUE;
        SerialUART_write(byte);
    }

// wait for bytes sent out on TX line to be echoed on RX line
    uint32_t echo_delay = 0;
    while ((SerialUART_available() < datagram_size) && (echo_delay < ECHO_DELAY_MAX_MICROSECONDS)) {
        sleep_us(ECHO_DELAY_INC_MICROSECONDS);
        echo_delay += ECHO_DELAY_INC_MICROSECONDS;
    }

    if (echo_delay >= ECHO_DELAY_MAX_MICROSECONDS) {
        TMC2209_blocking_ = true;
        return;
    }

// clear RX buffer of echo bytes
    for (uint8_t i = 0; i < datagram_size; ++i) {
        byte = SerialUART_read();
    }
}

uint8_t TMC2209_calculateCrcWrite(TMC2209_WriteReadReplyDatagram_t datagram, uint8_t datagram_size) {
    uint8_t crc = 0;
    uint8_t byte;
    for (uint8_t i = 0; i < (datagram_size - 1); ++i) {
        byte = (datagram.bytes >> (i * BITS_PER_BYTE)) & BYTE_MAX_VALUE;
        for (uint8_t j = 0; j < BITS_PER_BYTE; ++j) {
            if ((crc >> 7) ^ (byte & 0x01)) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc = crc << 1;
            }
            byte = byte >> 1;
        }
    }
    return crc;
}


void TMC2209_sendDatagramWrite(TMC2209_WriteReadReplyDatagram_t datagram, uint8_t datagram_size) {
    if (!TMC2209_serial_ptr_) {
        return;
    }

    uint8_t byte;

// clear the serial receive buffer if necessary
    while (SerialUART_available() > 0) {
        byte = SerialUART_read();
    }

    for (uint8_t i = 0; i < datagram_size; ++i) {
        byte = (datagram.bytes >> (i * BITS_PER_BYTE)) & BYTE_MAX_VALUE;
        SerialUART_write(byte);
    }

// wait for bytes sent out on TX line to be echoed on RX line
    uint32_t echo_delay = 0;
    while ((SerialUART_available() < datagram_size) && (echo_delay < ECHO_DELAY_MAX_MICROSECONDS)) {
        sleep_us(ECHO_DELAY_INC_MICROSECONDS);
        echo_delay += ECHO_DELAY_INC_MICROSECONDS;
    }

    if (echo_delay >= ECHO_DELAY_MAX_MICROSECONDS) {
        TMC2209_blocking_ = true;
        return;
    }

// clear RX buffer of echo bytes
    for (uint8_t i = 0; i < datagram_size; ++i) {
        byte = SerialUART_read();
    }
}

void TMC2209_write(uint8_t register_address, uint32_t data) {
    TMC2209_WriteReadReplyDatagram_t write_datagram;
    write_datagram.bytes = 0;
    write_datagram.sync = SYNC;
    write_datagram.serial_address = TMC2209_serial_address_;
    write_datagram.register_address = register_address;
    write_datagram.rw = RW_WRITE;
    write_datagram.data = TMC2209_reverseData(data);
    write_datagram.crc = TMC2209_calculateCrcWrite(write_datagram, WRITE_READ_REPLY_DATAGRAM_SIZE);

    TMC2209_sendDatagramWrite(write_datagram, WRITE_READ_REPLY_DATAGRAM_SIZE);
}

uint32_t TMC2209_read(uint8_t register_address) {
    if (!TMC2209_serial_ptr_) {
        return 0;
    }
    TMC2209_ReadRequestDatagram_t read_request_datagram;
    read_request_datagram.bytes = 0;
    read_request_datagram.sync = SYNC;
    read_request_datagram.serial_address = TMC2209_serial_address_;
    read_request_datagram.register_address = register_address;
    read_request_datagram.rw = RW_READ;
    read_request_datagram.crc = TMC2209_calculateCrcRead(read_request_datagram, READ_REQUEST_DATAGRAM_SIZE);

    TMC2209_sendDatagramRead(read_request_datagram, READ_REQUEST_DATAGRAM_SIZE);

    uint32_t reply_delay = 0;
    while ((SerialUART_available() < WRITE_READ_REPLY_DATAGRAM_SIZE)
           &&
           (reply_delay < REPLY_DELAY_MAX_MICROSECONDS)) {
        sleep_us(REPLY_DELAY_INC_MICROSECONDS);
        reply_delay += REPLY_DELAY_INC_MICROSECONDS;
    }

    if (reply_delay >= REPLY_DELAY_MAX_MICROSECONDS) {
        TMC2209_blocking_ = true;
        return 0;
    }

    uint64_t byte;
    uint8_t byte_count = 0;
    TMC2209_WriteReadReplyDatagram_t read_reply_datagram;
    read_reply_datagram.bytes = 0;
    for (uint8_t i = 0; i < WRITE_READ_REPLY_DATAGRAM_SIZE; ++i) {
        byte = SerialUART_read();
        read_reply_datagram.bytes |= (byte << (byte_count++ * BITS_PER_BYTE));
    }

    // this unfortunate code was found to be necessary after testing on hardware
    uint32_t post_read_delay_repeat = POST_READ_DELAY_NUMERATOR / TMC2209_serial_baud_rate_;
    for (uint8_t i = 0; i < post_read_delay_repeat; ++i) {
        sleep_us(POST_READ_DELAY_INC_MICROSECONDS);
    }
    return TMC2209_reverseData(read_reply_datagram.data);
}

uint8_t TMC2209_percentToCurrentSetting(uint8_t percent) {
    uint8_t constrained_percent = constrain(percent,
                                            PERCENT_MIN,
                                            PERCENT_MAX);
    uint8_t current_setting = map(constrained_percent,
                                  PERCENT_MIN,
                                  PERCENT_MAX,
                                  CURRENT_SETTING_MIN,
                                  CURRENT_SETTING_MAX);
    return current_setting;
}

uint8_t TMC2209_currentSettingToPercent(uint8_t current_setting) {
    uint8_t percent = map(current_setting,
                          CURRENT_SETTING_MIN,
                          CURRENT_SETTING_MAX,
                          PERCENT_MIN,
                          PERCENT_MAX);
    return percent;
}

uint8_t TMC2209_percentToHoldDelaySetting(uint8_t percent) {
    uint8_t constrained_percent = constrain(percent,
                                            PERCENT_MIN,
                                            PERCENT_MAX);
    uint8_t hold_delay_setting = map(constrained_percent,
                                     PERCENT_MIN,
                                     PERCENT_MAX,
                                     HOLD_DELAY_MIN,
                                     HOLD_DELAY_MAX);
    return hold_delay_setting;
}

uint8_t TMC2209_holdDelaySettingToPercent(uint8_t hold_delay_setting) {
    uint8_t percent = map(hold_delay_setting,
                          HOLD_DELAY_MIN,
                          HOLD_DELAY_MAX,
                          PERCENT_MIN,
                          PERCENT_MAX);
    return percent;
}

void TMC2209_writeStoredGlobalConfig() {
    TMC2209_write(ADDRESS_GCONF, TMC2209_global_config_.bytes);
}

uint32_t TMC2209_readGlobalConfigBytes() {
    return TMC2209_read(ADDRESS_GCONF);
}

void TMC2209_writeStoredDriverCurrent() {
    TMC2209_write(ADDRESS_IHOLD_IRUN, TMC2209_driver_current_.bytes);

    if (TMC2209_driver_current_.irun >= SEIMIN_UPPER_CURRENT_LIMIT) {
        TMC2209_cool_config_.seimin = SEIMIN_UPPER_SETTING;
    } else {
        TMC2209_cool_config_.seimin = SEIMIN_LOWER_SETTING;
    }
    if (TMC2209_cool_step_enabled_) {
        TMC2209_write(ADDRESS_COOLCONF, TMC2209_cool_config_.bytes);
    }
}

void TMC2209_writeStoredChopperConfig() {
    TMC2209_write(ADDRESS_CHOPCONF, TMC2209_chopper_config_.bytes);
}

uint32_t TMC2209_readChopperConfigBytes() {
    return TMC2209_read(ADDRESS_CHOPCONF);
}

void TMC2209_writeStoredPwmConfig() {
    TMC2209_write(ADDRESS_PWMCONF, TMC2209_pwm_config_.bytes);
}

uint32_t TMC2209_readPwmConfigBytes() {
    return TMC2209_read(ADDRESS_PWMCONF);
}
