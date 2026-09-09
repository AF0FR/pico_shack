#include "morse.h"

#include <stdbool.h>
#include <ctype.h>
#include <stddef.h>

#include "pico/stdlib.h"

#include "audio.h"
#include "config.h"
#include "radio.h"
#include "settings.h"
#include "station_control.h"

typedef struct {
    char character;
    const char *pattern;
} morse_entry_t;

static const morse_entry_t morse_table[] = {
    {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},
    {'E', "."},     {'F', "..-."},  {'G', "--."},   {'H', "...."},
    {'I', ".."},    {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
    {'M', "--"},    {'N', "-."},    {'O', "---"},   {'P', ".--."},
    {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
    {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
    {'Y', "-.--"},  {'Z', "--.."},  {'0', "-----"}, {'1', ".----"},
    {'2', "..---"}, {'3', "...--"}, {'4', "....-"}, {'5', "....."},
    {'6', "-...."}, {'7', "--..."}, {'8', "---.."}, {'9', "----."},
    {'/', "-..-."}
};

static const char *lookup(char character)
{
    for (size_t i = 0; i < sizeof(morse_table) / sizeof(morse_table[0]); ++i) {
        if (morse_table[i].character == character) {
            return morse_table[i].pattern;
        }
    }
    return NULL;
}

static void send_character(const char *pattern, uint32_t dit_ms, uint32_t tone_hz)
{
    for (size_t i = 0; pattern[i] != '\0'; ++i) {
        if (!station_control_transmission_allowed()) {
            return;
        }
        const uint32_t element_ms = pattern[i] == '-' ? 3u * dit_ms : dit_ms;
        audio_play_tone(tone_hz, element_ms);

        if (pattern[i + 1] != '\0') {
            sleep_ms(dit_ms);
        }
    }
}

void morse_transmit(const char *text)
{
    fox_settings_t settings;
    settings_get(&settings);
    const uint32_t dit_ms = 1200u / settings.cw_wpm;
    if (!radio_ptt_on()) {
        return;
    }

    bool sent_character = false;
    for (size_t i = 0; text[i] != '\0'; ++i) {
        if (!station_control_transmission_allowed()) {
            break;
        }
        const char character = (char)toupper((unsigned char)text[i]);
        if (character == ' ') {
            if (sent_character) {
                sleep_ms(7u * dit_ms);
                sent_character = false;
            }
            continue;
        }

        const char *pattern = lookup(character);
        if (pattern == NULL) {
            continue;
        }

        if (sent_character) {
            sleep_ms(3u * dit_ms);
        }
        send_character(pattern, dit_ms, settings.cw_tone_hz);
        sent_character = true;
    }

    // The caller decides whether the following pause is long enough to
    // release PTT. The final Morse element has already stopped the audio.
}
