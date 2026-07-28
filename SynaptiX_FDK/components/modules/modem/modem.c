#include "modem.h"
#include "logger.h"
#include "sx_delay.h"

static const char *TAG = "MODEM";

void modem_init(modem_t *modem){
    modem->isBusy = 0;
    modem->isReady = 0;
    modem->resID = 0;
    modem->cmd = NULL;
    modem->waitElapsed = 0;
    /* hasPowerPin defaults to 0 (no VBAT cutoff transistor). Board init code
     * (sx_board.c) must explicitly set this to 1 only for boards that wire
     * the transistor for this modem. Never assume, never leave to chance. */
    modem->hasPowerPin = 0;
    log_debug(TAG,"Initializing");

}

int modem_send_command(modem_t *modem, modem_command_t *cmd, uint32_t timeout){
    if(modem->isBusy) return -1;
    log_debug(TAG,"Send command %s",cmd->cmd);
    modem->cmd = cmd;
    modem->resID = 0;
    modem->isBusy = 1;
    modem->timeOut = timeout;
    modem->buff_id = 0;
    /* Bug fix (2026-07-28): buff_id=0 only resets the write cursor, it does
     * NOT clear old bytes still sitting in modem->buff from the PREVIOUS
     * command. Since buff is a fixed 512-byte array with no guaranteed
     * null-terminator management, strstr(modem->buff, ...) below in
     * modem_poll() can read straight through into leftover stale data from
     * an earlier command whenever the new response is shorter than the old
     * one. Confirmed on real board: a7677s.c's CREG-poll debug log showed
     * responses like "[AT+C1\n+CME1,\"IP\",\"m3-world\"\nOK\nAT+CGA]" —
     * a mix of a CGDCONT response and CREG echo bytes from different polls,
     * never a clean "+CREG:" line. Clearing the whole buffer here (not just
     * the cursor) fixes this for every AT command that goes through this
     * function, not just CREG. */
    memset(modem->buff, 0, MODEM_RX_BUFFER_SIZE);
    sx_uart_flush(&modem->uart);
    sx_uart_write(&modem->uart, (const uint8_t *)cmd->cmd, strlen(cmd->cmd));
    return 0;
}

void modem_poll(modem_t *modem, uint32_t timeStamp){
    /* waitElapsed lives in the modem_t instance itself (see modem.h), instead
     * of a static local variable, so that multiple modem instances (e.g.
     * more than one UART-attached modem on the same board in the future)
     * each track their own command timeout independently. A static local
     * here would silently share state across every modem_t, corrupting
     * timeout tracking as soon as a second instance exists. */
    if (!modem->isBusy) return;

    int available = sx_uart_available(&modem->uart);
    
    if(available > 0 && (modem->buff_id + available) < MODEM_RX_BUFFER_SIZE){
        
        int read = sx_uart_read(&modem->uart, (uint8_t *)modem->buff + modem->buff_id, available, 10);
        if(read > 0){
            log_debug(TAG, "Read : %d bytes", read);
            log_debug(TAG,"Data : %s",modem->buff+modem->buff_id);
            log_print_hex(LOGGER_DEBUG,TAG,modem->buff+modem->buff_id,read);
            modem->buff_id += read;
            modem->waitElapsed = 0;

            if(modem->cmd->res_success && strstr(modem->buff, modem->cmd->res_success)){
                modem->isBusy = 0;
                modem->waitElapsed = 0;
                log_debug(TAG, "Command success: [%s]", modem->buff);
                if(modem->cmd->callback)
                    modem->cmd->callback(modem, modem->buff, MODEM_RESPONSE_SUCCESS, modem->cmd->arg);
                return;
            }
            else if(modem->cmd->res_fail && strstr(modem->buff, modem->cmd->res_fail)){
                modem->isBusy = 0;
                modem->waitElapsed = 0;
                log_debug(TAG, "Command fail: [%s]", modem->buff);
                if(modem->cmd->callback)
                    modem->cmd->callback(modem, modem->buff, MODEM_RESPONSE_FAIL, modem->cmd->arg);
                return;
            }
        }
    }

    modem->waitElapsed += timeStamp;
    if(modem->waitElapsed >= modem->timeOut){
        modem->isBusy = 0;
        modem->waitElapsed = 0;
        log_error(TAG, "TIMEOUT response: [%s]", (modem->buff_id > 0) ? modem->buff : "NULL");
        if(modem->cmd->callback)
            modem->cmd->callback(modem, NULL, MODEM_RESPONSE_TIMEOUT, modem->cmd->arg);
    }
}