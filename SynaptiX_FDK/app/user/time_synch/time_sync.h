#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "modem_ops.h"
#include "gps.h"
#include "sx_ex_rtc.h"

/* Syncs the hardware RTC (RX8130CE) to a real-world time source.
 *
 * Per the user (2026-08-01): originally designed to sync exactly once
 * ever, on the assumption the RX8130CE's own backup-powered oscillator
 * would keep accurate time indefinitely after that. Confirmed on real
 * hardware this drifts noticeably over just tens of minutes of runtime
 * (observed ~38s offset between the RTC-derived telemetry timestamp and
 * the wall-clock time the MQTT payload actually reached the broker) --
 * ordinary quartz RTC drift, not a bug in the RTC chip itself, but too
 * much for this application. Fresh NITZ time is already available every
 * wake anyway (a7677s.c's A7677S_INIT_CCLK init step re-runs as part of
 * the normal modem attach sequence on every wake, logging "Network time
 * synced (UTC): ..." each time -- this was already happening, just never
 * consumed past the first cycle), so time_sync_reset() lets the caller
 * (app.c's APP_CYCLE_WAKING case, once per wake right after
 * sx_sleep_manager_is_wake_done() reports true — by which point
 * modem_wait_ready has already completed and this wake's CCLK read is
 * in) clear `done` and let time_sync_poll() re-sync the RTC from that
 * fresh NITZ reading every cycle instead of only the very first one.
 *
 * modem NITZ (network-provided time, via modem_ops_t.get_time_synced()/
 * get_synced_time() — see a7677s.c's A7677S_INIT_CTZU/A7677S_INIT_CCLK
 * steps) is the primary source since it's available as soon as the modem
 * attaches to the network, with no need to wait for a GPS fix. GPS UTC
 * (sx_gps_t's `tim` field, populated from NMEA RMC sentences once a fix
 * is valid) is the fallback, used only if the modem never reports a
 * synced time for a given cycle. */

typedef struct {
    modem_handle_t *modem;  /* e.g. &board.modem */
    sx_gps_t       *gps;    /* e.g. &board.gps */
    rx8130ce_t     *rtc;    /* e.g. &board.rtc */
    uint8_t         done;   /* 1 once the RTC has been set from either source this cycle */
} time_sync_t;

void time_sync_init(time_sync_t *ts, modem_handle_t *modem, sx_gps_t *gps, rx8130ce_t *rtc);

/* Call every tick from app_process(). No-op once ts->done is 1 for the
 * current cycle — cheap to call unconditionally. */
void time_sync_poll(time_sync_t *ts);

/* True once the RTC has been synced from either source this cycle. */
uint8_t time_sync_is_done(const time_sync_t *ts);

/* Clears ts->done so the next time_sync_poll() call re-syncs the RTC from
 * whatever fresh time source is available (normally modem NITZ, refreshed
 * every wake — see this file's top comment). Intended to be called once
 * per wake, after the modem has finished attaching (so get_time_synced()
 * reflects this wake's CCLK read, not a stale flag) — see app.c's
 * APP_CYCLE_WAKING case. Does not touch the RTC itself; the actual
 * re-sync only happens on the following time_sync_poll() call(s), same as
 * the original first-sync path. */
void time_sync_reset(time_sync_t *ts);

#ifdef __cplusplus
}
#endif

#endif