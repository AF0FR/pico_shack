#include "web_settings.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lwip/apps/httpd.h"
#include "lwip/pbuf.h"

#include "settings.h"

#define POST_BODY_MAX 1024u

static volatile uint8_t save_result;
static volatile settings_validation_t save_validation = SETTINGS_VALID;
static void *post_connection;
static char post_body[POST_BODY_MAX];
static size_t post_body_length;
static bool post_overflow;

static const char *parameter_value(int count, char *names[], char *values[],
                                   const char *wanted)
{
    for (int i = 0; i < count; ++i) {
        if (strcmp(names[i], wanted) == 0) return values[i];
    }
    return NULL;
}

static void copy_text(char *destination, size_t destination_size,
                      const char *source, bool uppercase)
{
    size_t i = 0;
    for (; source[i] != '\0' && i + 1u < destination_size; ++i) {
        destination[i] = uppercase ?
            (char)toupper((unsigned char)source[i]) : source[i];
    }
    destination[i] = '\0';
}

static void update_u16(uint16_t *field, const char *name, int count,
                       char *names[], char *values[])
{
    const char *value = parameter_value(count, names, values, name);
    if (value != NULL) *field = (uint16_t)strtoul(value, NULL, 10);
}

static const char *save_handler(int count, char *names[], char *values[])
{
    const char *return_page = parameter_value(count, names, values, "return");
    const char *complete_page = return_page != NULL && strcmp(return_page, "cw") == 0 ?
        "/cw-apply-complete.html" :
        return_page != NULL && strcmp(return_page, "device") == 0 ?
        "/device-apply-complete.html" : "/apply-complete.html";
    fox_settings_t settings;
    settings_get(&settings);

    const char *station_id = parameter_value(count, names, values, "id");
    if (station_id != NULL) {
        copy_text(settings.station_id, sizeof(settings.station_id), station_id, true);
    }
    const char *ssid = parameter_value(count, names, values, "ssid");
    if (ssid != NULL) {
        copy_text(settings.wifi_ssid, sizeof(settings.wifi_ssid), ssid, false);
    }
    const char *password = parameter_value(count, names, values, "password");
    if (password != NULL && password[0] != '\0') {
        copy_text(settings.wifi_password, sizeof(settings.wifi_password), password, false);
    }
    const char *mode = parameter_value(count, names, values, "mode");
    if (mode != NULL) settings.operating_mode = (uint8_t)strtoul(mode, NULL, 10);
    const char *startup = parameter_value(count, names, values, "startup");
    if (startup != NULL) settings.startup_feature = (uint8_t)strtoul(startup, NULL, 10);
    const char *keyer_mode = parameter_value(count, names, values, "keymode");
    if (keyer_mode != NULL) settings.keyer_mode = (uint8_t)strtoul(keyer_mode, NULL, 10);
    if (parameter_value(count, names, values, "revp") != NULL) {
        settings.keyer_reversed = parameter_value(count, names, values, "rev") != NULL;
    }
    update_u16(&settings.keyer_hang_ms, "hang", count, names, values);

    update_u16(&settings.cw_wpm, "wpm", count, names, values);
    update_u16(&settings.cw_tone_hz, "cw", count, names, values);
    uint16_t gain = settings.audio_gain_percent;
    update_u16(&gain, "gain", count, names, values);
    settings.audio_gain_percent = gain <= UINT8_MAX ? (uint8_t)gain : UINT8_MAX;
    update_u16(&settings.warble_low_hz, "wl", count, names, values);
    update_u16(&settings.warble_high_hz, "wh", count, names, values);
    update_u16(&settings.warble_switch_ms, "ws", count, names, values);
    update_u16(&settings.warble_duration_ms, "wd", count, names, values);
    update_u16(&settings.sweep_low_hz, "sl", count, names, values);
    update_u16(&settings.sweep_high_hz, "sh", count, names, values);
    update_u16(&settings.sweep_step_hz, "ss", count, names, values);
    update_u16(&settings.sweep_step_ms, "sm", count, names, values);
    update_u16(&settings.sweep_duration_ms, "sd", count, names, values);
    update_u16(&settings.fox_pause_ms, "fp", count, names, values);
    update_u16(&settings.tone_pause_ms, "tp", count, names, values);
    update_u16(&settings.idle_ms, "idle", count, names, values);

    save_validation = settings_validate(&settings);
    if (save_validation != SETTINGS_VALID || !settings_set(&settings)) {
        save_result = 2u;
        return complete_page;
    }
    save_result = 1u;
    return complete_page;
}

const char *web_settings_save_handler(int index, int count,
                                      char *names[], char *values[])
{
    (void)index;
    return save_handler(count, names, values);
}

const char *web_settings_defaults_handler(int index, int count,
                                          char *names[], char *values[])
{
    (void)index;
    const char *confirm = parameter_value(count, names, values, "confirm");
    if (confirm == NULL || strcmp(confirm, "1") != 0) {
        save_result = 2u;
        save_validation = SETTINGS_ERROR_FLAGS;
        return "/settings.shtml";
    }
    settings_restore_defaults();
    save_result = 3u;
    save_validation = SETTINGS_VALID;
    return "/settings.shtml";
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void url_decode(char *text)
{
    char *read = text;
    char *write = text;
    while (*read != '\0') {
        if (*read == '+') {
            *write++ = ' ';
            ++read;
        } else if (*read == '%' && read[1] != '\0' && read[2] != '\0') {
            const int high = hex_value(read[1]);
            const int low = hex_value(read[2]);
            if (high >= 0 && low >= 0) {
                *write++ = (char)((high << 4) | low);
                read += 3;
            } else {
                *write++ = *read++;
            }
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

static int parse_form(char *body, char *names[], char *values[], int maximum)
{
    int count = 0;
    char *field = body;
    while (*field != '\0' && count < maximum) {
        char *next = strchr(field, '&');
        if (next != NULL) *next = '\0';
        char *equals = strchr(field, '=');
        if (equals != NULL) {
            *equals = '\0';
            names[count] = field;
            values[count] = equals + 1;
            url_decode(names[count]);
            url_decode(values[count]);
            ++count;
        }
        if (next == NULL) break;
        field = next + 1;
    }
    return count;
}

err_t httpd_post_begin(void *connection, const char *uri,
                       const char *http_request, u16_t http_request_len,
                       int content_len, char *response_uri,
                       u16_t response_uri_len, u8_t *post_auto_wnd)
{
    (void)http_request;
    (void)http_request_len;
    if (strcmp(uri, "/save.cgi") != 0 || content_len <= 0 ||
        content_len >= (int)sizeof(post_body) || post_connection != NULL) {
        snprintf(response_uri, response_uri_len, "/settings.shtml");
        save_result = 2u;
        return ERR_ARG;
    }
    post_connection = connection;
    post_body_length = 0u;
    post_overflow = false;
    *post_auto_wnd = 1u;
    return ERR_OK;
}

err_t httpd_post_receive_data(void *connection, struct pbuf *p)
{
    if (connection != post_connection ||
        post_body_length + p->tot_len >= sizeof(post_body)) {
        post_overflow = true;
        pbuf_free(p);
        return ERR_BUF;
    }
    pbuf_copy_partial(p, post_body + post_body_length, p->tot_len, 0u);
    post_body_length += p->tot_len;
    pbuf_free(p);
    return ERR_OK;
}

void httpd_post_finished(void *connection, char *response_uri,
                         u16_t response_uri_len)
{
    const char *result_uri = "/apply-complete.html";
    if (connection == post_connection && !post_overflow) {
        post_body[post_body_length] = '\0';
        char *names[LWIP_HTTPD_MAX_CGI_PARAMETERS];
        char *values[LWIP_HTTPD_MAX_CGI_PARAMETERS];
        const int count = parse_form(post_body, names, values,
                                     LWIP_HTTPD_MAX_CGI_PARAMETERS);
        result_uri = save_handler(count, names, values);
    } else {
        save_result = 2u;
    }
    snprintf(response_uri, response_uri_len, "%s", result_uri);
    post_connection = NULL;
    post_body_length = 0u;
    post_overflow = false;
}

const char *web_settings_save_class(void)
{
    return (save_result == 1u || save_result == 3u) ? "success" :
           (save_result == 2u ? "error" : "none");
}

const char *web_settings_save_message(void)
{
    if (save_result == 3u) {
        return "Factory defaults restored and queued for flash storage. Wi-Fi defaults take effect after reboot.";
    }
    return save_result == 0u ? "" : settings_validation_message(save_validation);
}
