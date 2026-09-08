#include "web.h"

#include <stdio.h>
#include <string.h>

#include "dhcpserver.h"
#include "dnsserver.h"
#include "lwip/apps/httpd.h"
#include "lwip/apps/mdns.h"
#include "lwip/ip4_addr.h"
#include "pico/cyw43_arch.h"

#include "config.h"
#include "keyer.h"
#include "settings.h"
#include "station_control.h"
#include "web_settings.h"
#include "workflow.h"

static dhcp_server_t dhcp_server;
static dns_server_t dns_server;
static volatile bool reboot_requested;

static void mdns_http_txt(struct mdns_service *service, void *txt_userdata)
{
    (void)txt_userdata;
    mdns_resp_add_service_txtitem(service, "path=/", 6);
}

static const char *parameter_value(int count, char *names[], char *values[],
                                   const char *wanted)
{
    for (int i = 0; i < count; ++i) {
        if (strcmp(names[i], wanted) == 0) {
            return values[i];
        }
    }
    return NULL;
}

static const char *reboot_handler(int index, int count, char *names[], char *values[])
{
    (void)index;
    const char *request = parameter_value(count, names, values, "request");
    if (request == NULL || strcmp(request, "1") != 0) {
        return "/settings.shtml?reboot=invalid";
    }
    reboot_requested = true;
    return "/reboot.shtml";
}

static const char *station_handler(int index, int count, char *names[], char *values[])
{
    (void)index;
    fox_settings_t settings;
    settings_get(&settings);
    const char *run = parameter_value(count, names, values, "run");
    const bool enabled = run != NULL && strcmp(run, "1") == 0;
    if (!enabled) keyer_release_web_keys();
    settings.transmit_enabled = enabled;
    if (settings_set(&settings)) {
        station_control_set_enabled(enabled);
    }
    return "/index.shtml";
}

static const char *mode_handler(int index, int count, char *names[], char *values[])
{
    (void)index;
    const char *mode = parameter_value(count, names, values, "mode");
    if (mode != NULL && (strcmp(mode, "0") == 0 || strcmp(mode, "1") == 0 ||
                         strcmp(mode, "2") == 0)) {
        fox_settings_t settings;
        settings_get(&settings);
        settings.operating_mode = (uint8_t)(mode[0] - '0');
        settings_set(&settings);
        station_control_set_manual_mode(settings.operating_mode == 1u);
        keyer_release_web_keys();
    }
    return "/key-state.txt";
}

static const char *key_handler(int index, int count, char *names[], char *values[])
{
    (void)index;
    const char *key = parameter_value(count, names, values, "key");
    const char *down = parameter_value(count, names, values, "down");
    if (key != NULL && down != NULL) {
        keyer_set_web_key(strcmp(key, "dah") == 0, strcmp(down, "1") == 0);
    }
    return "/key-state.txt";
}

static const tCGI cgi_handlers[] = {
    {"/mode.cgi", mode_handler},
    {"/key.cgi", key_handler},
    {"/save.cgi", web_settings_save_handler},
    {"/reboot.cgi", reboot_handler},
    {"/station.cgi", station_handler},
    {"/defaults.cgi", web_settings_defaults_handler},
};

static const char *ssi_tags[] = {
    "id", "wpm", "cw", "gain", "wl", "wh", "ws", "wd",
    "sl", "sh", "ss", "sm", "sd", "fp", "tp", "idle", "ssid",
    "txstatus", "startdis", "stopdis", "step", "stepid", "savecls", "savemsg",
    "did", "dssid", "dpass", "dwpm", "dcw", "dgain", "dwl", "dwh",
    "dws", "dwd", "dsl", "dsh", "dss", "dsm", "dsd", "dfp", "dtp", "didle"
    , "mode", "keymode", "rev", "hang", "modename", "startup"
};

static u16_t ssi_handler(int index, char *output, int output_length)
{
    fox_settings_t s;
    fox_settings_t d;
    settings_get(&s);
    settings_get_defaults(&d);

    switch (index) {
        case 0: return (u16_t)snprintf(output, output_length, "%s", s.station_id);
        case 1: return (u16_t)snprintf(output, output_length, "%u", s.cw_wpm);
        case 2: return (u16_t)snprintf(output, output_length, "%u", s.cw_tone_hz);
        case 3: return (u16_t)snprintf(output, output_length, "%u", s.audio_gain_percent);
        case 4: return (u16_t)snprintf(output, output_length, "%u", s.warble_low_hz);
        case 5: return (u16_t)snprintf(output, output_length, "%u", s.warble_high_hz);
        case 6: return (u16_t)snprintf(output, output_length, "%u", s.warble_switch_ms);
        case 7: return (u16_t)snprintf(output, output_length, "%u", s.warble_duration_ms);
        case 8: return (u16_t)snprintf(output, output_length, "%u", s.sweep_low_hz);
        case 9: return (u16_t)snprintf(output, output_length, "%u", s.sweep_high_hz);
        case 10: return (u16_t)snprintf(output, output_length, "%u", s.sweep_step_hz);
        case 11: return (u16_t)snprintf(output, output_length, "%u", s.sweep_step_ms);
        case 12: return (u16_t)snprintf(output, output_length, "%u", s.sweep_duration_ms);
        case 13: return (u16_t)snprintf(output, output_length, "%u", s.fox_pause_ms);
        case 14: return (u16_t)snprintf(output, output_length, "%u", s.tone_pause_ms);
        case 15: return (u16_t)snprintf(output, output_length, "%u", s.idle_ms);
        case 16: return (u16_t)snprintf(output, output_length, "%s", s.wifi_ssid);
        case 17: return (u16_t)snprintf(output, output_length, "%s",
                                        station_control_stop_requested() ? "Stopping" :
                                        (station_control_is_enabled() ? "Running" : "Stopped"));
        case 18: return (u16_t)snprintf(output, output_length, "%s",
                                        station_control_is_enabled() ? "disabled" : "");
        case 19: return (u16_t)snprintf(output, output_length, "%s",
                                        station_control_is_enabled() &&
                                        !station_control_stop_requested() ? "" : "disabled");
        case 20: return (u16_t)snprintf(output, output_length, "%s",
                                        workflow_name(workflow_get()));
        case 21: return (u16_t)snprintf(output, output_length, "%u",
                                        (unsigned)workflow_get());
        case 22: return (u16_t)snprintf(output, output_length, "%s",
                                        web_settings_save_class());
        case 23: return (u16_t)snprintf(output, output_length, "%s",
                                        web_settings_save_message());
        case 24: return (u16_t)snprintf(output, output_length, "%s", d.station_id);
        case 25: return (u16_t)snprintf(output, output_length, "%s", d.wifi_ssid);
        case 26: return (u16_t)snprintf(output, output_length, "%s", d.wifi_password);
        case 27: return (u16_t)snprintf(output, output_length, "%u", d.cw_wpm);
        case 28: return (u16_t)snprintf(output, output_length, "%u", d.cw_tone_hz);
        case 29: return (u16_t)snprintf(output, output_length, "%u", d.audio_gain_percent);
        case 30: return (u16_t)snprintf(output, output_length, "%u", d.warble_low_hz);
        case 31: return (u16_t)snprintf(output, output_length, "%u", d.warble_high_hz);
        case 32: return (u16_t)snprintf(output, output_length, "%u", d.warble_switch_ms);
        case 33: return (u16_t)snprintf(output, output_length, "%u", d.warble_duration_ms);
        case 34: return (u16_t)snprintf(output, output_length, "%u", d.sweep_low_hz);
        case 35: return (u16_t)snprintf(output, output_length, "%u", d.sweep_high_hz);
        case 36: return (u16_t)snprintf(output, output_length, "%u", d.sweep_step_hz);
        case 37: return (u16_t)snprintf(output, output_length, "%u", d.sweep_step_ms);
        case 38: return (u16_t)snprintf(output, output_length, "%u", d.sweep_duration_ms);
        case 39: return (u16_t)snprintf(output, output_length, "%u", d.fox_pause_ms);
        case 40: return (u16_t)snprintf(output, output_length, "%u", d.tone_pause_ms);
        case 41: return (u16_t)snprintf(output, output_length, "%u", d.idle_ms);
        case 42: return (u16_t)snprintf(output, output_length, "%u", s.operating_mode);
        case 43: return (u16_t)snprintf(output, output_length, "%u", s.keyer_mode);
        case 44: return (u16_t)snprintf(output, output_length, "%s", s.keyer_reversed ? "checked" : "");
        case 45: return (u16_t)snprintf(output, output_length, "%u", s.keyer_hang_ms);
        case 46: return (u16_t)snprintf(output, output_length, "%s",
                                        s.operating_mode == 0u ? "PicoFox" :
                                        s.operating_mode == 1u ? "PicoCW" : "Standby");
        case 47: return (u16_t)snprintf(output, output_length, "%u", s.startup_feature);
        default: return 0;
    }
}

bool web_init(void)
{
    fox_settings_t settings;
    settings_get(&settings);

    if (cyw43_arch_init() != 0) {
        return false;
    }

    cyw43_arch_enable_ap_mode(settings.wifi_ssid, settings.wifi_password,
                              CYW43_AUTH_WPA2_AES_PSK);

    ip4_addr_t gateway;
    ip4_addr_t netmask;
    gateway.addr = PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);
    netmask.addr = PP_HTONL(CYW43_DEFAULT_IP_MASK);

    cyw43_arch_lwip_begin();
    dhcp_server_init(&dhcp_server, &cyw43_state.netif[CYW43_ITF_AP],
                     &gateway, &netmask);
    dns_server_init(&dns_server, &cyw43_state.netif[CYW43_ITF_AP], &gateway);
    mdns_resp_init();
    mdns_resp_add_netif(&cyw43_state.netif[CYW43_ITF_AP], "picoshack");
    mdns_resp_add_service(&cyw43_state.netif[CYW43_ITF_AP], "PicoShack", "_http",
                          DNSSD_PROTO_TCP, 80, mdns_http_txt, NULL);
    httpd_init();
    http_set_cgi_handlers(cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));
    http_set_ssi_handler(ssi_handler, ssi_tags, LWIP_ARRAYSIZE(ssi_tags));
    cyw43_arch_lwip_end();
    return true;
}

bool web_reboot_requested(void)
{
    return reboot_requested;
}
