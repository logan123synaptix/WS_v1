#ifndef SX_BOARD_H
#define SX_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "main.h"
#include "usart.h"
#include "gpio.h"
#include "app_config.h"
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
#include "sx_sleep.h"
// #include "adc.h"
#include "spi.h"
#include "i2c.h"
#include "sx_ex_rtc.h"
#include "bno055.h"
#include "sx_filter.h"
#include "sx_read_bat.h"
#include "sensirion_uart_hal.h"
#include "sht3x.h"
#include "ze12a.h"
#include "ads1115.h"
#include "sx_pump.h"

typedef struct {
    volatile uint32_t raw_adc;
    volatile float v_adc;
    volatile float v_bat;
}voltage_t;

typedef struct Board{
    /* Concrete driver instance + the modem_ops_t handle service/app layers
     * actually use (sx_mqtt.c, sx_user_mqtt.c, sx_sleep_service.c — see
     * modem_ops.h). Swapping to a different modem module later means
     * replacing a7677s below with the new driver's type and pointing
     * modem.ops/modem.ctx at it — nothing outside this file changes. */
    a7677s_t        a7677s;
    modem_handle_t  modem;
    sx_gps_t gps;
    sx_uart_t log_uart;
    #if USE_READ_PIN
    voltage_t voltage;
    #endif
    sx_usb_tiny_t usb;
    sx_W25Q128_t  q128;
    sx_usb_tiny_config_t usb_cfg;
    sx_storage_cfg_t    storage_cfg;
    sx_i2c_t            i2c1;
    rx8130ce_t          rtc;
    sx_i2c_t    rtc_i2c;
    bno055_t    imu;
    #if USE_READ_PIN
    sx_adc_reader_t s_adc_reader;
    #endif
    /* SPS30 (UART_DUST): the Sensirion HAL layer (sensirion_uart_hal.c)
     * only needs a UartDescr = sx_uart_t* to talk over, so a concrete
     * sx_uart_t instance is owned here and its address passed to
     * sensirion_uart_hal_init(). See sensirion_uart_portdescriptor.h. */
    sx_uart_t   sps30_uart;
    SHT3X_T     sht3x;
    /* ZE12A (gas_sensor / UART_EXTEND): its UART instance and mux-select
     * GPIOs are owned as private statics inside ze12a.c (s_comm,
     * s_mux_s0, s_mux_s1) — gas_sensor_init() takes the raw config/pin
     * descriptors below and wires them up internally, so no field is
     * needed here. */
    /* ADS1115 (shared I2C1, no dedicated GPIO — no ALERT/RDY pin wired
     * per this board's .ioc, so this driver only ever polls the OS bit,
     * never uses the comparator/alert function).
     *
     * AIN1 = adc_current (current-sense input, via INA180A1 U2, gain
     *   20V/V per TI's Device Comparison table — SOT23-5, pinout A).
     *   IN+/IN- sense the voltage drop across shunt resistor R16 (in
     *   series on the +12V rail, before the rest of the board); OUT
     *   feeds AIN1 through R9 (0R per schematic — a series link/isolation
     *   resistor, not the shunt itself).
     *   *** OPEN QUESTION, UNRESOLVED as of this commit — do not assume
     *   an answer: R16's actual resistance value has not been confirmed.
     *   User initially said "0 ohm" for this discussion, but Vout = Gain
     *   x I x R_shunt means R_shunt=0 would force AIN1's reading to 0V
     *   for any current — i.e. the current-sense channel would never
     *   read anything. This is very likely a mix-up with R9 (which
     *   legitimately is 0R on the schematic, but sits on the output
     *   side, not the shunt). User said they'd confirm this in a future
     *   conversation. Until R16's real value is confirmed:
     *     - PGA left at ADS1115_PGA_TWO as a placeholder only, matching
     *       AIN2 for now — NOT derived from any confirmed R16 value.
     *     - Treat every AIN1/adc_current reading as unverified — do not
     *       trust it for any real current measurement or safety logic
     *       (e.g. overcurrent cutoff) until this is resolved.
     *
     * AIN2 = adc_vol (voltage-sense input, 12V rail through a resistor
     *   divider: R14=100K top / R15=20K bottom per schematic, confirmed
     *   with user — the divider node feeds AIN2 directly).
     *   Vout = Vin * 20k/(100k+20k) = Vin/6 -> 2.0V at 12V nominal,
     *   2.5V at a 15V worst-case rail — comfortably under both this
     *   board's VDD (3.3V) and the ADS1115's absolute maximum analog
     *   input rating of VDD+0.3V=3.6V (Documents/ads1115.pdf, Absolute
     *   Maximum Ratings table). ADS1115_PGA_TWO (FSR +-2.048V) matches
     *   this range well. */
    ADS1115_T   ads1115;

    /* Tier-1 generic sleep/STOP-mode driver (components/peripherals/sleep).
     * Knows nothing about UART/USB/any peripheral — board_sleep_pre_stop_hook()
     * and board_sleep_post_wake_hook() (sx_board.c) are this board's plug-in
     * for whatever needs quiescing/restoring around STOP mode, wired in via
     * sx_sleep_init() in sx_board_init(). */
    sx_sleep_t  sleep;
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

/*  SPS30 power enable (EN_PW_DUST, per schematic)
 *  Active-HIGH: MCU drives this HIGH -> opto OP3's internal LED conducts
 *  (12V side) -> its output phototransistor turns on -> forms a divider
 *  with R11/R19 that pulls G1 of the AO4828 N-channel MOSFET high enough
 *  (VG > VS) -> MOSFET conducts D1(+5V) -> S1 -> feeds +5V to the SPS30
 *  (connector SENSOR pin 4) through D4/C20/C25 filtering. MCU LOW (also
 *  the default/idle state) -> opto off -> R19 pulls G1 back down -> MOSFET
 *  off -> SPS30 unpowered. Confirmed against the schematic, not assumed. */
#define EN_PW_DUST_Port         GPIOA
#define EN_PW_DUST_Pin          GPIO_PIN_9

/*PUMP PIN*/
#define EN_PW_PUMP_Port         GPIOA
#define EN_PW_PUMP_Pin          GPIO_PIN_8

/*  ZE12A mux select (TMUX4052 A0/A1) — see ze12a.c for the channel
 *  truth table (A1=0,A0=0 -> SR1; 0,1 -> SR2; 1,0 -> SR3; 1,1 -> unused).
 *  Pin assignment per CubeMX-generated Core/Inc/main.h. */
#define UART5_S0_Port           GPIOA
#define UART5_S0_Pin            GPIO_PIN_7
#define UART5_S1_Port           GPIOC
#define UART5_S1_Pin            GPIO_PIN_4

/* USB */
void dcd_fs_msp_init(uint8_t rhport);
void USB_DRD_FS_IRQHandler(void);

/* Board */
void sx_board_init(void);
void read_vol_pin(uint32_t time_stamp);
void gps_it_handle(void);
void sim_it_handle(void);
void dust_it_handle(void);
void extend_it_handle(void);
void board_gps_uart_resume_it(void);
void board_sim_uart_resume_it(void);
void board_dust_uart_resume_it(void);
void board_extend_uart_resume_it(void);

void sx_board_uart_abort(void);
void sx_board_uart_resume_it(void);
void check_charge(void);

sx_gpio_t *sx_board_get_sps30_power_gpio(void);
sx_gpio_t *sx_board_get_pump_gpio(void);

extern Board_t board;

#ifdef __cplusplus
}
#endif

#endif // SX_BOARD_H