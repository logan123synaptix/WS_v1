#ifndef NETWORK_CONFIG_H
#define NETWORK_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Runtime-editable, flash-persisted connection settings — broker host/
 * port/credentials/certs plus the GSM APN. Per discussion with the user:
 *
 *  - Thingsboard is NOT used for now. The project currently has no real
 *    Thingsboard broker to point at (app_config.h's USE_THINGSBOARD==1
 *    branch has no MQTT_HOST defined at all — see app.c's TODO comment
 *    at thingsboard_client_init()). Until the user provides a real
 *    Thingsboard broker, this module's defaults come from app_config.h's
 *    #else branch (MQTT_HOST/MQTT_CLIENT_ID/MQTT_USER/MQTT_PASS — the
 *    plain, non-Thingsboard MQTT config already in that file), NOT from
 *    Thingsboard's config path. app.c should build its sx_user_mqtt_cfg_t
 *    from network_config_get() rather than those macros directly, so
 *    switching brokers later doesn't mean re-touching app.c's init code.
 *
 *  - All of host/port/client_id/username/password/certs/APN must be
 *    changeable at runtime (not just compile-time #defines), and persist
 *    across reset/power-loss — hence flash-backed via sx_ex_storage
 *    (already available and already sx_storage_init()'d in
 *    sx_board_init(), see sx_board.c).
 *
 *  - Runtime *input* is now implemented via a USB CDC CLI, see
 *    app/user/shell_app/ (shell_app.c/.h + shell_commands.c) — command
 *    "settings -c -host/-port/-... -pump/-sensing/-data ..." calls the
 *    setters below then network_config_save() once. USB MSC (editing a
 *    config file on the exposed disk) is not wired up — the CLI covers
 *    this need for now.
 *
 *  - Also carries the main cycle timing (pump_on_ms/sensing_ms/sleep_ms)
 *    per the user's decision to consolidate all runtime-editable config
 *    into this one struct/flash file, even though the struct is no
 *    longer purely "network" config — kept the module name to avoid
 *    touching every include site.
 *
 *  - TLS certs are stored as raw blobs (ca_cert/client_cert/client_key),
 *    sized generously (NETWORK_CONFIG_CERT_MAX_LEN) since real cert PEM
 *    text can run a few KB; unused/non-TLS setups just leave them empty
 *    (cert_len == 0). This module does not validate cert contents, only
 *    stores/loads them — same "thin storage layer" spirit as the rest of
 *    this module. */

#define NETWORK_CONFIG_HOST_MAX_LEN       128U
#define NETWORK_CONFIG_CLIENT_ID_MAX_LEN  64U
#define NETWORK_CONFIG_USER_MAX_LEN       64U
#define NETWORK_CONFIG_PASS_MAX_LEN       64U
#define NETWORK_CONFIG_APN_MAX_LEN        32U
#define NETWORK_CONFIG_CERT_MAX_LEN       4096U

/* Flash path this config is persisted to/from (sx_ex_storage / littlefs,
 * see sx_storage_write()/sx_storage_read() in sx_ex_storage.h). Stored as
 * a raw struct blob, not JSON — simplest thing that works for a single
 * fixed-size record; revisit if human-editable-via-MSC becomes a real
 * requirement later (would want JSON or similar at that point). */
#define NETWORK_CONFIG_FLASH_PATH  "/network_config.bin"

typedef struct {
    /* Broker connection */
    char     host[NETWORK_CONFIG_HOST_MAX_LEN];
    uint16_t port;
    char     client_id[NETWORK_CONFIG_CLIENT_ID_MAX_LEN];
    char     username[NETWORK_CONFIG_USER_MAX_LEN];   /* empty string = no auth */
    char     password[NETWORK_CONFIG_PASS_MAX_LEN];   /* empty string = no auth */
    uint16_t keepalive_s;
    bool     use_tls;

    /* TLS certs — only meaningful when use_tls is true. cert_len == 0
     * means "not set" for that slot. */
    char     ca_cert[NETWORK_CONFIG_CERT_MAX_LEN];
    uint32_t ca_cert_len;
    char     client_cert[NETWORK_CONFIG_CERT_MAX_LEN];
    uint32_t client_cert_len;
    char     client_key[NETWORK_CONFIG_CERT_MAX_LEN];
    uint32_t client_key_len;

    /* GSM APN */
    char     apn[NETWORK_CONFIG_APN_MAX_LEN];
    char     apn_username[NETWORK_CONFIG_USER_MAX_LEN];  /* empty string = NULL to a7677s_set_full_apn() */
    char     apn_password[NETWORK_CONFIG_PASS_MAX_LEN];  /* empty string = NULL to a7677s_set_full_apn() */

    /* Main cycle timing (ms) — replaces app_config.h's APP_PUMP_ON_MS/
     * APP_SENSING_MS/APP_CYCLE_PERIOD_MS #defines at runtime. Per the
     * user's decision, these live in this same struct/flash file rather
     * than a separate one, even though the struct is no longer purely
     * "network" config. app_config.h's #defines now only seed
     * build_defaults() below for a fresh/empty flash; app.c reads these
     * fields via network_config_get() instead of the #defines directly. */
    uint32_t pump_on_ms;   /* how long the pump stays on before sensing starts */
    uint32_t sensing_ms;   /* how long SENSING runs (SPS30 cycle + other sensors settle) */
    uint32_t sleep_ms;     /* STOP-mode sleep duration itself (not the total lap time) */

    /* Bumped whenever this struct's layout changes, so a future firmware
     * update can detect a stale/incompatible record in flash and fall
     * back to defaults instead of misinterpreting old bytes. Not
     * currently enforced beyond being stored — no migration logic yet. */
    uint32_t version;
} network_config_t;

#define NETWORK_CONFIG_VERSION  1U

/* Loads config from flash (NETWORK_CONFIG_FLASH_PATH). If the file does
 * not exist, or its stored version doesn't match NETWORK_CONFIG_VERSION,
 * falls back to defaults built from app_config.h's plain-MQTT #defines
 * (MQTT_HOST/MQTT_CLIENT_ID/MQTT_USER/MQTT_PASS — NOT the Thingsboard
 * ones, see the module doc-comment above) and APN/USERNAME_APN/
 * PASSWORD_APN, then immediately saves that default to flash so the
 * next boot loads it directly. sx_storage_init() (sx_board.c) must have
 * already run before this is called. */
void network_config_init(void);

/* Returns a pointer to the live in-RAM config. Callers should treat this
 * as read-only unless going through the setters below — direct field
 * writes bypass no validation today, but bypass network_config_save()'s
 * single save point too, so prefer the setters for anything meant to
 * persist. */
const network_config_t *network_config_get(void);

/* Field setters — each only updates the in-RAM copy; call
 * network_config_save() afterwards to persist to flash. Kept separate
 * (rather than one big "set everything" call) so a future CDC command
 * handler can update one field at a time without needing the full
 * struct on hand. Every string setter truncates to the field's *_MAX_LEN
 * (including the null terminator) rather than overflowing. */
void network_config_set_host(const char *host);
void network_config_set_port(uint16_t port);
void network_config_set_client_id(const char *client_id);
void network_config_set_username(const char *username);
void network_config_set_password(const char *password);
void network_config_set_keepalive(uint16_t keepalive_s);
void network_config_set_use_tls(bool use_tls);
void network_config_set_ca_cert(const char *pem, uint32_t len);
void network_config_set_client_cert(const char *pem, uint32_t len);
void network_config_set_client_key(const char *pem, uint32_t len);
void network_config_set_apn(const char *apn);
void network_config_set_apn_username(const char *apn_username);
void network_config_set_apn_password(const char *apn_password);

/* Cycle timing setters (ms). 0 is rejected by the CLI layer before it
 * ever reaches here (see shell_app/shell_commands.c) — these setters
 * themselves do not validate, same "thin" spirit as the rest of this
 * module. */
void network_config_set_pump_on_ms(uint32_t pump_on_ms);
void network_config_set_sensing_ms(uint32_t sensing_ms);
void network_config_set_sleep_ms(uint32_t sleep_ms);

/* Persists the current in-RAM config to flash (NETWORK_CONFIG_FLASH_PATH).
 * Returns true on success. Callers should batch several set_*() calls
 * before calling this once, rather than saving after every single field
 * change (flash writes are not free). */
bool network_config_save(void);

/* Resets the in-RAM config back to the app_config.h-derived defaults
 * (same values network_config_init() would fall back to) and saves them
 * to flash immediately. Does not touch certs (cleared to empty/0, same
 * as a fresh default). */
void network_config_reset_to_defaults(void);

#ifdef __cplusplus
}
#endif

#endif