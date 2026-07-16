#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define USER_DISK_LABEL_CREATE         "VD GPS"   

#define SLEEP_TIME_MS                   5*60*1000     // 5 minutes                 

/* ------------------------------------------------------------------ */
/*  Main app state machine timing (app.c's APP_MODE_FULL_POWER cycle)  */
/* ------------------------------------------------------------------ */
/* NOTE (per discussion with the user): these were fixed compile-time
 * defaults; runtime override is now implemented (2026-07-16) via the USB
 * CDC CLI ("settings -c -pump/-sensing/-data ...", see
 * app/user/cli_commands/), which reads/writes network_config_t's
 * pump_on_ms/sensing_ms/sleep_ms fields (network_config.h) rather than
 * these #defines directly. These 3 values below now only supply
 * network_config's build_defaults() with its fallback for a fresh/empty
 * flash — app.c no longer reads APP_PUMP_ON_MS/APP_SENSING_MS/
 * APP_CYCLE_PERIOD_MS itself, see app.c's doc-comment point 3. USB MSC
 * (editing a config file on the exposed disk) is not wired up — CLI
 * covers this need for now. */
#define APP_CYCLE_PERIOD_MS             SLEEP_TIME_MS  /* how long the board stays in STOP mode per lap (sleep duration itself, NOT the total ON_PUMP->SENSING->SENDING->sleep lap time — the full lap is longer by APP_PUMP_ON_MS+APP_SENSING_MS+publish time). Runtime-configurable is planned (see NOTE above) but not implemented yet — currently a fixed compile-time value. */
#define APP_PUMP_ON_MS                  30000U         /* how long the pump stays on before sensing starts */
#define APP_SENSING_MS                  60000U         /* how long SENSING runs (SPS30 measurement cycle + other sensors settle) */

/*USE MQTT*/
/* NOTE (2026-07-15, per the user): Thingsboard is NOT actually in use yet
 * — no real Thingsboard broker exists for this project to point at. Even
 * with USE_THINGSBOARD==1 here, app.c currently initializes plain MQTT
 * (sx_user_mqtt_nontls_init()/tls_init(), via network_config.h — a new
 * flash-backed, runtime-editable connection-settings module) rather than
 * thingsboard_client_init(). This flag is left at 1 rather than flipped to
 * 0 since thingsboard_client.c's behavior is gated on it and the user may
 * want to swap back once a real broker is available — flip app.c's
 * app_init() back to thingsboard_client_init() at that point, not this
 * flag alone (this flag only controls which app_config.h macros/topics
 * exist, not which client app.c actually calls). */
#define USE_THINGSBOARD 1

#define MQTT_ID                         "1234"
#define MQTT_PORT                       1883//1993
#define MQTT_PUSHLISH_TIME              10000
#define THINGSBOARD_KEEP_ALIVE_S        10
#define TOPIC_DEFAULT_SIZE              256
#define PAYLOAD_DEFAULT_SIZE            256

#if USE_THINGSBOARD
#define ATTRIBUTE_UPDATE_API "synaptix/demo/attributes/"MQTT_ID
#define ATTRIBUTE_REQUEST_API "synaptix/demo/attributes/response/"MQTT_ID
#define ATTRIBUTE_FW_REQUEST_API "v2/fw/request/chunk" // v2/fw/request/${requestId}/chunk/${chunkIndex}
#define TELEMETRY_API "synaptix/demo/telemetry/"MQTT_ID

#define TELEMETRY_IO_API "synaptix/demo/telemetry/"MQTT_ID"/IO/%d"
#define TELEMETRY_4_20mA_API "synaptix/demo/telemetry/"MQTT_ID"/4_20mA/%d"
#define TELEMETRY_THS_API "synaptix/demo/telemetry/"MQTT_ID"/THS/%d"

#define RPC_REQUEST_API "synaptix/demo/request/"MQTT_ID
#define RPC_RESPONSE_API "synaptix/demo/response/"MQTT_ID

#define THINGSBOARD_USER_NAME_SIZE  128
#define THINGSBOARD_PASSWORD_SIZE   128
#define THINGSBOARD_CLIENTID_SIZE   128
#define THINGSBOARD_HOST_SIZE       256
#else
#define MQTT_HOST                       "iot.coreflux.cloud"//"broker.emqx.io"//"test.mosquitto.org"//"103.226.248.21" "broker.hivemq.com"
#define MQTT_CLIENT_ID                  "vin_station_xxx"
#define MQTT_USER                       NULL
#define MQTT_PASS                       NULL
#define MQTT_KEEPALIVE                  60          /* seconds */
#define MQTT_STATION_DATA_TOPIC         "hanoi/air_quality/data/"
#define MQTT_STATION_HEARTBEAT_TOPIC    "vindynamic/tracking/gps/"
#define MQTT_SUB_TOPIC                  "stm32/cmd/#"
#endif

#define APN                             "v-internet"

#define USERNAME_APN                    NULL
#define PASSWORD_APN                    NULL

#define SX_TIME_IN_SLEEP                60000U     
#define SX_TIME_IN_WAKE                 160000U   

#define T_ALIVE_MS                      30000U      /* MCU stay-alive after wake   */
#define GPS_TIMEOUT_MS                  130000U      /* GPS fix timeout (ms)        */

#define TIME_PUBLISH_FULL_PW_MODE_MS    60000U

#define ENTER_SLEEP_TIMEOUT_MS          1000U

#define TIME_READ_PIN                   10000U

#define DELTA_T                         100U

/* ------------------------------------------------------------------ */
/*  W25Q128 Partition Table (16MB total) */
/* ------------------------------------------------------------------ */

#define FLASH_TOTAL_SIZE            (16U * 1024U * 1024U)   /* 16MB */

#define PART_BOOTLOADER_OFFSET      (0x000000U)
#define PART_BOOTLOADER_SIZE        (1U * 1024U * 1024U)    /* 1MB  */

#define PART_MQTT_CONFIG_OFFSET     (PART_BOOTLOADER_OFFSET + PART_BOOTLOADER_SIZE)
#define PART_MQTT_CONFIG_SIZE       (2U * 1024U * 1024U)    /* 2MB  */

#define PART_MISC_OFFSET            (PART_MQTT_CONFIG_OFFSET + PART_MQTT_CONFIG_SIZE)
#define PART_MISC_SIZE              (1U * 1024U * 1024U)    /* 1MB  */

#define PART_MSC_DISK_WIN           (PART_MISC_OFFSET + PART_MISC_SIZE)
#define PART_MSC_DISK_SIZE          (3U * 1024U * 1024U)    /*  3MB */

#define PART_GPS_LOG_OFFSET         (PART_MSC_DISK_WIN + PART_MSC_DISK_SIZE)
#define PART_GPS_LOG_SIZE           (FLASH_TOTAL_SIZE - PART_GPS_LOG_OFFSET) /* ~9MB */

/* GPS Log file path */
#define GPS_LOG_FILE_PATH           "/log_gps"
#define IMU_CALIB_FILE_PATH         "/imu_calib"

#define GPS_CSV_FILE_PATH           "log_gps.csv"   
// #define GPS_CSV_HEADER           "fix,lat,lon,rssi,time,date\n"
#define GPS_CSV_HEADER              "fix,lat,lon,alt,spd,sat,time,date\n"
#define GPS_LOG_READ_CHUNK          256U 

// #define GPS_LOG_MAX_ENTRIES 10

/* ================================================================== */
/*  ADC Voltage Reader                                                 */
/* ================================================================== */

#define USE_READ_PIN 0

#if USE_READ_PIN
#define ADC_VREF            3.3f
#define ADC_RESOLUTION      4095.0f
#define VBAT_DIVIDER_RATIO  1.47f
#define SAMPLE_PERIOD_MS    100U
#define LOG_PERIOD_MS       10000U

/* Kalman tuning
 *  Q small -> trust model, smooth but slow to follow real changes
 *  R large -> trust measurement less (noisy ADC)
 *  Adjust Q upward if Vbat changes are missed; adjust R upward for more smoothing */
/* Kalman tuning for battery voltage:
 * Q small = smooth, R large = trust measurement less */
#define VBAT_KALMAN_Q   0.0001f
#define VBAT_KALMAN_R   0.1f
#define VBAT_LPF_ALPHA  0.05f   

#define OVERSAMPLE_COUNT    12U
#endif

#endif /* APP_CONFIG_H */