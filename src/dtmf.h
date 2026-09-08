#pragma once

#include <stddef.h>

// Initializes DTMF reception on the conditioned radio-speaker ADC input.
void dtmf_init(void);

// Executes any decoded command in the main context. Safe to call frequently.
void dtmf_poll(void);

// Copies the most recently decoded stable digits for status display.
void dtmf_get_recent(char *destination, size_t destination_size);
