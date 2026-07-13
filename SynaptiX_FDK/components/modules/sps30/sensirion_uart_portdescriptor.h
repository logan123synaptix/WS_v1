/*
 * This file replaces Sensirion's original sensirion_uart_portdescriptor.h.
 *
 * The original v0/Sensirion-reference version of this file defines
 * UartDescr as a Linux serial device path (const char*, e.g.
 * "/dev/ttyUSB0"), meant for a POSIX host running the Sensirion Linux UART
 * driver. That has no meaning on bare-metal STM32.
 *
 * On this platform, the "port" the Sensirion HAL layer needs to remember
 * is simply which sx_uart_t instance (already configured and initialized
 * by sx_board.c, e.g. bsp_uart[UART_DUST]) the SPS30 driver should talk
 * to. sensirion_uart_hal_init() stores this pointer; sensirion_uart_hal_tx/
 * rx() use it on every call. See sensirion_uart_hal.c.
 */

#ifndef UART_TYPEDEF_H
#define UART_TYPEDEF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sx_uart.h"

/* Platform-dependent port descriptor: pointer to the sx_uart_t instance
 * wired to the SPS30 (UART_DUST, see sx_board.h/.c). */
typedef sx_uart_t *UartDescr;

#ifdef __cplusplus
}
#endif
#endif