#pragma once

// Station identification. Change this before transmitting.
#define STATION_ID "KB0TLL"

// Raspberry Pi Pico GPIO assignments.
#define PTT_PIN   0u
#define AUDIO_PIN 1u

#define DIT_PIN                 2u
#define DAH_PIN                 3u

// Pico 2 W access point. WPA2 passwords must contain at least 8 characters.
#define WIFI_AP_SSID     "PicoShack"
#define WIFI_AP_PASSWORD "picoshack1"

// Set to 0 if the PTT interface is active-low at the Pico GPIO.
#define PTT_ACTIVE_LEVEL 1

// Morse settings.
#define CW_WPM     15u
#define CW_TONE_HZ 500u
#define AUDIO_GAIN_PERCENT 50u

#define OPERATING_MODE_DEFAULT 0u
#define KEYER_MODE_DEFAULT     1u
#define KEYER_REVERSED_DEFAULT 0u
#define KEYER_HANG_MS_DEFAULT  500u

// Warble settings.
#define WARBLE_LOW_HZ      300u
#define WARBLE_HIGH_HZ     800u
#define WARBLE_SWITCH_MS   140u
#define WARBLE_DURATION_MS 5000u

// Triangle sweep settings.
#define SWEEP_LOW_HZ      300u
#define SWEEP_HIGH_HZ     800u
#define SWEEP_STEP_HZ     20u
#define SWEEP_STEP_MS     20u
#define SWEEP_DURATION_MS 5000u

// Off-air intervals.
#define FOX_PAUSE_MS   1000u
#define TONE_PAUSE_MS  5000u
#define IDLE_DURATION_MS 15000u

// Pauses shorter than this remain keyed with silent audio. Longer pauses
// release PTT normally.
#define PTT_RELEASE_PAUSE_MS 500u

// Radio key-up and key-down protection.
#define PTT_LEAD_MS 150u
#define PTT_TAIL_MS 100u
