#pragma once

#include <stddef.h>
#include <stdbool.h>

// Initializes DTMF reception on the conditioned radio-speaker ADC input.
void dtmf_init(void);

// Executes any decoded command in the main context. Safe to call frequently.
void dtmf_poll(void);

// Queues the action represented by a post-prefix DTMF digit (0-7 or #).
bool dtmf_queue_command(char digit);

// Copies the most recently decoded stable digits for status display.
void dtmf_get_recent(char *destination, size_t destination_size);
