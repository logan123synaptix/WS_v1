#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "modem_ops.h"
#include "gps.h"
#include "sx_ex_rtc.h"

/* Syncs the hardware RTC (RX8130CE) to a real-world time source exactly
 * once, then stops checking. Per the user: modem NITZ (network-provided
 * time, via modem_ops_t.get_time_synced()/get_synced_time() — see
 * a7677s.c's A7677S_INIT_CTZU/A7677S_INIT_CCLK steps) is the primary
 * source since it's available as soon as the modem attaches to the
 * network, with no need to wait for a GPS fix. GPS UTC (sx_gps_t's `tim`
 * field, populated from NMEA RMC sentences once a fix is valid) is the
 * fallback, used only if the modem never reports a synced time. Neither
 * source is polled again after the first successful sync — the RX8130CE
 * keeps its own time from then on (it has a backup supply per its
 * datasheet), same "set once, hardware keeps it after that" assumption
 * WS_v0 made for its own RTC usage. */

typedef struct {
    modem_handle_t *modem;  /* e.g. &board.modem */
    sx_gps_t       *gps;    /* e.g. &board.gps */
    rx8130ce_t     *rtc;    /* e.g. &board.rtc */
    uint8_t         done;   /* 1 once the RTC has been set from either source */
} time_sync_t;

void time_sync_init(time_sync_t *ts, modem_handle_t *modem, sx_gps_t *gps, rx8130ce_t *rtc);

/* Call every tick from app_process(). No-op once ts->done is 1 — cheap to
 * call unconditionally forever after that point. */
void time_sync_poll(time_sync_t *ts);

/* True once the RTC has been synced from either source. */
uint8_t time_sync_is_done(const time_sync_t *ts);

#ifdef __cplusplus
}
#endif

#endif