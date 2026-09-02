#pragma once

#include <stdbool.h>

void station_control_init(void);
bool station_control_is_enabled(void);
void station_control_set_enabled(bool enabled);
void station_control_set_manual_mode(bool enabled);
bool station_control_transmission_allowed(void);
bool station_control_stop_requested(void);
void station_control_begin_stop_id(void);
void station_control_complete_stop(void);
