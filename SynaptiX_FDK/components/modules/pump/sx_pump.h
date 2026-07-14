#ifndef PUMP_H
#define PUMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sx_board.h"
#include "sx_gpio.h"

#define PUMP_TURN_ON    SX_GPIO_HIGH
#define PUMP_TURN_OFF   SX_GPIO_LOW

void pump_init(sx_gpio_t *gpio, sx_gpio_ops_t *ops, void *pDriver);

void pump_on(sx_gpio_t *gpio);

void pump_off(sx_gpio_t *gpio);

#ifdef __cplusplus
}
#endif

#endif