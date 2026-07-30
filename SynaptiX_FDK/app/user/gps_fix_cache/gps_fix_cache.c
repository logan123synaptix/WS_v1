#include "gps_fix_cache.h"
#include "sx_ex_storage.h"
#include "cJSON.h"
#include "logger.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "GPS_FIX_CACHE";

/* Single-file cache, not a directory of entries like offline_queue's
 * OFFLINE_QUEUE_DIR — there is only ever "the most recent fix", not a
 * history, so one fixed path is enough. Parent dir created once in
 * gps_fix_cache_save()/_load() via sx_storage_mkdir() (idempotent, same
 * pattern app_init() uses for OFFLINE_QUEUE_DIR). */
#define GPS_FIX_CACHE_DIR   "/gps"
#define GPS_FIX_CACHE_PATH  "/gps/last_fix.json"
#define GPS_FIX_CACHE_BUFSZ 96U /* {"lat":-90.123456,"lon":-180.123456} plus slack */

void gps_fix_cache_save(float lat, float lon)
{
    sx_storage_mkdir(GPS_FIX_CACHE_DIR);

    /* Per the user: delete-then-recreate on every successful fix, rather
     * than an in-place overwrite — simplest safe pattern for LittleFS's
     * wear-leveling, and avoids ever reading back a partially-written
     * file if a save were interrupted mid-write. sx_storage_delete() on a
     * file that doesn't exist yet (very first fix ever) is expected to
     * just report not-found; that's fine, proceed to write regardless. */
    sx_storage_delete(GPS_FIX_CACHE_PATH);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "lat", lat);
    cJSON_AddNumberToObject(root, "lon", lon);

    char buf[GPS_FIX_CACHE_BUFSZ];
    memset(buf, 0, sizeof(buf));
    if (!cJSON_PrintPreallocated(root, buf, sizeof(buf), 0)) {
        log_error(TAG, "Failed to serialize fix (lat=%f lon=%f), not caching", lat, lon);
        cJSON_Delete(root);
        return;
    }
    cJSON_Delete(root);

    if (sx_storage_write(GPS_FIX_CACHE_PATH, buf, strlen(buf)) == SX_STORAGE_OK) {
        log_info(TAG, "Cached fix: %s", buf);
    } else {
        log_error(TAG, "Failed to write GPS fix cache to %s", GPS_FIX_CACHE_PATH);
    }
}

bool gps_fix_cache_load(float *out_lat, float *out_lon)
{
    if (!sx_storage_exists(GPS_FIX_CACHE_PATH)) {
        log_info(TAG, "No cached fix yet (%s does not exist)", GPS_FIX_CACHE_PATH);
        return false;
    }

    int32_t size = sx_storage_size(GPS_FIX_CACHE_PATH);
    if (size <= 0 || (uint32_t)size >= GPS_FIX_CACHE_BUFSZ) {
        log_error(TAG, "Cached fix file has bad size (%ld), ignoring", (long)size);
        return false;
    }

    char buf[GPS_FIX_CACHE_BUFSZ];
    memset(buf, 0, sizeof(buf));
    if (sx_storage_read(GPS_FIX_CACHE_PATH, buf, (uint32_t)size) != SX_STORAGE_OK) {
        log_error(TAG, "Failed to read cached fix from %s", GPS_FIX_CACHE_PATH);
        return false;
    }

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        log_error(TAG, "Cached fix file is not valid JSON: %s", buf);
        return false;
    }

    const cJSON *lat_j = cJSON_GetObjectItemCaseSensitive(root, "lat");
    const cJSON *lon_j = cJSON_GetObjectItemCaseSensitive(root, "lon");
    if (!cJSON_IsNumber(lat_j) || !cJSON_IsNumber(lon_j)) {
        log_error(TAG, "Cached fix file missing lat/lon: %s", buf);
        cJSON_Delete(root);
        return false;
    }

    *out_lat = (float)lat_j->valuedouble;
    *out_lon = (float)lon_j->valuedouble;
    cJSON_Delete(root);

    log_info(TAG, "Loaded cached fix: lat=%f lon=%f", *out_lat, *out_lon);
    return true;
}