#ifndef GPS_FIX_CACHE_H
#define GPS_FIX_CACHE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/* Persists the most recent successful GPS fix to external flash
 * (/gps/last_fix.json, via sx_ex_storage.h — LittleFS underneath), so a
 * cycle that fails to get a fresh fix within its wait window (see
 * app.c's APP_CYCLE_GPS_WAIT) can still publish a last-known position
 * instead of nulls. Per the user (2026-07-30): "mỗi lần có gps thì xóa
 * file đi rồi tạo lại cho dễ" — every successful fix replaces the file
 * outright (delete + write), not an in-place update; this is also the
 * simplest safe pattern for a wear-leveling filesystem like LittleFS. */

/* Deletes any existing cache file, then writes lat/lon as the new one.
 * Called once per cycle, only when APP_CYCLE_GPS_WAIT gets a fresh fix
 * (see app.c). Logs and no-ops (old file may or may not still exist) if
 * either the delete or the write fails — losing this cache is not fatal,
 * it only affects the *next* cycle's fallback if that one also fails to
 * fix. */
void gps_fix_cache_save(float lat, float lon);

/* Reads the cached fix into *out_lat/*out_lon if the cache file exists
 * and parses successfully. Returns true and fills both outputs on
 * success; returns false (outputs left untouched) if there is no cache
 * yet (e.g. very first boot, never had a fix) or the file is corrupt. */
bool gps_fix_cache_load(float *out_lat, float *out_lon);

#ifdef __cplusplus
}
#endif

#endif