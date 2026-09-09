#include "radio.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"

#include "audio.h"
#include "config.h"
#include "dtmf.h"
#include "fox.h"
#include "morse.h"
#include "settings.h"
#include "station_control.h"
#include "web.h"
#include "workflow.h"

static inline int ptt_inactive_level(void)
{
    return !PTT_ACTIVE_LEVEL;
}

static volatile bool ptt_active;

void radio_init(void)
{
    gpio_init(PTT_PIN);
    gpio_set_dir(PTT_PIN, GPIO_OUT);
    gpio_put(PTT_PIN, ptt_inactive_level());
    ptt_active = false;

    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
}

bool radio_ptt_on(void)
{
    if (!station_control_transmission_allowed()) {
        return false;
    }
    if (ptt_active) {
        return true;
    }
    gpio_put(PTT_PIN, PTT_ACTIVE_LEVEL);
    ptt_active = true;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
    sleep_ms(PTT_LEAD_MS);
    if (!station_control_transmission_allowed()) {
        radio_force_ptt_off();
        return false;
    }
    return true;
}

void radio_force_ptt_off(void)
{
    gpio_put(PTT_PIN, ptt_inactive_level());
    ptt_active = false;
}

void radio_ptt_off(void)
{
    audio_stop();
    if (ptt_active) {
        sleep_ms(PTT_TAIL_MS);
    }
    gpio_put(PTT_PIN, ptt_inactive_level());
    ptt_active = false;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
}

void radio_pause_ms(unsigned milliseconds)
{
    audio_stop();
    if (milliseconds >= PTT_RELEASE_PAUSE_MS ||
        station_control_stop_requested() || web_reboot_requested()) {
        radio_ptt_off();
    }
    absolute_time_t deadline = make_timeout_time_ms(milliseconds);
    do {
        const absolute_time_t command_start = get_absolute_time();
        dtmf_poll();
        // A command temporarily owns the radio. Its courtesy tone and payload
        // must not consume the interrupted sequence's remaining pause.
        deadline = delayed_by_us(deadline,
            absolute_time_diff_us(command_start, get_absolute_time()));
        if (station_control_stop_requested()) {
            fox_settings_t settings;
            settings_get(&settings);
            station_control_begin_stop_id();
            workflow_set(WORKFLOW_STOP_ID);
            morse_transmit(settings.station_id);
            radio_ptt_off();
            station_control_complete_stop();
            workflow_set(WORKFLOW_STOPPED);
            break;
        }
        // Complete a requested stop/ID before unwinding the old sequence.
        // A reset pending while stopped must not make the idle loop spin.
        if (fox_restart_pending() && station_control_is_enabled()) {
            break;
        }

        const bool settings_saved = settings_save_if_dirty();
        if (settings_saved && web_reboot_requested()) {
            // Give lwIP time to deliver the confirmation page before restarting.
            sleep_ms(1000);
            fox_settings_t settings;
            settings_get(&settings);
            station_control_set_enabled(true);
            workflow_set(WORKFLOW_REBOOT_ID);
            morse_transmit(settings.station_id);
            radio_ptt_off();
            watchdog_reboot(0, 0, 0);
            while (true) {
                tight_loop_contents();
            }
        }

        if (time_reached(deadline)) {
            break;
        }
        const int64_t remaining_ms =
            absolute_time_diff_us(get_absolute_time(), deadline) / 1000;
        sleep_ms(remaining_ms < 100 ? (uint32_t)remaining_ms : 100u);
    } while (true);
}
