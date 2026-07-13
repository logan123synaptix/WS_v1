/*
 * Platform glue between Sensirion's SHDLC/streaming layer (unmodified,
 * copied from the reference driver) and this project's own non-blocking
 * UART peripheral driver (sx_uart.h).
 *
 * This file replaces v0's sensirion_uart_hal.c, which implemented these
 * same functions using a dedicated FreeRTOS TX task plus busy-wait RX
 * polling via bsp_delay_us(). This project is bare-metal (no RTOS), so
 * that implementation cannot be reused as-is.
 *
 * Design note on blocking behavior:
 * - sx_uart_write() calls HAL_UART_Transmit() directly, which is already
 *   blocking (up to 1000ms internal HAL timeout). sensirion_uart_hal_tx()
 *   therefore also blocks for the (very short, a few dozen bytes at
 *   9600-115200 baud) duration of the frame. This matches the reference
 *   driver's behavior and is acceptable because SPS30 commands are sent
 *   only occasionally (once per second at most in normal operation), not
 *   from a tight polling loop.
 * - sx_uart_read() does NOT block: it returns immediately with however
 *   many bytes are already queued (possibly zero), matching how gps.c
 *   already uses it elsewhere in this project. sensirion_uart_hal_rx()
 *   preserves that non-blocking behavior by doing a single read attempt
 *   and returning immediately. The retry/timeout loop that decides how
 *   long to keep waiting for a full SHDLC response already exists one
 *   layer up, in sensirion_streaming_shdlc.c's sensirion_shdlc_read_
 *   response() (see its "retries" / sensirion_uart_hal_sleep_usec loop) -
 *   this HAL layer must not duplicate that waiting logic, only provide
 *   the raw non-blocking read primitive it polls against.
 */

#include "sensirion_uart_hal.h"
#include "sx_uart.h"
#include "sx_delay.h"

static sx_uart_t *s_port = NULL;

int16_t sensirion_uart_hal_init(UartDescr port) {
    s_port = port;
    return 0;
}

int16_t sensirion_uart_hal_free(void) {
    s_port = NULL;
    return 0;
}

int16_t sensirion_uart_hal_tx(uint16_t data_len, const uint8_t *data) {
    if (s_port == NULL) {
        return -1;
    }
    /* sx_uart_write() has no failure return path in this project's
     * driver (HAL errors are not surfaced past it); if it returns at all,
     * the whole frame was handed to the HAL transmit call. */
    sx_uart_write(s_port, data, (int)data_len);
    return (int16_t)data_len;
}

int16_t sensirion_uart_hal_rx(uint16_t max_data_len, uint8_t *data) {
    if (s_port == NULL) {
        return -1;
    }
    /* Single non-blocking attempt: read whatever is already queued (may
     * be 0 bytes), with a 0ms timeout so sx_uart_read() cannot itself
     * wait. The caller (sensirion_streaming_shdlc.c) is responsible for
     * calling this repeatedly with its own retry/backoff loop. */
    int received = sx_uart_read(s_port, data, (int)max_data_len, 0);
    return (int16_t)received;
}

void sensirion_uart_hal_sleep_usec(uint32_t useconds) {
    /* This project only exposes millisecond-granularity delay. The
     * Sensirion HAL contract explicitly allows this (see
     * sensirion_uart_hal.h: "a <10 millisecond precision is sufficient"),
     * so rounding up to the nearest millisecond is within spec. Round up
     * rather than down so a caller requesting e.g. 100us still gets a
     * non-zero delay instead of being silently skipped. */
    uint32_t ms = (useconds + 999U) / 1000U;
    if (ms > 0U) {
        sx_delay_ms(ms);
    }
}