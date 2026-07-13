#ifndef SPS30_H
#define SPS30_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stdio.h"
#include "stdint.h"
#include "sx_uart.h"

typedef enum {
    SPS30_START_MEASUREMENT = 0x00, // Reponse time: 20ms
    SPS30_STOP_MEASUREMENT = 0x01,  // Reponse time: 20ms
    SPS30_READ_MEASURED_VALUE = 0x03 ,    // read , response time: 20ms
    SPS30_SLEEP = 0x10, // sleep, reponse time: 5ms
    SPS30_WAKEUP = 0x11, // wake up
    SPS30_START_FAN_CLEANING = 0x56,
    SPS30_READ_WRITE_AUTO_CLEANING_INTERVAL = 0x80,
    SPS30_DEVICE_INFO =  0xD0,
    SPS30_READ_VERSION = 0xD1,
    SPS30_READ_DEVICE_STATUS_REG = 0xD2,
    SPS30_RESET = 0xD3
}SPS30_CMD_T;

typedef enum{
    SPS30_ERR_TIMEOUT,
    SPS30_ERR_INIT,
    SPS30_ERR_WRITE,
    SPS30_ERR_READ,
    SPS30_ERR_WRITE_READ,
}SPS30_ERR_T;

typedef struct{
    sx_uart_t *uart;
    
}sps30_handle_t;

SPS30_ERR_T sps30_init(void);

#ifdef __cplusplus
}
#endif

#endif