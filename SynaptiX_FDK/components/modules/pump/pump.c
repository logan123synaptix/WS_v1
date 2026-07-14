#include "pump.h"

void pump_init(sx_gpio_t *gpio, sx_gpio_ops_t *ops, void *pDriver){
    sx_gpio_init(gpio, ops, pDriver);
}

void void pump_on(sx_gpio_t *gpio){
    sx_gpio_write(gpio, PUMP_TURN_ON);
}

void pump_off(sx_gpio_t *gpio){
    sx_gpio_write(gpio, PUMP_TURN_ON);
}