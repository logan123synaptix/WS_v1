#include "sx_uart.h"
#include "sx_mem.h"
#include "sx_config.h"
#include "sx_delay.h"
#include "logger.h"

static const char *TAG = "SX_UART";

#if SX_PLATFORM == SX_PLATFORM_STM32H5 
    #include "stm32h5xx_hal.h"
    #include "stm32h5xx_hal_uart.h"
#elif SX_PLATFORM == SX_PLATFORM_STM32H7
    #include "stm32h7xx_hal.h"
    #include "stm32h7xx_hal_uart.h"
#elif SX_PLATFORM == SX_PLATFORM_STM32F7
    #include "stm32f7xx_hal.h"
    #include "stm32f7xx_hal_uart.h"
#elif SX_PLATFORM == SX_PLATFORM_STM32F4
    #include "stm32f4xx_hal.h"
    #include "stm32f4xx_hal_uart.h"
#elif SX_PLATFORM == SX_PLATFORM_STM32F1
    #include "stm32f1xx_hal.h"
    #include "stm32f1xx_hal_uart.h"
#endif

void sx_uart_init(sx_uart_t *_uart, sx_uart_config_t *_config, int _rxBufferSize, int _txBufferSize){
    _uart->config = _config;
    _uart->rxBufferSize = _rxBufferSize;
    if(!_uart->rxBuffer) {
        _uart->rxBuffer = sx_malloc(_rxBufferSize);
    }
    cqueue_init_static(&_uart->rxQueue, _uart->rxBuffer, _rxBufferSize, sizeof(uint8_t));  // len=512, size=1
    _uart->txBufferSize = _txBufferSize;
    if(!_uart->txBuffer) {
        _uart->txBuffer = sx_malloc(_txBufferSize);
    }
    cqueue_init_static(&_uart->txQueue, _uart->txBuffer, _txBufferSize, sizeof(uint8_t));  // len=512, size=1
    #if (SX_USE_OS == 1)
    sx_os_mutex_new(&_uart->rxMutex);
    #endif
    // log_debug(TAG, "UART initialized with baudrate: %d, bits: %d, parity: %d, stopbits: %d", _config->baudrate, _config->bits, _config->parity, _config->stopbits);
}
void sx_uart_write(sx_uart_t *_uart, const uint8_t *_data, int _len){
    // log_debug(TAG, "UART write: %d bytes", _len);
#if SX_PLATFORM == SX_PLATFORM_STM32H5 || SX_PLATFORM == SX_PLATFORM_STM32H7 || SX_PLATFORM == SX_PLATFORM_STM32F7 || SX_PLATFORM == SX_PLATFORM_STM32F4 || SX_PLATFORM == SX_PLATFORM_STM32F1
    HAL_UART_Transmit((UART_HandleTypeDef *)_uart->config->pDriver, (uint8_t *)_data, _len, 1000);
#endif
}
int sx_uart_read(sx_uart_t *_uart, uint8_t *_data, int _len, uint32_t _timeoutMS){
    #if (SX_USE_OS == 1)
    sx_os_mutex_lock(_uart->rxMutex);
    #endif
    int len = 0;
    uint32_t time = 0;
    /* BUG FIX (2026-07-29): was `time < _timeoutMS`, which for the
     * common non-blocking poll case (_timeoutMS == 0, e.g.
     * shell_app_poll()/test_shell_poll()'s "drain whatever's already in
     * the ring buffer, don't wait" pattern) evaluates to `0 < 0` = false
     * on the very FIRST check — so the loop body (the only place
     * cqueue_receive() is called) never runs even once, even though
     * sx_uart_available() already reported bytes waiting in rxQueue.
     * Confirmed on real hardware via debug logging: sx_uart_available()
     * correctly reported avail=6 (a full "help\r\n"), but every
     * sx_uart_read(..., 0) call returned 0 bytes read, over and over,
     * every tick — since shell_app_poll()'s own `while
     * (sx_uart_available() > 0)` loop never saw the count drop, it
     * looped forever within a single call, permanently starving
     * main.c's while(1) of any further ticks (this is what looked like
     * "the shell echoes then hangs" — the MCU was not hung on UART, it
     * was spinning in this one function).
     * Fix: `<=` instead of `<` — for _timeoutMS==0 this allows exactly
     * one iteration (time=0 <= 0), i.e. "grab whatever is already
     * buffered, don't wait for more" — the behavior every non-blocking
     * caller actually wants. For _timeoutMS>0 this is a one-iteration
     * difference out of many and does not meaningfully change blocking
     * behavior for any existing caller (AT command parsing, etc.). */
    while(len < _len && time <= _timeoutMS){
        /* Bug fix (2026-07-28): cqueue_receive() does a non-atomic
         * read-modify-write on queue->count (and head/tail), and the
         * SAME queue is also written from sx_uart_rx_callback() running
         * in the UART RX interrupt (bare-metal build, SX_USE_OS == 0,
         * so there is no mutex above to serialize this). If the ISR
         * fires in the middle of cqueue_receive()'s count update, the
         * two updates race and one gets lost, corrupting count/head/tail.
         * Symptom observed on real hardware: AT command responses
         * missing their first byte (e.g. "+CMQTTPUB: 0,0" arriving as
         * "CMQTTPUB: 0,0", or "AT+CSQ" response truncated to "]K"),
         * happening frequently under UART traffic, not randomly.
         * Fix: briefly disable IRQs around the single-consumer call so
         * the ISR (single producer) can't interleave with it. Kept here
         * at the call site (not inside cqueue.c) so cqueue stays a
         * generic, ISR-agnostic utility usable elsewhere without this
         * overhead.
         * Uses save/restore of PRIMASK (not a bare __disable_irq() /
         * __enable_irq() pair) so this is safe to call even if it ever
         * ends up nested inside another critical section elsewhere --
         * restoring the saved mask can't accidentally re-enable
         * interrupts that the outer caller intended to keep off. */
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        bool got = cqueue_receive(&_uart->rxQueue, _data + len);
        __set_PRIMASK(primask);
        if(got == false){
            // sx_delay_ms(1);
            // time += 1;
            break;
        }
        len++;
    }
    #if (SX_USE_OS == 1)
    sx_os_mutex_unlock(_uart->rxMutex);
    #endif
    return len;
}
int sx_uart_available(sx_uart_t *_uart){
    //log_debug(TAG, "UART available: %d bytes", _uart->rxQueue.count);
    return _uart->rxQueue.count;
}

int sx_uart_rx_callback(sx_uart_t *_uart, const uint8_t *_data, int _len){
    /* Paired with the PRIMASK save/restore guard in sx_uart_read():
     * this callback runs in the UART RX ISR and is the single producer
     * into rxQueue, while sx_uart_read() in the main loop is the single
     * consumer. Wrapping the ISR side too makes the critical section
     * symmetric and safe even if UART interrupts are ever nested with
     * a higher-priority IRQ that also touches this queue. See
     * sx_uart_read() for the full bug writeup. */
    int len = 0;
    while(len < _len){
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        bool sent = cqueue_send(&_uart->rxQueue, _data + len);
        __set_PRIMASK(primask);
        if(sent == true){
            len++;
        } else {
            break;
        }
    }
    return len;
}
int sx_uart_tx_callback(sx_uart_t *_uart){
    return 0;
}

int sx_uart_flush(sx_uart_t *_uart)
{
    /* Bug fix (2026-07-31): cqueue_init_static() resets head/tail/count
     * non-atomically, and this queue is also written from
     * sx_uart_rx_callback() running in the UART RX interrupt (same
     * single-producer/single-consumer queue as sx_uart_read() above --
     * see that function's 2026-07-28 bug fix comment for the full
     * writeup of the underlying race). Without disabling IRQs here, an
     * RX byte arriving mid-reset can interleave with the head/tail/count
     * write and leave the ring buffer's pointers inconsistent, corrupting
     * whatever is read next. Confirmed on real hardware: calling
     * sx_uart_flush() right before each SPS30 SHDLC command (added to
     * clear stale bytes between commands) started producing a
     * consistent SENSIRION_SHDLC_ERR_EXECUTION_FAILURE (-8) from the
     * very first cycle, where before the fix it took several cycles for
     * unrelated stale-byte errors to snowball -- pointing at flush()
     * itself corrupting the queue right as SPS30's UART interrupt (just
     * re-armed by board_dust_uart_resume_it()) was live. Same
     * save/restore PRIMASK pattern as the other two functions in this
     * file, so this stays safe even if ever called from within another
     * critical section. */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    cqueue_init_static(&_uart->rxQueue, _uart->rxBuffer, _uart->rxBufferSize, sizeof(uint8_t));
    __set_PRIMASK(primask);
    return 0;
}

int sx_uart_abort(sx_uart_t *_uart){
    HAL_UART_Abort((UART_HandleTypeDef *)_uart->config->pDriver);
}