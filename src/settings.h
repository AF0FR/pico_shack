#pragma once

#include <stdbool.h>
#include <stdint.h>

#define STATION_ID_MAX_LENGTH 15u
#define WIFI_SSID_MAX_LENGTH 32u
#define WIFI_PASSWORD_MAX_LENGTH 63u

typedef struct {
    char station_id[STATION_ID_MAX_LENGTH + 1u];
    char wifi_ssid[WIFI_SSID_MAX_LENGTH + 1u];
    char wifi_password[WIFI_PASSWORD_MAX_LENGTH + 1u];
    uint8_t transmit_enabled;
    uint8_t operating_mode;
    uint8_t startup_feature;
    uint8_t keyer_mode;
    uint8_t keyer_reversed;
    uint16_t keyer_hang_ms;
    uint16_t cw_wpm;
    uint16_t cw_tone_hz;
    uint8_t audio_gain_percent;
    uint16_t warble_low_hz;
    uint16_t warble_high_hz;
    uint16_t warble_switch_ms;
    uint16_t warble_duration_ms;
    uint16_t sweep_low_hz;
    uint16_t sweep_high_hz;
    uint16_t sweep_step_hz;
    uint16_t sweep_step_ms;
    uint16_t sweep_duration_ms;
    uint16_t fox_pause_ms;
    uint16_t tone_pause_ms;
    uint16_t idle_ms;
} fox_settings_t;

typedef enum {
    SETTINGS_VALID = 0,
    SETTINGS_ERROR_STATION_ID,
    SETTINGS_ERROR_WIFI_SSID,
    SETTINGS_ERROR_WIFI_PASSWORD,
    SETTINGS_ERROR_FLAGS,
    SETTINGS_ERROR_MODE,
    SETTINGS_ERROR_KEYER_MODE,
    SETTINGS_ERROR_KEYER_HANG,
    SETTINGS_ERROR_CW_WPM,
    SETTINGS_ERROR_CW_TONE,
    SETTINGS_ERROR_GAIN,
    SETTINGS_ERROR_WARBLE_RANGE,
    SETTINGS_ERROR_WARBLE_SWITCH,
    SETTINGS_ERROR_WARBLE_DURATION,
    SETTINGS_ERROR_SWEEP_RANGE,
    SETTINGS_ERROR_SWEEP_STEP,
    SETTINGS_ERROR_SWEEP_STEP_TIME,
    SETTINGS_ERROR_SWEEP_DURATION,
    SETTINGS_ERROR_FOX_PAUSE,
    SETTINGS_ERROR_TONE_PAUSE,
    SETTINGS_ERROR_IDLE,
} settings_validation_t;

void settings_init(void);
void settings_get(fox_settings_t *destination);
void settings_get_defaults(fox_settings_t *destination);
settings_validation_t settings_validate(const fox_settings_t *candidate);
const char *settings_validation_message(settings_validation_t result);
bool settings_set(const fox_settings_t *candidate);
void settings_restore_defaults(void);

// Writes a pending web update to flash. Call only from the main execution
// context, preferably while PTT is off. Returns true if nothing is pending or
// the save succeeded.
bool settings_save_if_dirty(void);
