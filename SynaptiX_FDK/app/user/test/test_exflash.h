#ifndef TEST_EXFLASH_H
#define TEST_EXFLASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Standalone bring-up test for the external flash W25Q128 (SPI), going
 * through the real storage-layer API (sx_ex_storage.c/.h), same
 * pattern as test_sht3x.c / test_rtc.c / test_gps.c: reuse the real
 * stack, don't re-implement SPI access here.
 *
 * IMPORTANT — architecture note (previous version of this test used
 * board.q128.initialized directly and always failed): board.q128
 * (Board_t field, sx_board.h) is DEAD — it is declared but never
 * written or read anywhere else in the repo. The real sx_W25Q128_t
 * instance lives as a private `static` inside sx_ex_storage.c and is
 * NOT reachable from board.q128. Any test written against board.q128
 * directly will always report "not initialized", regardless of SPI
 * wiring or init order — that is not a hardware bug, it is a stale
 * reference to a field the architecture no longer routes through.
 *
 * sx_storage_init() is already called inside sx_board_init()
 * (sx_board.c, via board.storage_cfg) — this test does not call it
 * again. It only exercises the public sx_storage_*() file API
 * (write/read/exists/size/delete/free_space), which is the same API
 * app.c's offline MQTT queue actually uses — so this test validates
 * the real code path, not a lower-level shortcut around it.
 *
 * Call test_exflash_init() once after sx_board_init() (Core/Src/main.c),
 * then test_exflash_poll(delta_ms) every tick in the main while(1)
 * loop — same calling convention as the other test_*_poll() functions,
 * even though this test only actually does work on its first few
 * calls (see test_exflash.c for the one-shot sequencing). */

void test_exflash_init(void);
void test_exflash_poll(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif