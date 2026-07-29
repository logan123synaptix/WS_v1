#include "test_gps.h"
#include "sx_board.h"
#include "gps.h"
#include "logger.h"

static const char *TAG = "TEST_GPS";

/* How often to print a "still waiting" heartbeat while no fix has been
 * parsed yet. Purely a liveness indicator for this test — gps.c logs
 * every actual parsed sentence itself already (see test_gps.h note). */
#define TEST_GPS_HEARTBEAT_MS 5000U

static uint32_t s_heartbeat_accum_ms = 0;
static bool     s_first_fix_logged   = false;

void test_gps_init(void)
{
    log_info(TAG, "=== TEST GPS GP02 (UART2, 9600 baud) ===");

    /* board.gps was already gps_init()'d inside sx_board_init()
     * (sx_board.c) — do NOT call gps_init() again here, it would
     * re-toggle N/F and RST GPIOs and reset the UART ring buffer for
     * no reason. gps_init() already drives N/F=HIGH (powered) and
     * RST=HIGH (out of reset), so the module should already be running
     * and emitting NMEA sentences by the time this test starts polling. */

    log_info(TAG, "Waiting for NMEA sentences — cold start under open sky "
                   "can take 30s to a few minutes for first fix.");
    log_info(TAG, "RMC/GGA sentences are logged directly by the GPS driver "
                   "(tag=GPS) as they are parsed — watch for that tag below.");
}

void test_gps_poll(uint32_t delta_ms)
{
    /* Drives the byte-at-a-time NMEA sentence assembly + parse
     * callback, same call app.c's real gps_process() makes every tick
     * (see app.c line ~760). Parsed RMC/GGA sentences are logged
     * inside gps.c itself (tag=GPS) — this function does not need to
     * inspect board.gps.latitude/longtitude itself to report a fix. */
    gps_process(&board.gps, delta_ms);

    /* Heartbeat: only prints before the first fix, so the test log
     * doesn't get noisy once real GPS/GGA lines are already flowing. */
    if (s_first_fix_logged) {
        return;
    }

    if (board.gps.latitude != 0.0f || board.gps.longtitude != 0.0f) {
        /* First non-zero coordinate we've observed — gps.c's RMC
         * handler only writes these on rmc.valid == true, so this is
         * a real fix, not a parse of an invalid/empty sentence (those
         * explicitly zero latitude/longtitude, see gps_callback_task()). */
        s_first_fix_logged = true;
        log_info(TAG, "First fix observed: lat=%.6f lon=%.6f (full detail logged above by tag=GPS)",
                  board.gps.latitude, board.gps.longtitude);
        return;
    }

    s_heartbeat_accum_ms += delta_ms;
    if (s_heartbeat_accum_ms >= TEST_GPS_HEARTBEAT_MS) {
        s_heartbeat_accum_ms = 0;
        log_info(TAG, "... still waiting for fix (no valid RMC yet)");
    }
}