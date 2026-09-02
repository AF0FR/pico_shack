#include "audio.h"

#include <stdbool.h>

#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

#include "config.h"
#include "settings.h"
#include "station_control.h"

static uint audio_slice;
static uint audio_channel;
static uint16_t audio_target_level;
static bool audio_active;

// Half-cosine amplitude values from 0 to 1, sampled at 1 ms intervals.
static const uint16_t envelope[6] = {0u, 6259u, 22641u, 42894u, 59276u, 65535u};

static void set_envelope_level(uint16_t scale)
{
    pwm_set_chan_level(audio_slice, audio_channel,
                       ((uint32_t)audio_target_level * scale) / 65535u);
}

void audio_init(void)
{
    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);
    audio_slice = pwm_gpio_to_slice_num(AUDIO_PIN);
    audio_channel = pwm_gpio_to_channel(AUDIO_PIN);
    pwm_set_enabled(audio_slice, false);
    audio_active = false;
}

void audio_start_tone(uint32_t frequency_hz)
{
    const uint16_t wrap = 4095u;
    fox_settings_t settings;
    settings_get(&settings);
    const float divider = (float)clock_get_hz(clk_sys) /
                          ((float)frequency_hz * (float)(wrap + 1u));

    audio_target_level = (uint16_t)(((uint32_t)(wrap + 1u) *
                                    settings.audio_gain_percent) / 200u);
    if (audio_active) {
        pwm_set_clkdiv(audio_slice, divider);
        return;
    }

    pwm_set_enabled(audio_slice, false);
    pwm_set_clkdiv(audio_slice, divider);
    pwm_set_wrap(audio_slice, wrap);
    pwm_set_chan_level(audio_slice, audio_channel, 0u);
    pwm_set_counter(audio_slice, 0u);
    pwm_set_enabled(audio_slice, true);
    audio_active = true;
    for (uint i = 1u; i <= AUDIO_ENVELOPE_MS; ++i) {
        set_envelope_level(envelope[i]);
        sleep_ms(1);
    }
}

void audio_stop(void)
{
    if (audio_active) {
        for (int i = (int)AUDIO_ENVELOPE_MS - 1; i >= 0; --i) {
            set_envelope_level(envelope[i]);
            sleep_ms(1);
        }
    }
    pwm_set_enabled(audio_slice, false);
    gpio_set_function(AUDIO_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(AUDIO_PIN, GPIO_OUT);
    gpio_put(AUDIO_PIN, 0);
    gpio_set_function(AUDIO_PIN, GPIO_FUNC_PWM);
    audio_active = false;
}

void audio_play_warble(void)
{
    fox_settings_t settings;
    settings_get(&settings);
    const absolute_time_t deadline = make_timeout_time_ms(settings.warble_duration_ms);
    bool use_high_tone = false;

    while (!time_reached(deadline) && station_control_transmission_allowed()) {
        audio_start_tone(use_high_tone ? settings.warble_high_hz : settings.warble_low_hz);
        use_high_tone = !use_high_tone;

        const int64_t remaining_ms = absolute_time_diff_us(get_absolute_time(), deadline) / 1000;
        sleep_ms(remaining_ms < settings.warble_switch_ms ?
                 (uint32_t)remaining_ms : settings.warble_switch_ms);
    }

    audio_stop();
}

void audio_play_sweep(void)
{
    fox_settings_t settings;
    settings_get(&settings);
    const absolute_time_t deadline = make_timeout_time_ms(settings.sweep_duration_ms);
    int frequency = settings.sweep_low_hz;
    int step = settings.sweep_step_hz;

    while (!time_reached(deadline) && station_control_transmission_allowed()) {
        audio_start_tone((uint32_t)frequency);

        const int64_t remaining_ms = absolute_time_diff_us(get_absolute_time(), deadline) / 1000;
        sleep_ms(remaining_ms < settings.sweep_step_ms ?
                 (uint32_t)remaining_ms : settings.sweep_step_ms);

        frequency += step;
        if (frequency >= (int)settings.sweep_high_hz) {
            frequency = settings.sweep_high_hz;
            step = -(int)settings.sweep_step_hz;
        } else if (frequency <= (int)settings.sweep_low_hz) {
            frequency = settings.sweep_low_hz;
            step = settings.sweep_step_hz;
        }
    }

    audio_stop();
}
