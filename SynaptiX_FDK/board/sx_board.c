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
static sx_gpio_t     s_lte_gpio;
static sx_gpio_pin_t s_lte_pwrkey_pin = {.pin = LTE_PWRKEY_Pin, .port = LTE_PWRKEY_Port};
static sx_gpio_pin_t s_lte_rst_pin = {.pin = LTE_RESET_Pin, .port = LTE_RESET_Port};

static sx_gpio_t     s_gps_gpio;
static sx_gpio_pin_t s_gps_power_pin = {.pin = GPS_EN_PW_Pin, .port = GPS_EN_PW_Port};
static sx_gpio_pin_t s_gps_pps_pin = {.pin = GPS_PPS_Pin, .port = GPS_PPS_Port};
static sx_gpio_pin_t s_gps_rst_pin = {.pin = GPS_RST_Pin, .port = GPS_RST_Port};

static sx_gpio_t     s_i2c_rst_gpio;
static sx_gpio_pin_t s_i2c_rst_pin = {.pin = I2C1_RESET_Pin, .port = I2C1_RESET_Port};

static sx_gpio_t     s_rs485_rde_gpio;
static sx_gpio_pin_t s_rs485_rde_pin = {.pin = RS485_RDE_Pin, .port = RS485_RDE_Port};

static sx_gpio_t     s_spi_cs_gpio;
static sx_gpio_pin_t s_spi_cs_pin = {.pin = SPI_CS_Pin, .port = SPI_CS_Port};

static sx_gpio_t     s_rtc_pwr;
static sx_gpio_pin_t s_rtc_pwr_pin = {.pin = NULL, .port = NULL};

/*  SPI */
// static sx_gpio_t s_spi_cs;
// static sx_gpio_t s_spi_pwr;
// static sx_spi_t storage_spi;

static void spi_storage_init(void){
    board.storage_cfg.cs_pin = s_spi_cs_pin;
    board.storage_cfg.pwr_pin = NULL;
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
    uart_config[UART_EXTEND].baudrate = 115200;
    uart_config[UART_EXTEND].bits = 8;
    uart_config[UART_EXTEND].parity = 0;
    uart_config[UART_EXTEND].stopbits = 1;

    uart_config[UART_LOG].pDriver = hal_uart[UART_LOG];
    uart_config[UART_LOG].baudrate = 256000;
    uart_config[UART_LOG].bits = 8;
    uart_config[UART_LOG].parity = 0;
    uart_config[UART_LOG].stopbits = 1;

    bsp_uart[UART_LTE] = &board.sim76xx.base.uart;
    bsp_uart[UART_GPS] = &board.gps.comm;

    // I2C
    sx_i2c_init(&board.i2c1, &sx_i2c_ops, &hi2c1);
    // RTC
    sx_gpio_init(&s_rtc_pwr,   &sx_gpio_ops, &s_rtc_pwr_pin);
    rx8130ce_init(&board.rtc,  &board.i2c1, &s_rtc_pwr);
    // IMU
    sx_gpio_init(&s_imu_reset, &sx_gpio_ops, &s_imu_reset_pin);
    // Initialize LTE
    sx_gpio_init(&board.sim76xx.base.pwrPin, &sx_gpio_ops, &s_lte_pwrkey_pin);
    sx_uart_init(&board.sim76xx.base.uart, &uart_config[UART_LTE], 512, 512);
    sim76xx_init(&board.sim76xx);
    HAL_UART_Receive_IT(hal_uart[UART_LTE], &uart_rx_char[UART_LTE], 1);
    sim76xx_power_on(&board.sim76xx);
    gps_init(&board.gps, &uart_config[UART_GPS], &sx_gpio_ops, &s_gps_power_pin, NULL);
    HAL_UART_Receive_IT(hal_uart[UART_GPS], &uart_rx_char[UART_GPS], 1);
    sim76xx_start(&board.sim76xx);
    // Initialize GPS
    spi_storage_init();
    bno055_power_on(&board.imu);
    sx_gpio_write(&s_imu_en, SX_GPIO_LOW);
    bno055_init(&board.imu, &board.i2c1, BNO055_I2C_ADDR_DEFAULT, &s_imu_en, &s_imu_reset);

    HAL_ADCEx_Calibration_Start(hal_adc, ADC_SINGLE_ENDED);
    HAL_ADC_Start(hal_adc);
    sx_adc_reader_init(&board.s_adc_reader);
}

static void sx_sim76_uart_abort(void) {
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
    sx_sim76_uart_abort();
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

static void set_enter_sleep_mode(void) {
    sx_gpio_write(&s_dis_charge, SX_GPIO_HIGH); 
    sx_gpio_write(&s_charge, SX_GPIO_LOW);
    log_info(TAG, "Enter sleep POWER");
}

// static void set_enter_full_mode(void) {
//     sx_gpio_write(&s_charge, SX_GPIO_HIGH);
//     sx_gpio_write(&s_dis_charge, SX_GPIO_HIGH);
//     log_info(TAG, "Enter full POWER");
// }

void read_vol_pin(uint32_t time_stamp) {
    sx_adc_reader_process(&board.s_adc_reader, hal_adc, time_stamp);
    board.voltage.v_bat = board.s_adc_reader.v_bat_filtered;
}

/* USB IT CB    */
void tud_mount_cb(void) {
    log_info(TAG, "USB tiny connected");
    
    //app_sync_gps_log_to_disk();
    // set_enter_full_mode();
    //app_mode = APP_MODE_FULL_POWER;
    //app_notify_usb_connected();
}

void tud_umount_cb(void) {
    //(void)remote_wakeup_en;
    sx_gpio_write(&s_charge, SX_GPIO_LOW);
    sx_gpio_write(&s_dis_charge, SX_GPIO_HIGH);
    log_info(TAG,"USB discharge");
    app_request_sleep();
    log_info(TAG, "USB tiny disconnected");
    // set_enter_sleep_mode();
    // app_request_sleep();
    
}

void tud_suspend_cb(bool remote_wakeup_en) {
    sx_gpio_write(&s_dis_charge, SX_GPIO_HIGH);
    // (void)remote_wakeup_en;
    sx_gpio_write(&s_charge, SX_GPIO_LOW);
    // log_info(TAG,"USB discharge");
    // app_request_sleep();
    // log_info(TAG, "USB tiny suspended"); 
}

void tud_resume_cb(void) {
    log_info(TAG, "USB tiny resumed");
    // HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, 0);
    // HAL_GPIO_WritePin(GPIOC, GPIO_PIN_6, 1);
    //set_enter_full_mode();
    // app_mode = APP_MODE_FULL_POWER;
    // app_notify_usb_connected();
}

void check_charge(void){
    uint8_t ret = HAL_GPIO_ReadPin(VBUS_PORT, VBUS_PIN);
    (ret == 1)?(sx_gpio_write(&s_charge, SX_GPIO_HIGH)):(sx_gpio_write(&s_charge, SX_GPIO_LOW));
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

void HAL_GPIO_EXTI_Rising_Callback(uint16_t GPIO_Pin)
{
    if(GPIO_Pin == VBUS_PIN){
        
        /* Set EXTI wake reason for sleep manager if MCU is waking from sleep */
        sx_sleep_set_exti_wake();
        
        sx_gpio_write(&s_dis_charge, SX_GPIO_HIGH);
        sx_gpio_write(&s_charge, SX_GPIO_HIGH);

        app_sync_gps_log_to_disk();
        app_mode_full_pw();
        app_notify_usb_connected();
        log_info(TAG,"USB charge");
    }
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin){
    if(GPIO_Pin == VBUS_PIN){
        sx_gpio_write(&s_dis_charge, SX_GPIO_HIGH);
        sx_gpio_write(&s_charge, SX_GPIO_LOW);
        
        app_request_sleep();
        log_info(TAG,"USB discharge");
    }
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    log_info("USB", "CDC line state: dtr=%d rts=%d", dtr, rts);
}