#include "network_config.h"
#include "app_config.h"
#include "sx_ex_storage.h"
#include "logger.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "NET_CFG";

static network_config_t s_cfg;

/* Truncating string copy — always null-terminates within dst_size,
 * never overflows even if src is longer than dst_size - 1. Used by
 * every string setter below instead of strncpy() directly, since plain
 * strncpy() does not guarantee null-termination when src is >= dst_size. */
static void safe_strcpy(char *dst, uint32_t dst_size, const char *src)
{
    if (dst == NULL || dst_size == 0) return;
    if (src == NULL) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Builds the app_config.h-derived default config. Per the module
 * doc-comment in network_config.h: defaults come from the plain-MQTT
 * #defines (MQTT_HOST/MQTT_CLIENT_ID/MQTT_USER/MQTT_PASS, only defined
 * when USE_THINGSBOARD==0 in app_config.h), NOT from Thingsboard's
 * MQTT_ID/THINGSBOARD_* macros — Thingsboard is not in use yet, see
 * that doc-comment for why. If USE_THINGSBOARD==1 at compile time (as
 * it currently is), those plain-MQTT macros are not defined either
 * (app_config.h's #else branch), so this falls back further to a
 * hardcoded placeholder host — same "flag it loudly rather than invent
 * a real-looking value" approach as app.c's TODO at
 * thingsboard_client_init(). Whoever picks the real non-Thingsboard
 * broker to use should update this function (or, more likely, the
 * still-not-written CDC/MSC input layer that calls network_config_set_*()
 * at runtime instead — see network_config.h). */
static void build_defaults(network_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

#if !USE_THINGSBOARD
    safe_strcpy(cfg->host, sizeof(cfg->host), MQTT_HOST);
    cfg->port = MQTT_PORT;
    safe_strcpy(cfg->client_id, sizeof(cfg->client_id), MQTT_CLIENT_ID);
    safe_strcpy(cfg->username, sizeof(cfg->username), MQTT_USER ? MQTT_USER : "");
    safe_strcpy(cfg->password, sizeof(cfg->password), MQTT_PASS ? MQTT_PASS : "");
    cfg->keepalive_s = MQTT_KEEPALIVE;
#else
    /* USE_THINGSBOARD==1 but Thingsboard is not actually in use yet (see
     * doc-comment above) — app_config.h's #else branch (which defines
     * MQTT_HOST/MQTT_CLIENT_ID/etc.) is not compiled in this case, so
     * there is no real non-Thingsboard broker macro available here
     * either. Placeholder, same spirit as app.c's TODO — MUST be
     * replaced (via network_config_set_host() + _save(), or by editing
     * this function) once a real broker is decided. */
    safe_strcpy(cfg->host, sizeof(cfg->host), "REPLACE_ME_BROKER_HOST");
    cfg->port = 1883;
    safe_strcpy(cfg->client_id, sizeof(cfg->client_id), "REPLACE_ME_CLIENT_ID");
    cfg->username[0] = '\0';
    cfg->password[0] = '\0';
    cfg->keepalive_s = 60;
#endif

    cfg->use_tls          = false;
    cfg->ca_cert_len       = 0;
    cfg->client_cert_len   = 0;
    cfg->client_key_len    = 0;

    safe_strcpy(cfg->apn, sizeof(cfg->apn), APN);
    safe_strcpy(cfg->apn_username, sizeof(cfg->apn_username), USERNAME_APN ? USERNAME_APN : "");
    safe_strcpy(cfg->apn_password, sizeof(cfg->apn_password), PASSWORD_APN ? PASSWORD_APN : "");

    cfg->version = NETWORK_CONFIG_VERSION;
}

/* Per the user: read the file if it exists and has content, and use it
 * directly — do not try to detect/reject a "stale version" record. If
 * the file does not exist, or exists but is empty (size <= 0), fall back
 * to app_config.h-derived defaults. This is the whole rule; no extra
 * validation layer beyond "does a non-empty file exist". */
void network_config_init(void)
{
    int32_t size = sx_storage_size(NETWORK_CONFIG_FLASH_PATH);

    if (size > 0) {
        sx_storage_err_t err = sx_storage_read(NETWORK_CONFIG_FLASH_PATH, &s_cfg, sizeof(s_cfg));
        if (err == SX_STORAGE_OK) {
            log_info(TAG, "Loaded network config from flash (host=%s port=%u)",
                      s_cfg.host, s_cfg.port);
            return;
        }
        log_warn(TAG, "Failed to read stored network config (err=%d) - using defaults", err);
    } else {
        log_info(TAG, "No network config file (or empty) - using defaults");
    }

    build_defaults(&s_cfg);
    if (!network_config_save()) {
        log_error(TAG, "Failed to save default network config to flash");
    }
}

const network_config_t *network_config_get(void)
{
    return &s_cfg;
}

void network_config_set_host(const char *host)
{
    safe_strcpy(s_cfg.host, sizeof(s_cfg.host), host);
}

void network_config_set_port(uint16_t port)
{
    s_cfg.port = port;
}

void network_config_set_client_id(const char *client_id)
{
    safe_strcpy(s_cfg.client_id, sizeof(s_cfg.client_id), client_id);
}

void network_config_set_username(const char *username)
{
    safe_strcpy(s_cfg.username, sizeof(s_cfg.username), username);
}

void network_config_set_password(const char *password)
{
    safe_strcpy(s_cfg.password, sizeof(s_cfg.password), password);
}

void network_config_set_keepalive(uint16_t keepalive_s)
{
    s_cfg.keepalive_s = keepalive_s;
}

void network_config_set_use_tls(bool use_tls)
{
    s_cfg.use_tls = use_tls;
}

/* Cert setters share the same truncate-and-record-length shape; len==0
 * or pem==NULL clears the slot (cert_len set to 0, same as "not set"). */
static void set_cert_slot(char *dst, uint32_t dst_cap, uint32_t *len_out,
                           const char *pem, uint32_t len)
{
    if (pem == NULL || len == 0) {
        dst[0] = '\0';
        *len_out = 0;
        return;
    }
    uint32_t n = (len >= dst_cap) ? (dst_cap - 1) : len;
    memcpy(dst, pem, n);
    dst[n] = '\0';
    *len_out = n;
}

void network_config_set_ca_cert(const char *pem, uint32_t len)
{
    set_cert_slot(s_cfg.ca_cert, sizeof(s_cfg.ca_cert), &s_cfg.ca_cert_len, pem, len);
}

void network_config_set_client_cert(const char *pem, uint32_t len)
{
    set_cert_slot(s_cfg.client_cert, sizeof(s_cfg.client_cert), &s_cfg.client_cert_len, pem, len);
}

void network_config_set_client_key(const char *pem, uint32_t len)
{
    set_cert_slot(s_cfg.client_key, sizeof(s_cfg.client_key), &s_cfg.client_key_len, pem, len);
}

void network_config_set_apn(const char *apn)
{
    safe_strcpy(s_cfg.apn, sizeof(s_cfg.apn), apn);
}

void network_config_set_apn_username(const char *apn_username)
{
    safe_strcpy(s_cfg.apn_username, sizeof(s_cfg.apn_username), apn_username);
}

void network_config_set_apn_password(const char *apn_password)
{
    safe_strcpy(s_cfg.apn_password, sizeof(s_cfg.apn_password), apn_password);
}

bool network_config_save(void)
{
    s_cfg.version = NETWORK_CONFIG_VERSION;
    sx_storage_err_t err = sx_storage_write(NETWORK_CONFIG_FLASH_PATH, &s_cfg, sizeof(s_cfg));
    if (err != SX_STORAGE_OK) {
        log_error(TAG, "sx_storage_write failed (err=%d)", err);
        return false;
    }
    return true;
}

void network_config_reset_to_defaults(void)
{
    build_defaults(&s_cfg);
    if (!network_config_save()) {
        log_error(TAG, "Failed to save default network config to flash");
    }
}