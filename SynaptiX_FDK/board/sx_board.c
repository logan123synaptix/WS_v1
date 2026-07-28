#include "sx_board.h"
#include "stm32h5xx_hal.h"
#include "tusb.h"
#include "usb.h"
#include "sx_user_cdc.h"
#include "sx_user_msc.h"
#include "logger.h"
#include "sx_delay.h"
#include "sx_gpio.h"
#include "app.h"
#include "sx_sleep.h"

static const char *TAG = "BOARD";

Board_t board;


static UART_HandleTypeDef *hal_uart[6] = {&huart1, &huart2, &huart3, &huart4, &huart5, &huart6}; // lte, gps, rs485, dust-sensor, extend-uart, log

static TIM_HandleTypeDef *sx_tim1 = &htim1;

static sx_uart_t *bsp_uart[6];
static uint8_t uart_rx_char[6];

static void set_enter_sleep_mode(void);
static void set_enter_full_mode(void);

/* sx_sleep_t hooks (see components/peripherals/sleep/sx_sleep.h) — this
 * board's plug-in for what needs quiescing before STOP mode / restoring
 * after waking. sx_sleep.c itself has no knowledge of UART/USB. */
static void board_sleep_pre_stop_hook(void *hook_ctx);
static void board_sleep_post_wake_hook(void *hook_ctx);

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

/* SPS30 power enable — see EN_PW_DUST_Pin's doc-comment in sx_board.h for
 * the full opto/MOSFET chain and active-HIGH polarity. Defaults LOW at
 * boot (CubeMX's MX_GPIO_Init resets it low before this file ever runs),
 * so SPS30 stays unpowered until something explicitly drives it HIGH —
 * that on/off sequencing belongs to the SPS30 business-logic layer
 * (not yet written), not to board bring-up, so it is only declared here. */
static sx_gpio_t     s_en_pw_dust;
static sx_gpio_pin_t s_en_pw_dust_pin = {.pin = EN_PW_DUST_Pin, .port = EN_PW_DUST_Port};

/* ZE12A mux select lines (TMUX4052 A0/A1) — see gas_sensor_init() in
 * ze12a.c, which owns and drives these once initialized below. */
static sx_gpio_pin_t s_uart5_s0_pin = {.pin = UART5_S0_Pin, .port = UART5_S0_Port};
static sx_gpio_pin_t s_uart5_s1_pin = {.pin = UART5_S1_Pin, .port = UART5_S1_Port};

/* Pump */
static sx_gpio_t     s_en_pw_pump;
static sx_gpio_pin_t s_en_pw_pump_pin = {.pin = EN_PW_PUMP_Pin, .port = EN_PW_PUMP_Port};

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

    // SPS30 power-enable GPIO — left LOW/unpowered here; the SPS30
    // business-logic layer (not yet written) is responsible for driving
    // it HIGH before talking to the sensor and LOW when done.
    sx_gpio_init(&s_en_pw_dust, &sx_gpio_ops, &s_en_pw_dust_pin);

    // SPS30 (UART_DUST) — sensirion_uart_hal_init() stores this sx_uart_t*
    // as the port every sensirion_uart_hal_tx/rx() call uses; nothing else
    // here needs a bsp_uart[] entry since SPS30 talks through the
    // Sensirion HAL layer, not sx_uart_t directly.
    sx_uart_init(&board.sps30_uart, &uart_config[UART_DUST], 512, 512);
    bsp_uart[UART_DUST] = &board.sps30_uart;
    sensirion_uart_hal_init(&board.sps30_uart);
    HAL_UART_Receive_IT(hal_uart[UART_DUST], &uart_rx_char[UART_DUST], 1);

    // SHT3x — shares I2C1 with the RTC/IMU, no separate UART or GPIO.
    sht3x_init(&board.sht3x, &board.i2c1);

    // ADS1115 - share I2C1 with the RTC/IMU/SHT3X
    ADS1115_Init(&board.ads1115, &board.i2c1, ADS1115_DATA_RATE_250, ADS1115_PGA_TWO);

    // ZE12A (UART_EXTEND) — gas_sensor_init() owns its UART instance and
    // both mux-select GPIOs internally (see ze12a.c). bsp_uart[UART_EXTEND]
    // is fetched via gas_sensor_get_uart() (valid only after init, which
    // just ran above) so HAL_UART_RxCpltCallback() below can route
    // received bytes into it the same way it does for LTE/GPS/LOG/DUST.
    gas_sensor_init(&uart_config[UART_EXTEND], &sx_gpio_ops, &s_uart5_s0_pin, &s_uart5_s1_pin);
    bsp_uart[UART_EXTEND] = gas_sensor_get_uart();
    HAL_UART_Receive_IT(hal_uart[UART_EXTEND], &uart_rx_char[UART_EXTEND], 1);

    // TIMER (TIM1) — sx_timer_init_regs() applies Prescaler/Period directly,
    // no auto-derivation. PSC=31, Period=99 chosen to match tim.c's own
    // CubeMX-configured Prescaler/Period exactly (32MHz/(31+1)=1MHz tick_hz,
    // 1MHz/(99+1)=10kHz interrupt) — same numbers as before, just applied
    // explicitly instead of via sx_timer_init_freq()'s auto-search.
    //
    // sx_timer_init_freq() was tried here first but is the WRONG tool for
    // this board: it always picks the smallest Prescaler (highest tick_hz)
    // that fits freq_hz's ARR in 16 bits — for freq_hz=10000 at 32MHz input
    // that's PSC=0, i.e. tick_hz=32MHz (the full undivided input clock).
    // sx_pwm_sw derives ITS OWN period_ticks from this tick_hz for a PWM
    // period in ms (see sx_pwm_sw.c's _period_ms_to_ticks()) and applies
    // that as a 16-bit ARR via sx_timer_start_ticks() — at 32MHz tick_hz,
    // period_ms=10 -> period_ticks=320000, which silently truncates when
    // written to TIM1's 16-bit ARR register (real bug, would have made the
    // pump run at completely the wrong PWM frequency). At 1MHz tick_hz,
    // period_ms=10 -> period_ticks=10000, comfortably within 16 bits (max
    // representable period_ms at 1MHz is 65.535ms — plenty of headroom).
    sx_timer_init_regs(&board.sx_timer, &sx_timer_ops, sx_tim1,
                        32000000U, 31U, 99U, NULL, NULL);

    // Pump init — owns s_en_pw_pump's GPIO init and board.sx_pwm_sw's PWM
    // struct init (period_ms=10 -> 100Hz PWM / 1% duty resolution at this
    // timer's 1MHz tick_hz, see sx_pump.c). Wire the timer's callback to
    // drive this PWM struct now that it's a valid init'd target.
    pump_init(&board.sx_pwm_sw, &s_en_pw_pump, &sx_gpio_ops, &s_en_pw_pump_pin,
              &board.sx_timer);
    board.sx_timer.callback = sx_pwm_software_tick_cb;
    board.sx_timer.arg = &board.sx_pwm_sw;

    // Board bring-up: run the pump at 40% duty from boot for timer/PWM
    // hardware verification. Not app-cycle-driven yet — app.c's
    // APP_CYCLE_ON_PUMP state below owns steady-state pump control via
    // pump_set_power() (at network_config_t's pump_duty_percent) once the
    // sensing cycle starts, and pump_off() to stop it (sx_sleep_manager.c
    // also calls pump_off() on its own sleep step).
    pump_set_power(&board.sx_pwm_sw, 40U);

    // Tier-1 generic sleep driver — this board's pre_stop/post_wake hooks
    // (defined below) are what actually know about LTE (USART1) + GPS
    // (USART2) UARTs and USB; sx_sleep.c itself has no such knowledge.
    sx_sleep_init(&board.sleep, &sx_sleep_ops, NULL,
                  board_sleep_pre_stop_hook, board_sleep_post_wake_hook, NULL);
}

static void sx_lte_uart_abort(void) {
    HAL_UART_Abort(hal_uart[UART_LTE]);
}

static void sx_gps_uart_abort(){
    HAL_UART_Abort(hal_uart[UART_GPS]);
}

static void sx_dust_uart_abort(void) {
    HAL_UART_Abort(hal_uart[UART_DUST]);
}

static void sx_extend_uart_abort(void) {
    HAL_UART_Abort(hal_uart[UART_EXTEND]);
}

/* Called by sx_sleep.c's _enter_stop() right before actually entering STOP
 * mode. This board's own knowledge of what needs quiescing: LTE (USART1),
 * GPS (USART2), SPS30/dust (UART4), ZE12A/extend (UART5), plus USB. Moved
 * here (out of components/peripherals/sleep) since that module must not
 * know these peripherals exist — a board without USB/these UARTs would
 * just supply a different hook, without touching sx_sleep.c at all.
 *
 * UART_DUST/UART_EXTEND are aborted here the same way as LTE/GPS —
 * per user decision, treat all four UARTs identically regardless of
 * SPS30 already having its own EN_PW_DUST power-cut (sx_sleep_manager's
 * sps30_power_off sleep_step runs earlier, before this hook) — aborting
 * here is still cheap and avoids a stale pending byte/IRQ either way. */
static void board_sleep_pre_stop_hook(void *hook_ctx)
{
    (void)hook_ctx;

    sx_lte_uart_abort();
    sx_gps_uart_abort();
    sx_dust_uart_abort();
    sx_extend_uart_abort();
    __HAL_UART_CLEAR_IT(&huart1, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_CLEAR_IT(&huart2, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_CLEAR_IT(&huart4, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    __HAL_UART_CLEAR_IT(&huart5, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_PEF);
    HAL_NVIC_ClearPendingIRQ(USART1_IRQn);
    HAL_NVIC_ClearPendingIRQ(USART2_IRQn);
    HAL_NVIC_ClearPendingIRQ(UART4_IRQn);
    HAL_NVIC_ClearPendingIRQ(UART5_IRQn);

    HAL_NVIC_DisableIRQ(USB_DRD_FS_IRQn);
    HAL_NVIC_ClearPendingIRQ(USB_DRD_FS_IRQn);
}

/* Mirror of board_sleep_pre_stop_hook(), called right after waking +
 * SystemClock_Config() + tick resume. Only USB needs restoring here — LTE
 * and GPS UARTs are re-armed later, on demand, via
 * board_sim_uart_resume_it()/board_gps_uart_resume_it() (called from the
 * app-layer wake sequencing), not unconditionally on every wake. */
static void board_sleep_post_wake_hook(void *hook_ctx)
{
    (void)hook_ctx;

    HAL_NVIC_EnableIRQ(USB_DRD_FS_IRQn);
}

void gps_it_handle(){
    HAL_UART_Receive_IT(hal_uart[UART_GPS], &uart_rx_char[UART_GPS], 1);
}

void sim_it_handle(){
    HAL_UART_Receive_IT(hal_uart[UART_LTE], &uart_rx_char[UART_LTE], 1);
}

void dust_it_handle(){
    HAL_UART_Receive_IT(hal_uart[UART_DUST], &uart_rx_char[UART_DUST], 1);
}

void extend_it_handle(){
    HAL_UART_Receive_IT(hal_uart[UART_EXTEND], &uart_rx_char[UART_EXTEND], 1);
}

void board_gps_uart_resume_it(void){
    sx_gps_uart_abort();
    gps_it_handle();
}

void board_sim_uart_resume_it(void){
    sx_lte_uart_abort();
    sim_it_handle();
}

/* Same "re-arm on demand" style as GPS/LTE above, not called
 * unconditionally from post_wake_hook() — sx_sleep_manager's wake_steps
 * call these when SPS30/ZE12A are actually needed again (mirrors GPS/
 * modem's board_gps_uart_resume_it()/board_sim_uart_resume_it() pattern). */
void board_dust_uart_resume_it(void){
    sx_dust_uart_abort();
    dust_it_handle();
}

void board_extend_uart_resume_it(void){
    sx_extend_uart_abort();
    extend_it_handle();
}

/* Getters for board-owned GPIOs that app-layer modules (sps30_app.h,
 * sx_sleep_manager.h) need a pointer to but must not own/init themselves
 * — s_en_pw_dust/s_en_pw_pump stay static to this file, already
 * sx_gpio_init()'d/pump_init()'d in sx_board_init() above; these just
 * expose the address. */
sx_gpio_t *sx_board_get_sps30_power_gpio(void)
{
    return &s_en_pw_dust;
}

sx_gpio_t *sx_board_get_pump_gpio(void)
{
    return &s_en_pw_pump;
}

/* app.c drives the pump through sx_pump.h's pump_set_power()/pump_off()
 * (pump_on() is defined but no longer called anywhere in the app layer —
 * see app.c's doc-comment point 2), which take a sx_pwm_software_t*
 * (software-PWM driven, see sx_pump.c) rather than the raw sx_gpio_t*
 * returned by sx_board_get_pump_gpio() above — that getter is kept for
 * callers that still only need the bare GPIO (e.g. board-level diagnostics), but app.c
 * needs this one instead. board.sx_pwm_sw is already pump_init()'d in
 * sx_board_init() above (which also owns *_en_pw_pump's sx_gpio_init()),
 * so this just exposes its address, same pattern as the getter above. */
sx_pwm_software_t *sx_board_get_pump_pwm(void)
{
    return &board.sx_pwm_sw;
}
void sx_board_uart_resume_it(void) {
    // sx_sim76_uart_abort();
    // sx_gps_uart_abort();
    // HAL_UART_Receive_IT(hal_uart[UART_LTE], &uart_rx_char[UART_LTE], 1);
    // HAL_UART_Receive_IT(hal_uart[UART_GPS], &uart_rx_char[UART_GPS], 1);
    board_gps_uart_resume_it();
    board_sim_uart_resume_it();
}

/* USB IT CB    */
void tud_mount_cb(void) {
    log_info(TAG, "USB tiny connected");
}

void tud_umount_cb(void) {
    log_info(TAG, "USB tiny disconnected");
}

void tud_suspend_cb(bool remote_wakeup_en) {
    log_info(TAG, "USB tiny suspend");
}

void tud_resume_cb(void) {
    log_info(TAG, "USB tiny resumed");
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    log_info("USB", "CDC line state: dtr=%d rts=%d", dtr, rts);
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
    } else if(huart == hal_uart[UART_DUST]){
        sx_uart_rx_callback(bsp_uart[UART_DUST], &uart_rx_char[UART_DUST], 1);
        HAL_UART_Receive_IT(hal_uart[UART_DUST], &uart_rx_char[UART_DUST], 1);
    } else if(huart == hal_uart[UART_EXTEND]){
        sx_uart_rx_callback(bsp_uart[UART_EXTEND], &uart_rx_char[UART_EXTEND], 1);
        HAL_UART_Receive_IT(hal_uart[UART_EXTEND], &uart_rx_char[UART_EXTEND], 1);
    }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim){
    if(htim->Instance == sx_tim1->Instance){
        sx_timer_irq_handle(&board.sx_timer);
    }
}