#include "new_boot_backup_reg.h"
#include "stm32h5xx_hal.h"

/* TAMP->BKPxR is a contiguous array of 32-bit registers (BKP0R..BKP31R),
 * see RTC_BKP_NB (32) in stm32h563xx.h. We access it as an array via the
 * TAMP peripheral base to avoid a 32-way switch on the register name. */

void boot_backup_reg_init(void)
{
    /* Backup domain write protection (DBP) must be disabled before writing
     * to TAMP/RTC backup registers. HAL_PWR_EnableBkUpAccess() sets the DBP
     * bit in PWR_DBPR. Also enable the RTC/TAMP APB3 clock domain, which is
     * shared between RTC and TAMP on STM32H563 (no separate TAMPEN bit). */
    __HAL_RCC_RTC_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();
}

uint32_t boot_backup_reg_read(uint32_t index)
{
    return (&TAMP->BKP0R)[index];
}

void boot_backup_reg_write(uint32_t index, uint32_t value)
{
    (&TAMP->BKP0R)[index] = value;
}

void boot_backup_reg_clear(uint32_t index)
{
    boot_backup_reg_write(index, 0U);
}