#ifndef SX_USER_EXFLASH_H
#define SX_USER_EXFLASH_H

#ifdef __cplusplus
extern "C"{
#endif

#include <stdint.h>
#include <stdbool.h>
#include "stm32h5xx_hal.h"
#include "sx_spi.h"
#include "sx_gpio.h"

typedef enum {
    SX_STORAGE_OK           =  0,
    SX_STORAGE_ERR_INVALID  = -1,   /* fail param           */
    SX_STORAGE_ERR_IO       = -2,   /* errol read/write SPI */
    SX_STORAGE_ERR_NOT_FOUND= -3,   /* file not exist       */
    SX_STORAGE_ERR_NO_SPACE = -4,   /* flash full           */
    SX_STORAGE_ERR_NOT_INIT = -5,   /* not init             */
} sx_storage_err_t;

typedef struct {
    SPI_HandleTypeDef *hspi;
    sx_spi_t s_spi;
    sx_gpio_t s_cs;
    sx_gpio_pin_t cs_pin;
} sx_storage_cfg_t;

/*  Init    */
sx_storage_err_t sx_storage_init(sx_storage_cfg_t *cfg);

/*  File operation  */
sx_storage_err_t sx_storage_write        (const char *path, const void *data, uint32_t len);
sx_storage_err_t sx_storage_append       (const char *path, const void *data, uint32_t len);
sx_storage_err_t sx_storage_read         (const char *path, void *buf, uint32_t len);
sx_storage_err_t sx_storage_read_partial (const char *path, void *buf, uint32_t offset, uint32_t len);
sx_storage_err_t sx_storage_delete       (const char *path);
bool             sx_storage_exists       (const char *path);
int32_t          sx_storage_size         (const char *path);

/*  Directory operation */
sx_storage_err_t sx_storage_mkdir        (const char *path);
void             sx_storage_list_dir     (const char *path); // log-only listing, does not return entries to caller

/* One directory entry as returned by sx_storage_list(). "." and ".." are skipped. */
typedef struct {
    char     name[64]; /* entry filename only, no path prefix */
    bool     is_dir;
    uint32_t size;      /* file size in bytes; 0 if is_dir is true */
} sx_storage_entry_t;

/* Lists up to max_entries entries of directory `path` into the caller-provided
 * `entries` array. Returns the number of entries written (0..max_entries) on
 * success, or -1 on error (not initialized, invalid path, or directory open
 * failure). If the directory has more entries than max_entries, only the
 * first max_entries are returned (no indication is given that entries were
 * truncated - call with a larger buffer if this matters). */
int32_t sx_storage_list(const char *path, sx_storage_entry_t *entries, uint32_t max_entries);

/*  Fillesystem info    */
int32_t          sx_storage_free_space   (void);  
int32_t          sx_storage_total_space  (void);

/*  Danger zone */
sx_storage_err_t sx_storage_format       (void);
sx_storage_err_t sx_storage_factory_reset(void);

void sx_storage_sleep(void);
void sx_storage_wake(void);

#ifdef __cplusplus
}
#endif

#endif