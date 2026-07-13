#ifndef SX_BOARD_H
#define SX_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "main.h"
#include "usart.h"
#include "gpio.h"
#include "sx_spi.h"
#include "sx_gpio.h"
#include "sx_uart.h"
#include "gps.h"
#include "a7677s.h"
#include "modem_ops.h"
#include "sx_delay.h"
#include "sx_W25Q128.h"
#include "sx_ex_storage.h"
#include "sx_usb_tiny_cdc.h"
#include "adc.h"
#include "spi.h"
#include "i2c.h"
#include "sx_ex_rtc.h"
#include "bno055.h"
#include "sx_filter.h"
#include "sx_read_bat.h"

typedef struct {
    volatile uint32_t raw_adc;
    volatile float v_adc;
    volatile float v_bat;
}voltage_t;

typedef struct Board{
    /* Concrete driver instance + the modem_ops_t handle service/app layers
     * actually use (sx_mqtt.c, sx_user_mqtt.c, sx_sleep_manager.c — see
     * modem_ops.h). Swapping to a different modem module later means
     * replacing a7677s below with the new driver's type and pointing
     * modem.ops/modem.ctx at it — nothing outside this file changes. */
    a7677s_t        a7677s;
    modem_handle_t  modem;
    sx_gps_t gps;
    sx_uart_t log_uart;
    voltage_t voltage;
    sx_usb_tiny_t usb;
    sx_W25Q128_t  q128;
    sx_usb_tiny_config_t usb_cfg;
    sx_storage_cfg_t    storage_cfg;
    sx_i2c_t            i2c1;
    rx8130ce_t          rtc;
    sx_i2c_t    rtc_i2c;
    bno055_t    imu;
    sx_adc_reader_t s_adc_reader;
}Board_t;

typedef enum{
    UART_LTE = 0,
    UART_GPS,
    UART_RS485,
    UART_DUST,
    UART_EXTEND, // gas sensor ZE12A
    UART_LOG
}BSP_NUM_UART;

#define NUM_UART                6

/*  MODE USB    */
#define BOARD_USE_CDC           1
#define BOARD_USE_MSC           1

/*  LTE GPIO    */   
#define LTE_PWRKEY_Port         GPIOD
#define LTE_PWRKEY_Pin          GPIO_PIN_12
#define LTE_RESET_Port          GPIOD
#define LTE_RESET_Pin           GPIO_PIN_11

/*  GPS GPIO    */
#define GPS_PPS_Port            GPIOC
#define GPS_PPS_Pin             GPIO_PIN_11
#define GPS_CPW_Port            GPIOC
#define GPS_CPW_Pin             GPIO_PIN_12
#define GPS_RST_Port            GPIOC
#define GPS_RST_Pin             GPIO_PIN_13

/*  I2C PIN    */
#define I2C1_RESET_Port         GPIOB
#define I2C1_RESET_Pin          GPIO_PIN_8

/*  SPI */
#define SPI_CS_Port             GPIOC
#define SPI_CS_Pin              GPIO_PIN_12

/*  RS485 DE    */
#define RS485_RDE_Port          GPIOB
#define RS485_RDE_Pin           GPIO_PIN_2

/* USB */
void dcd_fs_msp_init(uint8_t rhport);
void USB_DRD_FS_IRQHandler(void);

/* Board */
void sx_board_init(void);
void read_vol_pin(uint32_t time_stamp);
void gps_it_handle(void);
void sim_it_handle(void);
void board_gps_uart_resume_it(void);
void board_sim_uart_resume_it(void);

inline void sx_delay(uint32_t time_delay_ms){HAL_Delay(time_delay_ms);}
inline uint32_t sx_gettick(void){return HAL_GetTick();}

void sx_board_uart_abort(void);
void sx_board_uart_resume_it(void);
void check_charge(void);

extern Board_t board;

#ifdef __cplusplus
}
#endif

#endif // SX_BOARD_H