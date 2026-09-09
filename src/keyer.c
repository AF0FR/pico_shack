#include "keyer.h"

#include <stdbool.h>
#include "pico/stdlib.h"
#include "audio.h"
#include "config.h"
#include "radio.h"
#include "settings.h"
#include "station_control.h"
#include "workflow.h"

static volatile bool web_dit_pressed;
static volatile bool web_dah_pressed;

static void read_keys(const fox_settings_t *s, bool *dit, bool *dah)
{
    const bool physical_dit = !gpio_get(s->keyer_reversed ? DAH_PIN : DIT_PIN);
    // A mono straight-key plug can ground the unused paddle contact.
    const bool physical_dah = s->keyer_mode != 0u &&
        !gpio_get(s->keyer_reversed ? DIT_PIN : DAH_PIN);
    *dit = physical_dit || web_dit_pressed;
    *dah = physical_dah || web_dah_pressed;
}

void keyer_set_web_key(bool dah, bool pressed)
{
    if (dah) web_dah_pressed = pressed;
    else web_dit_pressed = pressed;
}

void keyer_release_web_keys(void)
{
    web_dit_pressed = false;
    web_dah_pressed = false;
}

static void wait_keys(uint32_t ms, const fox_settings_t *s, bool *dit, bool *dah)
{
    for (uint32_t i = 0; i < ms; ++i) {
        bool d, h;
        read_keys(s, &d, &h);
        *dit |= d;
        *dah |= h;
        sleep_ms(1);
    }
}

void keyer_init(void)
{
    gpio_init(DIT_PIN); gpio_set_dir(DIT_PIN, GPIO_IN); gpio_pull_up(DIT_PIN);
    gpio_init(DAH_PIN); gpio_set_dir(DAH_PIN, GPIO_IN); gpio_pull_up(DAH_PIN);
}

static void straight_key(const fox_settings_t *s)
{
    bool dit, dah;
    read_keys(s, &dit, &dah);
    audio_start_tone(s->cw_tone_hz);
    while ((dit || dah) && station_control_transmission_allowed()) {
        sleep_ms(1);
        read_keys(s, &dit, &dah);
    }
    audio_stop();
}

static void paddle_key(const fox_settings_t *s, bool initial_dit, bool initial_dah)
{
    const uint32_t dit_ms = 1200u / s->cw_wpm;
    bool dit_memory = initial_dit, dah_memory = initial_dah, last_dah = true;
    bool squeeze_seen = initial_dit && initial_dah;
    while (station_control_transmission_allowed()) {
        bool dit, dah;
        read_keys(s, &dit, &dah);
        dit_memory |= dit; dah_memory |= dah;
        squeeze_seen |= dit && dah;
        if (!dit_memory && !dah_memory) break;

        bool send_dah = dit_memory && dah_memory ? !last_dah : dah_memory;
        if (send_dah) dah_memory = false; else dit_memory = false;
        audio_start_tone(s->cw_tone_hz);
        const uint32_t element_ms = send_dah ? 3u * dit_ms : dit_ms;
        const uint32_t ramp_ms = 2u * AUDIO_ENVELOPE_MS;
        wait_keys(element_ms > ramp_ms ? element_ms - ramp_ms : 0u,
                  s, &dit_memory, &dah_memory);
        audio_stop();
        wait_keys(dit_ms, s, &dit_memory, &dah_memory);
        last_dah = send_dah;

        read_keys(s, &dit, &dah);
        if (s->keyer_mode == 2u && squeeze_seen && !dit && !dah &&
            !dit_memory && !dah_memory) {
            if (last_dah) dit_memory = true; else dah_memory = true;
        }
        if (!dit && !dah) squeeze_seen = false;
    }
}

void keyer_run(void)
{
    fox_settings_t s;
    settings_get(&s);
    workflow_set(WORKFLOW_KEYER_READY);
    if (station_control_stop_requested()) {
        audio_stop();
        radio_ptt_off();
        station_control_complete_stop();
    }
    bool dit, dah;
    read_keys(&s, &dit, &dah);
    if (!dit && !dah) {
        settings_save_if_dirty();
        sleep_ms(1);
        return;
    }

    workflow_set(WORKFLOW_KEYER_ACTIVE);
    if (!radio_ptt_on()) return;
    do {
        if (s.keyer_mode == 0u) straight_key(&s); else paddle_key(&s, dit, dah);

        const absolute_time_t deadline = make_timeout_time_ms(s.keyer_hang_ms);
        dit = dah = false;
        while (!time_reached(deadline) && station_control_transmission_allowed()) {
            read_keys(&s, &dit, &dah);
            if (dit || dah) break;
            sleep_ms(1);
        }
        // Handle another element here so every keyed path reaches PTT-off,
        // even if a short press disappears before the next input read.
    } while ((dit || dah) && station_control_transmission_allowed());
    radio_ptt_off();
}
