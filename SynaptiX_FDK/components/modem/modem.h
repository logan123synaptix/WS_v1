#ifndef MODEM_H
#define MODEM_H
#ifdef __cplusplus
extern "C" {
#endif

#include "sx_uart.h"
#include "sx_gpio.h"
#include "cqueue.h"
#include <string.h>

#define MODEM_RX_BUFFER_SIZE 512

typedef struct modem modem_t;

typedef enum modem_response_st{
    MODEM_RESPONSE_SUCCESS = 0,
    MODEM_RESPONSE_FAIL,
    MODEM_RESPONSE_TIMEOUT
}modem_response_st_t;

typedef void (*modem_command_response_callback_t)(modem_t *modem, const char *response, modem_response_st_t res, void *arg);

typedef struct modem_command{
    const char *cmd;
    const char *res_success;
    const char *res_fail;
    modem_command_response_callback_t callback;
    void *arg; // callback
}modem_command_t;

struct modem
{
    /* data */
    char buff[MODEM_RX_BUFFER_SIZE];
    uint32_t buff_id;
    sx_uart_t uart;
    sx_gpio_t pwrPin;        /* PWRKEY line — every modem driver has this */
    sx_gpio_t powerPin;      /* VBAT cutoff transistor GPIO — optional,
                              * depends on board revision. Only valid to use
                              * when hasPowerPin is 1. */
    uint8_t hasPowerPin;     /* 1 if this board wires a VBAT cutoff for this
                              * modem, 0 otherwise. Must be explicitly set by
                              * the board init code (sx_board.c), never
                              * assumed. Drivers must check this flag before
                              * touching powerPin. */
    uint8_t isBusy;
    uint8_t isReady;
    uint32_t timeOut;
    uint32_t waitElapsed;    /* elapsed time accumulator for the current
                              * command timeout, tracked per-instance.
                              * Replaces the old "static uint32_t s_time"
                              * local in modem_poll(), which was unsafe with
                              * more than one modem instance. */
    uint32_t resID;
    modem_command_t *cmd;
};

void modem_init(modem_t *modem);
void modem_poll(modem_t *modem,uint32_t timeStamp);

//int modem_send_command(modem_t *modem, modem_command_t *cmd, char *response, int response_size,modem_command_response_callback_t callback,uint32_t timeout);
int modem_send_command(modem_t *modem, modem_command_t *cmd, uint32_t timeout);

static inline uint8_t modem_is_busy(modem_t *modem){
    return modem->isBusy;
}

static inline uint8_t modem_is_ready(modem_t *modem){
    return modem->isReady;
}

#ifdef __cplusplus
}
#endif
#endif // MODEM_H