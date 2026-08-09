#include "test_mux_select.h"
#include "sx_gpio.h"
#include "sx_board.h"   /* UART5_S0_Pin/Port, UART5_S1_Pin/Port aliases */
#include "logger.h"

static const char *TAG = "TEST_MUX_SELECT";

/* Same two physical pins ze12a.c's s_mux_s0/s_mux_s1 use -- separate
 * static instances here so this diagnostic never touches ze12a.c's
 * private state, but they resolve to the identical GPIOA_PIN_7 /
 * GPIOC_PIN_4 hardware lines. Do not run this alongside gas_sensor_init()
 * (see header doc-comment). */
static sx_gpio_t     s_test_mux_s0;
static sx_gpio_t     s_test_mux_s1;
static sx_gpio_pin_t s_test_uart5_s0_pin = {.pin = UART5_S0_Pin, .port = UART5_S0_Port};
static sx_gpio_pin_t s_test_uart5_s1_pin = {.pin = UART5_S1_Pin, .port = UART5_S1_Port};
static bool          s_test_mux_initialized;

void select_mux_test_init(void)
{
    sx_gpio_init(&s_test_mux_s0, &sx_gpio_ops, &s_test_uart5_s0_pin);
    sx_gpio_init(&s_test_mux_s1, &sx_gpio_ops, &s_test_uart5_s1_pin);
    s_test_mux_initialized = true;
    log_info(TAG, "select_mux_test_init done -- probe UART5_S0_Pin "
             "(GPIOA pin 7) and UART5_S1_Pin (GPIOC pin 4) at the MCU "
             "with a multimeter after each select_mux_test(channel) call");
}

void select_mux_test(uint8_t channel)
{
    if (!s_test_mux_initialized) {
        log_warn(TAG, "select_mux_test called before select_mux_test_init -- ignoring");
        return;
    }

    /* Identical addressing to ze12a_select_mux_channel() in ze12a.c:
     * bit0 -> S0/A0, bit1 -> S1/A1. Values outside 0-3 are masked to 2
     * bits, same as ze12a.c's implicit behaviour via channel & 0x01 /
     * channel & 0x02 (there is no explicit range check in ze12a.c
     * either). */
    SX_GPIO_VALUE s0_val = (channel & 0x01U) ? SX_GPIO_HIGH : SX_GPIO_LOW;
    SX_GPIO_VALUE s1_val = (channel & 0x02U) ? SX_GPIO_HIGH : SX_GPIO_LOW;

    sx_gpio_write(&s_test_mux_s0, s0_val);
    sx_gpio_write(&s_test_mux_s1, s1_val);

    log_info(TAG, "channel=%u -> S0=%s S1=%s (probe MCU pins now)",
             channel,
             (s0_val == SX_GPIO_HIGH) ? "HIGH" : "LOW",
             (s1_val == SX_GPIO_HIGH) ? "HIGH" : "LOW");
}