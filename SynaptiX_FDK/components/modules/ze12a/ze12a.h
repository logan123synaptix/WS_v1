#ifndef GAS_SENSOR_H
#define GAS_SENSOR_H
#ifdef __cplusplus
extern "C" {
#endif

#include "sx_uart.h"
#include "sx_gpio.h"
#include <stdint.h>
#include <stdbool.h>

#define GAS_SENSOR_COUNT 5

/* The TMUX4052 mux physically has 4 selectable channels (2-bit address,
 * A1/A0), even though only 3 physical ZE12A modules (SR1/SR2/SR3) are
 * populated on this board revision per the schematic. Address 3 is simply
 * left unconnected; polling it will just time out with no frame received,
 * which is handled the same as any other disconnected/missing sensor. */
#define GAS_SENSOR_MUX_CHANNEL_COUNT 4

/* How long (ms) to stay on one mux channel waiting for a frame before
 * moving on to the next. ZE12A's default active-upload mode sends a new
 * frame every ~1 second (per datasheet section "Communication
 * Specification"), so 2000ms comfortably covers at least one frame if a
 * module is actually present and responding. */
#define GAS_SENSOR_CHANNEL_DWELL_MS 2000U

/* How long (ms) without a successfully parsed, checksum-valid frame
 * before a given gas type is considered disconnected. */
#define GAS_SENSOR_TIMEOUT_MS 10000U

typedef enum GasSensorType{
    GAS_SENSOR_CO = 0x04,
    GAS_SENSOR_SO2 = 0x2B,
    GAS_SENSOR_NO2 = 0x2C,
    GAS_SENSOR_O3 = 0x2A,
    GAS_SENSOR_H2S =  0x03
}GasSensorType_t;

typedef struct GasSensor{
    GasSensorType_t type;
    struct GasFrame
    {
        /* data */
        uint8_t start;
        uint8_t addr;
        uint8_t command;
        uint8_t payload[4];
        uint8_t crc;
    }Frame;
    uint16_t value;
    bool isConnected;
    uint8_t unitPPB;
    uint32_t timeout;
}GasSensor_t;

/* Non-blocking SHDLC-style byte assembly state for the single shared
 * UART5 link (mirrors the byte-at-a-time approach used in gps.c, since
 * only one physical UART serves all mux channels one at a time). */
typedef enum {
    GAS_SENSOR_RX_WAIT_START = 0,  /* waiting for the 0xFF start byte */
    GAS_SENSOR_RX_COLLECTING,      /* gathering the remaining 8 bytes */
} gas_sensor_rx_state_t;

void gas_sensor_init(sx_uart_config_t *_uart_cfg, sx_gpio_ops_t *_gpio_ops,
                     void *_s0_arg, void *_s1_arg);
void gas_sensor_poll(uint32_t time_stamp_ms);
int gas_sensor_read_value(GasSensor_t *sensor, uint16_t *value);
void gas_sensor_start_calibration(GasSensor_t *sensor, bool isZeroCalib);

extern GasSensor_t gas_sensor[GAS_SENSOR_COUNT];

#ifdef __cplusplus
}
#endif
#endif