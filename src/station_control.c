#include "station_control.h"

#include "hardware/gpio.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "radio.h"
#include "settings.h"

#define BOOTSEL_POLL_MS       20u
#define BOOTSEL_DEBOUNCE_TICKS 3u

static volatile bool transmit_enabled;
static volatile bool manual_mode;
static volatile bool stop_requested;
static volatile bool stop_id_active;
static struct repeating_timer bootsel_timer;

// BOOTSEL shares the flash chip-select line. This routine must execute from
// SRAM with interrupts disabled while that line is temporarily disconnected.
static bool __no_inline_not_in_flash_func(read_bootsel_button)(void)
{
    const uint cs_pin_index = 1u;
    const uint32_t interrupt_state = save_and_disable_interrupts();

    hw_write_masked(&ioqspi_hw->io[cs_pin_index].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    for (volatile int i = 0; i < 1000; ++i) {
    }

#if PICO_RP2040
    const uint32_t cs_bit = 1u << 1;
#else
    const uint32_t cs_bit = SIO_GPIO_HI_IN_QSPI_CSN_BITS;
#endif
    const bool pressed = !(sio_hw->gpio_hi_in & cs_bit);

    hw_write_masked(&ioqspi_hw->io[cs_pin_index].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    restore_interrupts(interrupt_state);
    return pressed;
}

void station_control_set_enabled(bool enabled)
{
    if (enabled) {
        stop_requested = false;
        stop_id_active = false;
        transmit_enabled = true;
    } else if (transmit_enabled) {
        stop_requested = true;
        radio_force_ptt_off();
    }
}

void station_control_set_manual_mode(bool enabled)
{
    manual_mode = enabled;
}

bool station_control_transmission_allowed(void)
{
    return manual_mode || (transmit_enabled && (!stop_requested || stop_id_active));
}

bool station_control_stop_requested(void)
{
    return stop_requested && !stop_id_active;
}

void station_control_begin_stop_id(void)
{
    stop_id_active = true;
}

void station_control_complete_stop(void)
{
    transmit_enabled = false;
    stop_requested = false;
    stop_id_active = false;
    radio_force_ptt_off();
}

static bool bootsel_poll_callback(struct repeating_timer *timer)
{
    (void)timer;
    static uint8_t pressed_ticks;
    static bool press_handled;
    const bool pressed = read_bootsel_button();

    if (pressed) {
        if (pressed_ticks < BOOTSEL_DEBOUNCE_TICKS) {
            ++pressed_ticks;
        }
        if (pressed_ticks == BOOTSEL_DEBOUNCE_TICKS && !press_handled) {
            press_handled = true;
            const bool enabled = !transmit_enabled;
            station_control_set_enabled(enabled);

            fox_settings_t settings;
            settings_get(&settings);
            settings.transmit_enabled = enabled;
            settings_set(&settings);
        }
    } else {
        pressed_ticks = 0u;
        press_handled = false;
    }
    return true;
}

void station_control_init(void)
{
    fox_settings_t settings;
    settings_get(&settings);
    transmit_enabled = settings.transmit_enabled != 0u;
    manual_mode = settings.operating_mode == 1u;
    stop_requested = false;
    stop_id_active = false;
    add_repeating_timer_ms(-(int32_t)BOOTSEL_POLL_MS, bootsel_poll_callback,
                           NULL, &bootsel_timer);
}

bool station_control_is_enabled(void)
{
    return transmit_enabled;
}
