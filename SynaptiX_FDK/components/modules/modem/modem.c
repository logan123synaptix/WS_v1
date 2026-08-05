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

            /* Bug fix (2026-08-05): a bare strstr() match is not enough for
             * res_success patterns that end mid-line without a trailing
             * "\r\n" of their own (e.g. a7677s_http.c's cb_http_action uses
             * "+HTTPACTION: 0," as a prefix, since the following
             * <statuscode>,<datalen> values vary and cannot be a fixed
             * literal). Without this check, UART bytes arriving in small
             * batches let strstr() match the moment just the prefix has
             * landed (confirmed on real hardware: buffer content
             * "+HTTPACTION: 0,2" matched and fired the callback before the
             * rest of "06,2048\r\n" arrived), causing the callback to parse
             * a truncated line.
             *
             * Follow-up fix #1 (same day): checking the byte immediately
             * AFTER every matched res_success broke every existing "\r\nOK
             * \r\n"-style command (that trailing byte isn't part of the
             * match, it's whatever's next in the buffer, usually '\0').
             * Fixed by trusting patterns that already end in '\r'/'\n'
             * immediately, only waiting for more bytes when the pattern
             * itself doesn't self-terminate.
             *
             * Follow-up fix #2 (same day): that "doesn't self-terminate"
             * bucket also caught ">" - the data-entry prompt used by
             * AT+CMQTTTOPIC, AT+CMQTTPUB, AT+CCERTDOWN, etc. (see a7677s.c,
             * many call sites). ">" is a complete signal on its own; the
             * modem sends it and then waits for the MCU to write raw data,
             * it never follows ">" with "\r\n" the way a URC line would.
             * Waiting for that never-coming "\r\n" broke MQTT publish on
             * real hardware (confirmed: AT+CMQTTTOPIC timed out every time
             * after fix #1's logic, even restricted to non-self-terminating
             * patterns). The actual distinguishing feature of the ORIGINAL
             * bug (HTTPACTION) is that it's a *prefix of a URC line* -
             * URC lines in this codebase always start with "+" and are
             * followed by variable data then "\r\n". ">" is not a URC line
             * prefix, it has nothing after it to wait for. So: only apply
             * the wait-for-more-bytes check to patterns that (a) don't
             * already end in '\r'/'\n', AND (b) start with '+' (i.e. are
             * genuinely a partial URC-line prefix like "+HTTPACTION: 0,").
             * Everything else (">", and any future single-char/non-URC
             * prompt) is trusted on bare strstr() match, exactly like
             * pre-2026-08-05 behavior. */
            char *match_pos = modem->cmd->res_success ? strstr(modem->buff, modem->cmd->res_success) : NULL;
            if(match_pos){
                size_t success_len = strlen(modem->cmd->res_success);
                char last_char_of_pattern = success_len > 0 ? modem->cmd->res_success[success_len - 1] : '\0';
                int pattern_self_terminated = (last_char_of_pattern == '\r' || last_char_of_pattern == '\n');
                int pattern_is_urc_prefix = (success_len > 0 && modem->cmd->res_success[0] == '+');
                int line_complete;
                if(pattern_self_terminated || !pattern_is_urc_prefix){
                    line_complete = 1;
                } else {
                    /* Bug fix (2026-08-05, follow-up #3): a partial-URC
                     * prefix like "+HTTPACTION: 0," is followed by
                     * VARIABLE data (<statuscode>,<datalen>, e.g. "206,2048")
                     * BEFORE the line's real "\r\n" - checking only the
                     * single byte right after the matched prefix (as the
                     * previous version of this fix did) checks the first
                     * digit of that variable data, which is never '\r'/'\n'
                     * itself, so line_complete was always false and every
                     * HTTPACTION call timed out even once the full line
                     * (with trailing \r\n) had genuinely arrived - confirmed
                     * on real hardware: TIMEOUT log showed the complete
                     * "+HTTPACTION: 0,206,2048\r\n" sitting in the buffer.
                     * Fix: scan forward from the end of the matched prefix,
                     * byte by byte, until a '\r' or '\n' is found - bounded
                     * by modem->buff_id (how many bytes have actually been
                     * received so far), never reading past valid data into
                     * the unwritten remainder of the fixed-size buff[]
                     * array. */
                    size_t scan_pos = (size_t)(match_pos - modem->buff) + success_len;
                    line_complete = 0;
                    while(scan_pos < modem->buff_id){
                        char c = modem->buff[scan_pos];
                        if(c == '\r' || c == '\n'){
                            line_complete = 1;
                            break;
                        }
                        scan_pos++;
                    }
                }
                if(line_complete){
                    modem->isBusy = 0;
                    modem->waitElapsed = 0;
                    log_debug(TAG, "Command success: [%s]", modem->buff);
                    if(modem->cmd->callback)
                        modem->cmd->callback(modem, modem->buff, MODEM_RESPONSE_SUCCESS, modem->cmd->arg);
                    return;
                }
                /* Prefix matched but the line hasn't finished arriving yet -
                 * fall through without resetting isBusy/waitElapsed, so the
                 * next modem_poll() tick reads more bytes and re-checks.
                 * waitElapsed was already reset to 0 above (line 66) since
                 * we did receive new bytes this tick, so the overall command
                 * timeout is not affected by this wait. */
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