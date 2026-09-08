#include "fox.h"

#include <stdio.h>

#include "audio.h"
#include "config.h"
#include "morse.h"
#include "radio.h"
#include "settings.h"
#include "station_control.h"
#include "workflow.h"

static volatile bool restart_requested;
static volatile bool one_sequence_requested;

void fox_request_sequence(bool once)
{
    restart_requested = true;
    one_sequence_requested = once;
}

bool fox_restart_pending(void)
{
    return restart_requested;
}

static void transmit_warble(void)
{
    if (!radio_ptt_on()) {
        return;
    }
    audio_play_warble();
}

static void transmit_sweep(void)
{
    if (!radio_ptt_on()) {
        return;
    }
    audio_play_sweep();
}

static bool picofox_mode_active(void)
{
    fox_settings_t settings;
    settings_get(&settings);
    return settings.operating_mode == 0u;
}

void fox_run_cycle(void)
{
    if (!picofox_mode_active()) return;
    if (!station_control_is_enabled() || !picofox_mode_active()) {
        workflow_set(WORKFLOW_STOPPED);
        radio_pause_ms(250u);
        return;
    }
    const bool run_once = one_sequence_requested;
    one_sequence_requested = false;
    restart_requested = false;
    fox_settings_t settings;
    settings_get(&settings);

    workflow_set(WORKFLOW_FOX_1);
    morse_transmit(settings.fox_identifier);
    workflow_set(WORKFLOW_PAUSE_1);
    radio_pause_ms(settings.fox_pause_ms);
    if (restart_requested) return;
    if (!station_control_is_enabled() || !picofox_mode_active()) {
        workflow_set(WORKFLOW_STOPPED);
        return;
    }

    workflow_set(WORKFLOW_WARBLE);
    transmit_warble();
    workflow_set(WORKFLOW_PAUSE_2);
    radio_pause_ms(settings.tone_pause_ms);
    if (restart_requested) return;
    if (!station_control_is_enabled() || !picofox_mode_active()) {
        workflow_set(WORKFLOW_STOPPED);
        return;
    }

    workflow_set(WORKFLOW_FOX_2);
    morse_transmit(settings.fox_identifier);
    workflow_set(WORKFLOW_PAUSE_3);
    radio_pause_ms(settings.fox_pause_ms);
    if (restart_requested) return;
    if (!station_control_is_enabled() || !picofox_mode_active()) {
        workflow_set(WORKFLOW_STOPPED);
        return;
    }

    workflow_set(WORKFLOW_SWEEP);
    transmit_sweep();
    workflow_set(WORKFLOW_PAUSE_4);
    radio_pause_ms(settings.tone_pause_ms);
    if (restart_requested) return;
    if (!station_control_is_enabled() || !picofox_mode_active()) {
        workflow_set(WORKFLOW_STOPPED);
        return;
    }

    char station_id_twice[(STATION_ID_MAX_LENGTH * 2u) + 2u];
    snprintf(station_id_twice, sizeof(station_id_twice), "%s %s",
             settings.station_id, settings.station_id);
    workflow_set(WORKFLOW_STATION_ID);
    morse_transmit(station_id_twice);
    workflow_set(WORKFLOW_IDLE);
    radio_pause_ms(settings.idle_ms);
    if (restart_requested) return;
    if (run_once) {
        settings_get(&settings);
        settings.transmit_enabled = 0u;
        settings_set(&settings);
        station_control_complete_stop();
        workflow_set(WORKFLOW_STOPPED);
    }
}
