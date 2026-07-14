#include "ze12a.h"
#include <string.h>
#include "logger.h"
#include "sx_delay.h"

static const char *TAG = "GAS_SENSOR";

GasSensor_t gas_sensor[GAS_SENSOR_COUNT];

/* Shared UART5 link (through the TMUX4052 mux) and the two GPIOs that
 * select which physical ZE12A module (SR1/SR2/SR3, plus one unused
 * channel) is currently connected to it. See the schematic: UART5_S0/S1
 * map directly to the mux's A0/A1 address inputs; EN is tied to GND on
 * the board, so it is always enabled and is not driven by firmware. */
static sx_uart_t s_comm;
static sx_gpio_t s_mux_s0;   /* mux A0 */
static sx_gpio_t s_mux_s1;   /* mux A1 */
static bool s_initialized;   /* set true once gas_sensor_init() has run */

static uint8_t s_mux_channel;          /* 0..GAS_SENSOR_MUX_CHANNEL_COUNT-1, currently selected */
static uint32_t s_channel_dwell_ms;    /* time spent on current channel so far */

static gas_sensor_rx_state_t s_rx_state;
static uint8_t s_rx_buf[9];
static uint8_t s_rx_count;             /* bytes collected so far into s_rx_buf */

/* Checksum per ZE12A datasheet ("Communication Protocol"): two's
 * complement of the sum of bytes 1..7 (i.e. all bytes between the leading
 * 0xFF start byte and the trailing check byte, inclusive of both ends
 * excluded). Verified against the worked example in the datasheet:
 * bytes {0x04,0x04,0x00,0x00,0x00,0x30,0xD4} sum to 0x0C; two's
 * complement (~0x0C)+1 = 0xF4, matching the datasheet's own worked
 * example check byte 0xF4. */
static uint8_t ze12a_checksum(const uint8_t *frame)
{
    uint8_t sum = 0;
    for (uint8_t i = 1; i <= 7; i++) {
        sum += frame[i];
    }
    return (uint8_t)(~sum + 1);
}

static void ze12a_select_mux_channel(uint8_t channel)
{
    /* channel is a 2-bit mux address: bit0 -> S0/A0, bit1 -> S1/A1. */
    sx_gpio_write(&s_mux_s0, (channel & 0x01U) ? SX_GPIO_HIGH : SX_GPIO_LOW);
    sx_gpio_write(&s_mux_s1, (channel & 0x02U) ? SX_GPIO_HIGH : SX_GPIO_LOW);
}

static void ze12a_send_command(uint8_t *cmd, uint8_t len){
    sx_uart_write(&s_comm, cmd, len);
    sx_delay_ms(20);
}

void gas_sensor_init(sx_uart_config_t *_uart_cfg, sx_gpio_ops_t *_gpio_ops,
                     void *_s0_arg, void *_s1_arg)
{
    GasSensorType_t sensor_types[GAS_SENSOR_COUNT] = {
        GAS_SENSOR_CO,
        GAS_SENSOR_SO2,
        GAS_SENSOR_NO2,
        GAS_SENSOR_O3,
        GAS_SENSOR_H2S
    };

    for (uint8_t i = 0; i < GAS_SENSOR_COUNT; i++) {
        gas_sensor[i].type = sensor_types[i];
        gas_sensor[i].isConnected = false;
        gas_sensor[i].value = 0;
        gas_sensor[i].unitPPB = 0;
        gas_sensor[i].timeout = 0;
        memset(&gas_sensor[i].Frame, 0, sizeof(gas_sensor[i].Frame));
    }

    sx_uart_init(&s_comm, _uart_cfg, sizeof(s_rx_buf) * 4, sizeof(s_rx_buf) * 4);

    sx_gpio_init(&s_mux_s0, _gpio_ops, _s0_arg);
    sx_gpio_init(&s_mux_s1, _gpio_ops, _s1_arg);

    s_mux_channel = 0;
    s_channel_dwell_ms = 0;
    ze12a_select_mux_channel(s_mux_channel);

    s_rx_state = GAS_SENSOR_RX_WAIT_START;
    s_rx_count = 0;

    s_initialized = true;

    log_debug(TAG, "Initializing");
}

sx_uart_t *gas_sensor_get_uart(void)
{
    return s_initialized ? &s_comm : NULL;
}

/* Parses one complete 9-byte frame already sitting in s_rx_buf, verifies
 * its checksum, and if valid, matches it against gas_sensor[] by the Gas
 * code byte (buffer[1]) - not by which mux channel it arrived on. This
 * mirrors the reference driver: any module can report any gas type, and
 * the firmware self-identifies each reading from the frame content
 * itself rather than assuming a fixed channel-to-gas mapping. */
static bool ze12a_handle_frame(const uint8_t *frame)
{
    uint8_t crc = ze12a_checksum(frame);
    if (crc != frame[8]) {
        log_warn(TAG, "ZE12A checksum mismatch on mux ch %u", s_mux_channel);
        return false;
    }

    GasSensorType_t type = (GasSensorType_t)frame[1];
    uint16_t value = (uint16_t)((frame[4] << 8) | frame[5]);

    for (uint8_t i = 0; i < GAS_SENSOR_COUNT; i++) {
        if (gas_sensor[i].type == type) {
            gas_sensor[i].isConnected = true;
            gas_sensor[i].unitPPB = frame[2];
            gas_sensor[i].value = value;
            gas_sensor[i].timeout = GAS_SENSOR_TIMEOUT_MS;
            log_info(TAG, "ZE12A type=0x%02X value=%u unit=0x%02X (mux ch %u)",
                     type, value, frame[2], s_mux_channel);
            return true;
        }
    }

    log_warn(TAG, "ZE12A frame with unknown gas code 0x%02X", frame[1]);
    return false;
}

void gas_sensor_poll(uint32_t time_stamp_ms)
{
    /* Non-blocking byte assembly: pull whatever bytes are already queued
     * this tick, same approach as gps_process(). Never wait here - if
     * fewer than 9 bytes are available, just resume next tick.
     *
     * advance_channel mirrors the reference driver's `res == 0` condition:
     * gas_sensor_process() in WS_v0 returns 0 (success) only once a full
     * 9-byte frame has been collected AND its checksum is valid AND its
     * gas code matched a known sensor type. Any other outcome (no frame
     * yet, bad checksum, unknown gas code) leaves res == -1 and the mux
     * stays on the current channel until the 2s ceiling is hit. We mirror
     * that here: only a fully valid, matched frame triggers an early
     * channel switch; a checksum failure or unknown code does not. */
    bool advance_channel = false;
    uint8_t byte;
    while (sx_uart_available(&s_comm) > 0) {
        if (1 != sx_uart_read(&s_comm, &byte, 1, 0)) {
            break;
        }

        switch (s_rx_state) {
        case GAS_SENSOR_RX_WAIT_START:
            if (byte == 0xFFU) {
                s_rx_buf[0] = byte;
                s_rx_count = 1;
                s_rx_state = GAS_SENSOR_RX_COLLECTING;
            }
            /* Any other byte while waiting for start is discarded. */
            break;

        case GAS_SENSOR_RX_COLLECTING:
            s_rx_buf[s_rx_count++] = byte;
            if (s_rx_count >= sizeof(s_rx_buf)) {
                if (ze12a_handle_frame(s_rx_buf)) {
                    advance_channel = true;
                }
                s_rx_state = GAS_SENSOR_RX_WAIT_START;
                s_rx_count = 0;
            }
            break;

        default:
            s_rx_state = GAS_SENSOR_RX_WAIT_START;
            s_rx_count = 0;
            break;
        }
    }

    /* Round-robin the mux to the next channel either as soon as we've
     * successfully read one valid frame from the current channel (fast
     * path, matches WS_v0's res==0 early-advance), or once the 2s dwell
     * ceiling is reached regardless of success (ensures we never get
     * stuck on a silent/disconnected channel). */
    s_channel_dwell_ms += time_stamp_ms;
    if (advance_channel || s_channel_dwell_ms >= GAS_SENSOR_CHANNEL_DWELL_MS) {
        s_channel_dwell_ms = 0;
        s_mux_channel = (uint8_t)((s_mux_channel + 1U) % GAS_SENSOR_MUX_CHANNEL_COUNT);
        ze12a_select_mux_channel(s_mux_channel);
        /* Switching channels mid-frame would corrupt whatever partial
         * frame was being collected from the previous module (different
         * physical sensor, unrelated byte stream); discard it. */
        s_rx_state = GAS_SENSOR_RX_WAIT_START;
        s_rx_count = 0;
        sx_uart_flush(&s_comm);
    }

    /* Age out any gas type that hasn't produced a valid frame recently,
     * regardless of which mux channel it last came from. */
    for (uint8_t i = 0; i < GAS_SENSOR_COUNT; i++) {
        if (gas_sensor[i].timeout > time_stamp_ms) {
            gas_sensor[i].timeout -= time_stamp_ms;
        } else {
            gas_sensor[i].timeout = 0;
            gas_sensor[i].isConnected = false;
        }
    }
}

int gas_sensor_read_value(GasSensor_t *sensor, uint16_t *value)
{
    *value = sensor->value;
    return 0;
}

void gas_sensor_start_calibration(GasSensor_t *sensor, bool isZeroCalib)
{
    /* ZE12A calibration (zero-point / span) is not documented in the
     * datasheet section available to this driver (only the active-upload
     * read protocol is specified) - left unimplemented until calibration
     * command bytes are confirmed. */
    (void)sensor;
    (void)isZeroCalib;
}

void gas_sensor_switch_to_qa_mode(void){
    ze12a_send_command(CMD_SWITCH_TO_QA, sizeof(CMD_SWITCH_TO_QA));
    log_info(TAG, "Send cmd switch to QA mode");
}

void gas_sensor_request_reading(void){
    ze12a_send_command(CMD_READ_GAS, sizeof(CMD_READ_GAS));
}

