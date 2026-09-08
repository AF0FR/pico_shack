#include "pico/stdlib.h"

#include "audio.h"
#include "fox.h"
#include "keyer.h"
#include "radio.h"
#include "settings.h"
#include "station_control.h"
#include "web.h"
#include "workflow.h"

int main(void)
{
    stdio_init_all();
    settings_init();
    keyer_init();
    if (!web_init()) {
        while (true) {
            sleep_ms(1000);
        }
    }
    radio_init();
    station_control_init();
    audio_init();

    sleep_ms(2000);

    while (true) {
        fox_settings_t settings;
        settings_get(&settings);
        station_control_set_manual_mode(settings.operating_mode == 1u);
        if (settings.operating_mode == 0u) {
            fox_run_cycle();
        } else if (settings.operating_mode == 1u) {
            keyer_run();
        } else {
            workflow_set(WORKFLOW_STOPPED);
            radio_force_ptt_off();
            settings_save_if_dirty();
            sleep_ms(10);
        }
    }
}
