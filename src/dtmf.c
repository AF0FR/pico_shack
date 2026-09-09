#include "dtmf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "hardware/adc.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "audio.h"
#include "config.h"
#include "fox.h"
#include "morse.h"
#include "radio.h"
#include "settings.h"
#include "station_control.h"
#include "workflow.h"

#define DTMF_SAMPLE_RATE_HZ       8000u
#define DTMF_FRAME_SAMPLES         205u
#define DTMF_STABLE_FRAMES           3u
#define DTMF_RELEASE_FRAMES          2u
#define DTMF_PREFIX_TIMEOUT_MS     3000u
#define DTMF_MIN_FRAME_ENERGY    100000u
#define DTMF_RECENT_DIGITS            8u
#define DTMF_TURNAROUND_MS           350u
#define DTMF_ACK_MS                  500u
#define DTMF_ACK_SETTLE_MS          1000u
#define DTMF_ACK_POST_MS            1000u
#define DTMF_ACK_GAP_MS              100u

typedef enum {
    DTMF_COMMAND_NONE = 0,
    DTMF_COMMAND_START,
    DTMF_COMMAND_STOP,
    DTMF_COMMAND_ID,
    DTMF_COMMAND_SEQUENCE_ONCE,
    DTMF_COMMAND_FOX_IDENTIFIER,
    DTMF_COMMAND_WARBLE,
    DTMF_COMMAND_SWEEP,
    DTMF_COMMAND_RESTART,
    DTMF_COMMAND_CANCEL,
} dtmf_command_t;

// Q14 values of 2*cos(2*pi*f/8000) for the four row and column tones.
static const int16_t coefficients[8] = {
    27980, 26956, 25701, 24154, 19073, 16325, 13085, 9279
};

static const char digit_map[4][4] = {
    {'1', '2', '3', 'A'},
    {'4', '5', '6', 'B'},
    {'7', '8', '9', 'C'},
    {'*', '0', '#', 'D'},
};

static struct repeating_timer sample_timer;
static int32_t q1[8];
static int32_t q2[8];
static int32_t dc_level = 2048;
static uint64_t frame_energy;
static uint16_t sample_count;
static char candidate_digit;
static char latched_digit;
static uint8_t stable_frames;
static uint8_t release_frames;
static bool prefix_armed;
static uint32_t prefix_deadline_ms;
static volatile dtmf_command_t pending_command;
static dtmf_command_t released_command;
static char recent_digits[DTMF_RECENT_DIGITS + 1u];

static dtmf_command_t command_for_digit(char digit)
{
    switch (digit) {
    case '0': return DTMF_COMMAND_STOP;
    case '1': return DTMF_COMMAND_START;
    case '2': return DTMF_COMMAND_ID;
    case '3': return DTMF_COMMAND_RESTART;
    case '4': return DTMF_COMMAND_SEQUENCE_ONCE;
    case '5': return DTMF_COMMAND_FOX_IDENTIFIER;
    case '6': return DTMF_COMMAND_WARBLE;
    case '7': return DTMF_COMMAND_SWEEP;
    case '#': return DTMF_COMMAND_CANCEL;
    default: return DTMF_COMMAND_NONE;
    }
}

static void record_digit(char digit)
{
    const size_t length = strlen(recent_digits);
    if (length == DTMF_RECENT_DIGITS) {
        memmove(recent_digits, recent_digits + 1u, DTMF_RECENT_DIGITS - 1u);
        recent_digits[DTMF_RECENT_DIGITS - 1u] = digit;
    } else {
        recent_digits[length] = digit;
    }
    recent_digits[DTMF_RECENT_DIGITS] = '\0';
}

static char decode_frame(void)
{
    int64_t power[8];
    for (unsigned i = 0; i < 8u; ++i) {
        power[i] = (int64_t)q1[i] * q1[i] + (int64_t)q2[i] * q2[i] -
                   (((int64_t)coefficients[i] * q1[i] * q2[i]) >> 14);
        q1[i] = 0;
        q2[i] = 0;
    }

    const uint64_t energy = frame_energy;
    frame_energy = 0u;
    if (energy < DTMF_MIN_FRAME_ENERGY) return '\0';

    unsigned row = 0u;
    unsigned row_second = 1u;
    if (power[row_second] > power[row]) { row = 1u; row_second = 0u; }
    for (unsigned i = 2u; i < 4u; ++i) {
        if (power[i] > power[row]) { row_second = row; row = i; }
        else if (power[i] > power[row_second]) row_second = i;
    }

    unsigned column = 4u;
    unsigned column_second = 5u;
    if (power[column_second] > power[column]) { column = 5u; column_second = 4u; }
    for (unsigned i = 6u; i < 8u; ++i) {
        if (power[i] > power[column]) { column_second = column; column = i; }
        else if (power[i] > power[column_second]) column_second = i;
    }

    // Both tone groups must dominate their neighbors and together account for
    // a meaningful share of the frame energy.
    if (power[row] < power[row_second] * 4 ||
        power[column] < power[column_second] * 4 ||
        power[row] > power[column] * 8 ||
        power[column] > power[row] * 8 ||
        power[row] + power[column] < (int64_t)energy * 20) {
        return '\0';
    }
    return digit_map[row][column - 4u];
}

static void accept_digit(char digit)
{
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (prefix_armed && (int32_t)(now - prefix_deadline_ms) >= 0) {
        prefix_armed = false;
    }

    if (digit == '*') {
        prefix_armed = true;
        released_command = DTMF_COMMAND_NONE;
        prefix_deadline_ms = now + DTMF_PREFIX_TIMEOUT_MS;
        return;
    }
    if (!prefix_armed) return;
    prefix_armed = false;

    released_command = command_for_digit(digit);
}

static void process_digit(char digit)
{
    if (digit == '\0') {
        candidate_digit = '\0';
        stable_frames = 0u;
        if (latched_digit != '\0' && ++release_frames >= DTMF_RELEASE_FRAMES) {
            latched_digit = '\0';
            release_frames = 0u;
            if (released_command != DTMF_COMMAND_NONE) {
                pending_command = released_command;
                released_command = DTMF_COMMAND_NONE;
            }
        }
        return;
    }

    release_frames = 0u;
    if (digit != candidate_digit) {
        candidate_digit = digit;
        stable_frames = 1u;
    } else if (stable_frames < DTMF_STABLE_FRAMES) {
        ++stable_frames;
    }
    if (stable_frames == DTMF_STABLE_FRAMES && digit != latched_digit) {
        latched_digit = digit;
        record_digit(digit);
        accept_digit(digit);
    }
}

static bool sample_callback(struct repeating_timer *timer)
{
    (void)timer;
    if (gpio_get(PTT_PIN) == PTT_ACTIVE_LEVEL) {
        for (unsigned i = 0; i < 8u; ++i) {
            q1[i] = 0;
            q2[i] = 0;
        }
        frame_energy = 0u;
        sample_count = 0u;
        candidate_digit = '\0';
        latched_digit = '\0';
        stable_frames = 0u;
        release_frames = 0u;
        prefix_armed = false;
        released_command = DTMF_COMMAND_NONE;
        return true;
    }
    const int32_t sample = (int32_t)adc_read();
    dc_level += (sample - dc_level) >> 6;
    const int32_t centered = sample - dc_level;
    frame_energy += (uint64_t)((int64_t)centered * centered);

    for (unsigned i = 0; i < 8u; ++i) {
        const int32_t q0 = centered +
            (int32_t)(((int64_t)coefficients[i] * q1[i]) >> 14) - q2[i];
        q2[i] = q1[i];
        q1[i] = q0;
    }

    if (++sample_count == DTMF_FRAME_SAMPLES) {
        sample_count = 0u;
        process_digit(decode_frame());
    }
    return true;
}

void dtmf_init(void)
{
    adc_init();
    adc_gpio_init(SPEAKER_INPUT_PIN);
    adc_select_input(SPEAKER_ADC_INPUT);
    add_repeating_timer_us(-(int64_t)(1000000u / DTMF_SAMPLE_RATE_HZ),
                           sample_callback, NULL, &sample_timer);
}

static bool send_courtesy(unsigned count, bool remain_keyed)
{
    sleep_ms(DTMF_TURNAROUND_MS);
    if (!radio_ptt_on()) return false;
    // Allow the radio's transmit audio path to settle before the first beep.
    // This is additional to the normal PTT lead used by CW.
    sleep_ms(DTMF_ACK_SETTLE_MS);
    if (!station_control_transmission_allowed()) {
        radio_ptt_off();
        return false;
    }
    for (unsigned i = 0; i < count; ++i) {
        audio_play_tone(COURTESY_TONE_HZ, DTMF_ACK_MS);
        if (i + 1u < count) sleep_ms(DTMF_ACK_GAP_MS);
    }
    // Keep PTT asserted with silent audio before continuing or unkeying.
    sleep_ms(DTMF_ACK_POST_MS);
    if (!remain_keyed) radio_ptt_off();
    return true;
}

void dtmf_poll(void)
{
    const uint32_t interrupt_state = save_and_disable_interrupts();
    const dtmf_command_t command = pending_command;
    pending_command = DTMF_COMMAND_NONE;
    restore_interrupts(interrupt_state);
    if (command == DTMF_COMMAND_NONE) return;

    fox_settings_t settings;
    settings_get(&settings);
    const bool was_enabled = station_control_is_enabled();
    station_control_set_manual_mode(false);
    station_control_set_enabled(true);

    if (command == DTMF_COMMAND_CANCEL) {
        send_courtesy(2u, false);
        if (!was_enabled) station_control_complete_stop();
        station_control_set_manual_mode(settings.operating_mode == 1u);
        return;
    }

    const bool continues_transmitting =
        command != DTMF_COMMAND_START;
    if (!send_courtesy(1u, continues_transmitting)) {
        if (!was_enabled) station_control_complete_stop();
        station_control_set_manual_mode(settings.operating_mode == 1u);
        return;
    }

    if (command == DTMF_COMMAND_START || command == DTMF_COMMAND_STOP) {
        const bool enabled = command == DTMF_COMMAND_START;
        settings.transmit_enabled = enabled;
        settings_set(&settings);
        station_control_set_enabled(enabled);
        station_control_set_manual_mode(settings.operating_mode == 1u);
        // Discard the interrupted cycle, including any one-shot state.
        fox_request_sequence(false);
        return;
    }

    if (command == DTMF_COMMAND_SEQUENCE_ONCE ||
        command == DTMF_COMMAND_RESTART) {
        settings.operating_mode = 0u;
        settings.transmit_enabled = 1u;
        settings_set(&settings);
        station_control_set_manual_mode(false);
        station_control_set_enabled(true);
        fox_request_sequence(command == DTMF_COMMAND_SEQUENCE_ONCE);
        return;
    }

    const workflow_step_t previous_step = workflow_get();
    if (command == DTMF_COMMAND_ID) {
        workflow_set(WORKFLOW_STATION_ID);
        morse_transmit(settings.station_id);
    } else if (command == DTMF_COMMAND_FOX_IDENTIFIER) {
        workflow_set(WORKFLOW_FOX_1);
        morse_transmit(settings.fox_identifier);
    } else if (command == DTMF_COMMAND_WARBLE) {
        workflow_set(WORKFLOW_WARBLE);
        if (radio_ptt_on()) audio_play_warble();
    } else if (command == DTMF_COMMAND_SWEEP) {
        workflow_set(WORKFLOW_SWEEP);
        if (radio_ptt_on()) audio_play_sweep();
    }
    radio_ptt_off();
    if (!was_enabled) station_control_complete_stop();
    station_control_set_manual_mode(settings.operating_mode == 1u);
    workflow_set(previous_step);
}

bool dtmf_queue_command(char digit)
{
    const dtmf_command_t command = command_for_digit(digit);
    if (command == DTMF_COMMAND_NONE) return false;
    const uint32_t interrupt_state = save_and_disable_interrupts();
    pending_command = command;
    restore_interrupts(interrupt_state);
    return true;
}

void dtmf_get_recent(char *destination, size_t destination_size)
{
    if (destination_size == 0u) return;
    const uint32_t interrupt_state = save_and_disable_interrupts();
    strncpy(destination, recent_digits, destination_size - 1u);
    destination[destination_size - 1u] = '\0';
    restore_interrupts(interrupt_state);
}
