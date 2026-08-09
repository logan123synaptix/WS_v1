#ifndef TEST_MUX_SELECT_H
#define TEST_MUX_SELECT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* DIAGNOSTIC ONLY -- not part of normal firmware operation.
 *
 * Purpose: isolate whether the TMUX4052 mux select GPIOs (UART5_S0_Pin /
 * UART5_S1_Pin) are actually being driven to the 4 distinct (S0,S1)
 * combinations expected for channel 0..3, independent of ze12a.c and
 * gas_sensor_init(). Uses the exact same pin definitions and the exact
 * same (channel & 0x01)/(channel & 0x02) addressing formula as
 * ze12a_select_mux_channel() in ze12a.c, so a real difference in
 * behaviour here would point at the GPIO/board layer, not at ze12a.c's
 * own logic (which was already re-checked and found correct: see ZE12A
 * handoff notes).
 *
 * DO NOT call this while gas_sensor_init() has already run (i.e. do not
 * call both test_ze12a_init() and select_mux_test_init() in the same
 * build) -- both own the same two physical GPIO pins and will fight
 * over them, producing meaningless results.
 *
 * Usage from main.c, inside the #if TEST block, in place of
 * test_ze12a_init():
 *
 *     select_mux_test_init();
 *     select_mux_test(0);   // set mux to channel 0, hold here
 *
 * Then, with a multimeter, probe the MCU pins for UART5_S0_Pin and
 * UART5_S1_Pin directly (not the mux chip's A0/A1 pins) and confirm:
 *   channel 0 -> S0=LOW,  S1=LOW
 *   channel 1 -> S0=HIGH, S1=LOW
 *   channel 2 -> S0=LOW,  S1=HIGH
 *   channel 3 -> S0=HIGH, S1=HIGH
 * Call select_mux_test() again with a different channel value (rebuild
 * and reflash, or step through via debugger/breakpoint) to check each
 * of the 4 combinations in turn. The firmware only needs to sit on one
 * channel at a time long enough for a manual multimeter reading, so
 * there is no polling loop here -- just call it once per channel you
 * want to check.
 */

/* One-time setup: initializes the two mux-select GPIOs (same pins
 * ze12a.c uses: UART5_S0_Pin/UART5_S1_Pin). Call once before the first
 * select_mux_test() call. Safe to call multiple times (re-initializes
 * the same two pins each time). */
void select_mux_test_init(void);

/* Drives the mux select lines (S0/S1) to the combination for the given
 * channel (0-3), using the identical bit0->S0 / bit1->S1 addressing
 * ze12a_select_mux_channel() uses internally. Returns immediately after
 * setting the GPIOs -- probe with a multimeter afterwards, there is no
 * built-in delay or loop. channel values outside 0-3 are masked to 2
 * bits (same masking ze12a.c performs implicitly via channel & 0x01 /
 * channel & 0x02), so e.g. passing 7 behaves identically to passing 3. */
void select_mux_test(uint8_t channel);

#ifdef __cplusplus
}
#endif
#endif /* TEST_MUX_SELECT_H */