#include "sx_board.h"
#include "stm32h5xx_hal.h"
#include "tusb.h"
#include "usb.h"
#include "sx_user_cdc.h"
#include "sx_user_msc.h"
#include "logger.h"
#include "sx_delay.h"
#include "sx_gpio.h"
#include "app_config.h"
#include "app.h"
#include "sx_sleep.h"

static const char *TAG = "BOARD";

Board_t board;


static UART_HandleTypeDef *hal_uart[6] = {&huart1, &huart2, &huart3, &huart4, &huart5, &huart6}; // lte, gps, rs485, dust-sensor, extend-uart, log
static sx_uart_t *bsp_uart[6];
static uint8_t uart_rx_char[6];

static void set_enter_sleep_mode(void);
static void set_enter_full_mode(void);

void dcd_fs_msp_init(uint8_t rhport)
{
    (void)rhport;
    log_info(TAG, "dcd_fs_msp_init called");
    hpcd_USB_DRD_FS.Instance = USB_DRD_FS;
    HAL_PCD_MspInit(&hpcd_USB_DRD_FS);
    HAL_Delay(100);
    log_info(TAG, "MSP init done!");
}

void USB_DRD_FS_IRQHandler(void)
{
    tud_int_handler(0);
}

/* ------------------------------------------------------------------ */
/*  GPIO Define                                                       */
/* ------------------------------------------------------------------ */
static sx_gpio_pin_t s_lte_pwrkey_pin = {.pin = LTE_PWRKEY_Pin, .port = LTE_PWRKEY_Port};
/* LTE_RESET_Pin (GPIOD PIN_11) — per schematic, drives Q2's base through
 * R17 (open-collector style switch to GND on the module's own RESET pin).
 * MCU HIGH -> Q2 on -> module's RESET pin pulled low (native active-low
 * reset). MCU LOW (default/idle, R19 pulls Q2's base down, CubeMX also
 * inits this pin LOW at boot) -> Q2 off -> module's RESET pin floats high
 * via its internal VBAT pull-up -> normal operation. Used only by
 * a7677s_hard_reset() (modem_ops_t.hard_reset) as a last-resort escalation
 * when the normal PWRKEY power_off_start()/power_on_start() cycle has
 * itself failed to recover the modem, or it has stopped responding to AT
 * entirely — not a routine recovery path. See a7677s_t.resetPin's
 * doc-comment in a7677s.h for the full timing/state-machine details. */
static sx_gpio_pin_t s_lte_reset_pin = {.pin = LTE_RESET_Pin, .port = LTE_RESET_Port};

/* No VBAT-cutoff transistor on this board revision for the modem — see
 * a7677s_t.hasPowerPin (set to 0 below). A future board revision may add
 * one back, hence the field stays in modem_t/a7677s_t rather than being
 * removed outright (see modem.h's own comment on hasPowerPin/powerPin). */

static sx_gpio_pin_t s_gps_cpw_pin = {.pin = GPS_CPW_Pin, .port = GPS_CPW_Port};
static sx_gpio_pin_t s_gps_rst_pin = {.pin = GPS_RST_Pin, .port = GPS_RST_Port};
/* GPS_PPS_Pin is wired but not used by this firmware yet (no 1PPS capture
 * configured) — no sx_gpio_pin_t declared for it, nothing to pass around
 * until that feature is implemented. */

static sx_gpio_pin_t s_spi_cs_pin = {.pin = SPI_CS_Pin, .port = SPI_CS_Port};

/* RTC and IMU share the same physical reset line (I2C1_RESET_Pin) per the
 * schematic — one wire, not two independent pins. RTC does not use it at
 * all (rx8130ce_init() resets itself via an I2C register sequence per the
 * datasheet, see sx_ex_rtc.c's _software_reset()); only IMU takes this GPIO
 * as its reset_gpio. Kept as a single sx_gpio_t rather than one per
 * consumer, since writing it from two independent handles for what is one
 * physical wire invites the two drivers stepping on each other. */
static sx_gpio_t     s_i2c1_reset;
static sx_gpio_pin_t s_i2c1_reset_pin = {.pin = I2C1_RESET_Pin, .port = I2C1_RESET_Port};

/* RS485_RDE_Pin is wired (see sx_board.h) but not yet driven by any init
 * call in this file — left undeclared here rather than adding an unused
 * sx_gpio_t/sx_gpio_pin_t pair for a pin nothing reads or writes yet. */

static void spi_storage_init(void){
    board.storage_cfg.cs_pin = s_spi_cs_pin;
    board.storage_cfg.hspi = &hspi1;

    sx_storage_init(&board.storage_cfg);
}

// USART define

/* ------------------------------------------------------------------ */
/*  Board                                                               */
/* ------------------------------------------------------------------ */

static void log_print(const char *str)
{
    sx_uart_write(&board.log_uart, (const uint8_t *)str, strlen(str));
}

void sx_board_init(void)
{
    // Initialize Logger
    static sx_uart_config_t uart_config[6];
    uart_config[UART_LOG].pDriver = hal_uart[UART_LOG];
    uart_config[UART_LOG].baudrate = 115200;
    uart_config[UART_LOG].bits = 8;
    uart_config[UART_LOG].parity = 0;
    uart_config[UART_LOG].stopbits = 1;

    bsp_uart[UART_LOG] = &board.log_uart;

    sx_uart_init(&board.log_uart, &uart_config[UART_LOG], 512, 512);
    logger_init(LOGGER_INFO, log_print);
    log_info(TAG, "Board init start");
    
    /*  USB  */ 
    #if BOARD_USE_MSC
    sx_user_msc_init();
    sx_user_msc_create_disk(USER_DISK_LABEL_CREATE);
    log_info(TAG, "MSC disk created");
    #endif
    board.usb_cfg.rx_buf_size = 256;
    board.usb_cfg.tx_buf_size = 256;
    dcd_fs_msp_init(0);
    sx_usb_tiny_init(&board.usb, &board.usb_cfg);
    log_info(TAG, "USB init done");

    uart_config[UART_LTE].pDriver = hal_uart[UART_LTE];
    uart_config[UART_LTE].baudrate = 115200;
    uart_config[UART_LTE].bits = 8;
    uart_config[UART_LTE].parity = 0;
    uart_config[UART_LTE].stopbits = 1;

    uart_config[UART_GPS].pDriver = hal_uart[UART_GPS];
    uart_config[UART_GPS].baudrate = 9600;
    uart_config[UART_GPS].bits = 8;
    uart_config[UART_GPS].parity = 0;
    uart_config[UART_GPS].stopbits = 1;

    uart_config[UART_RS485].pDriver = hal_uart[UART_RS485];
    uart_config[UART_RS485].baudrate = 115200;
    uart_config[UART_RS485].bits = 8;
    uart_config[UART_RS485].parity = 0;
    uart_config[UART_RS485].stopbits = 1;

    uart_config[UART_DUST].pDriver = hal_uart[UART_DUST];
    uart_config[UART_DUST].baudrate = 115200;
    uart_config[UART_DUST].bits = 8;
    uart_config[UART_DUST].parity = 0;
    uart_config[UART_DUST].stopbits = 1;

    uart_config[UART_EXTEND].pDriver = hal_uart[UART_EXTEND];
    uart_config[UART_EXTEND].baudrate = 9600;
    uart_config[UART_EXTEND].bits = 8;
    uart_config[UART_EXTEND].parity = 0;
    uart_config[UART_EXTEND].stopbits = 1;

    uart_config[UART_LOG].pDriver = hal_uart[UART_LOG];
    uart_config[UART_LOG].baudrate = 115200;
    uart_config[UART_LOG].bits = 8;
    uart_config[UART_LOG].parity = 0;
    uart_config[UART_LOG].stopbits = 1;

    bsp_uart[UART_LTE] = &board.a7677s.base.uart;
    bsp_uart[UART_GPS] = &board.gps.comm;

    // I2C
    sx_i2c_init(&board.i2c1, &sx_i2c_ops, &hi2c1);

    /* Shared I2C1 reset line — see s_i2c1_reset's own comment above. Only
     * initialized once here; RTC does not take it (self-resets via I2C),
     * only IMU below does. */
    sx_gpio_init(&s_i2c1_reset, &sx_gpio_ops, &s_i2c1_reset_pin);

    // RTC — no power-cutoff GPIO on this board (VIO/VDD/VBAT wired
    // directly to 3.3V), see sx_ex_rtc.c/.h.
    rx8130ce_init(&board.rtc, &board.i2c1);
    // IMU reset uses the shared s_i2c1_reset initialized above.

    // Initialize LTE (A7677S)
    a7677s_init(&board.a7677s);
    board.a7677s.base.hasPowerPin = 0;   /* no VBAT-cutoff transistor on this board */
    sx_gpio_init(&board.a7677s.base.pwrPin, &sx_gpio_ops, &s_lte_pwrkey_pin);
    sx_gpio_init(&board.a7677s.resetPin, &sx_gpio_ops, &s_lte_reset_pin);
    a7677s_set_full_apn(&board.a7677s, APN, USERNAME_APN, PASSWORD_APN);
    sx_uart_init(&board.a7677s.base.uart, &uart_config[UART_LTE], 512, 512);
    HAL_UART_Receive_IT(hal_uart[UART_LTE], &uart_rx_char[UART_LTE], 1);

    board.modem.ops = &a7677s_ops;
    board.modem.ctx = &board.a7677s;

    /* Only kick off power-on here — power_on_start() is asynchronous
     * (advances through a7677s_power_state_t over several poll() ticks).
     * start() is NOT called here: it would be rejected immediately since
     * power_state isn't READY yet. The app layer (sx_user_mqtt.c's
     * _common_init()) is what actually calls start(), via
     * set_on_ready()'s callback firing once the modem is truly ready — see
     * modem_ops.h. This keeps board init limited to bringing up hardware,
     * not driving application-level sequencing. */
    board.modem.ops->power_on_start(board.modem.ctx);

    // Initialize GPS
    gps_init(&board.gps, &uart_config[UART_GPS], &sx_gpio_ops, &s_gps_cpw_pin, &s_gps_rst_pin);
    HAL_UART_Receive_IT(hal_uart[UART_GPS], &uart_rx_char[UART_GPS], 1);

    spi_storage_init();
    bno055_init(&board.imu, &board.i2c1, BNO055_I2C_ADDR_DEFAULT, &s_i2c1_reset);
}

static void sx_lte_uart_abort(void) {
    HAL_UART_Abort(hal_uart[UART_LTE]);
}

static void sx_gps_uart_abort(){
    HAL_UART_Abort(hal_uart[UART_GPS]);
}

void gps_it_handle(){
    HAL_UART_Receive_IT(hal_uart[UART_GPS], &uart_rx_char[UART_GPS], 1);
}

void sim_it_handle(){
    HAL_UART_Receive_IT(hal_uart[UART_LTE], &uart_rx_char[UART_LTE], 1);
}

void board_gps_uart_resume_it(void){
    sx_gps_uart_abort();
    gps_it_handle();
}

void board_sim_uart_resume_it(void){
    sx_lte_uart_abort();
    sim_it_handle();
}
void sx_board_uart_resume_it(void) {
    // sx_sim76_uart_abort();
    // sx_gps_uart_abort();
    // HAL_UART_Receive_IT(hal_uart[UART_LTE], &uart_rx_char[UART_LTE], 1);
    // HAL_UART_Receive_IT(hal_uart[UART_GPS], &uart_rx_char[UART_GPS], 1);
    board_gps_uart_resume_it();
    board_sim_uart_resume_it();
}

// static void set_enter_sleep_mode(void) {
//     sx_gpio_write(&s_dis_charge, SX_GPIO_HIGH); 
//     sx_gpio_write(&s_charge, SX_GPIO_LOW);
//     log_info(TAG, "Enter sleep POWER");
// }

/* USB IT CB    */
void tud_mount_cb(void) {
    log_info(TAG, "USB tiny connected");
}

void tud_umount_cb(void) {
    log_info(TAG,"USB discharge");
    app_request_sleep();
    log_info(TAG, "USB tiny disconnected");
}

void tud_suspend_cb(bool remote_wakeup_en) {

}

void tud_resume_cb(void) {
    log_info(TAG, "USB tiny resumed");
}


void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if(huart == hal_uart[UART_LTE]){
        sx_uart_rx_callback(bsp_uart[UART_LTE], &uart_rx_char[UART_LTE], 1);
        HAL_UART_Receive_IT(hal_uart[UART_LTE], &uart_rx_char[UART_LTE], 1);
    } else if(huart == hal_uart[UART_GPS]){
        sx_uart_rx_callback(bsp_uart[UART_GPS], &uart_rx_char[UART_GPS], 1);
        HAL_UART_Receive_IT(hal_uart[UART_GPS], &uart_rx_char[UART_GPS], 1);
    } else if(huart == hal_uart[UART_LOG]){
        sx_uart_rx_callback(bsp_uart[UART_LOG], &uart_rx_char[UART_LOG], 1);
        HAL_UART_Receive_IT(hal_uart[UART_LOG], &uart_rx_char[UART_LOG], 1);
    }
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    log_info("USB", "CDC line state: dtr=%d rts=%d", dtr, rts);
}