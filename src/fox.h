#pragma once

#include <stdbool.h>

void fox_run_cycle(void);

// Starts again at the first MO stage. If once is true, the station stops
// after completing that sequence.
void fox_request_sequence(bool once);
bool fox_restart_pending(void);
