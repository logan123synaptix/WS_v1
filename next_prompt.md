HANDOFF — FIX modem_poll() res_success + BUG TREO Ở AT+HTTPACTION (CHƯA XONG)

Viết giữa chừng phiên, dừng lại vì cần xác nhận 1 điểm với người dùng
trước khi đào tiếp (đợi đủ 121s hay chưa). Đọc kỹ trước khi tiếp tục.

============================================================
TRẠNG THÁI GIT — QUAN TRỌNG, LÀM NGAY ĐẦU PHIÊN SAU
============================================================
CHỈ 1 FILE bị sửa, CHƯA COMMIT/PUSH (container không có git
credential, không tự push được):
    SynaptiX_FDK/components/modules/modem/modem.c

Nội dung đầy đủ file đã sửa được dán nguyên văn ở cuối tài liệu này
(mục "NỘI DUNG ĐẦY ĐỦ modem.c ĐÃ SỬA"). NGƯỜI DÙNG ĐÃ CÓ FILE NÀY
TRÊN MÁY THẬT (đã build + test được, xem log dưới) — nhưng nếu
container phiên sau bị reset và không thấy thay đổi này qua git,
PHẢI hỏi người dùng xác nhận máy họ đang giữ bản nào, KHÔNG tự ý
ghi đè hoặc giả định.

============================================================
BỐI CẢNH — TẠI SAO CÓ THAY ĐỔI NÀY
============================================================
Đang test tính năng FOTA qua HTTP (a7677s_http.c, test_http.c) trên
nhánh ft/fota_ws. Phát hiện qua log thật (không phải suy đoán) rằng
modem_poll() (SynaptiX_FDK/components/modules/modem/modem.c) có bug
gốc: dùng strstr(modem->buff, res_success) để quyết định 1 lệnh AT
đã thành công — với các res_success dạng PREFIX của 1 dòng URC có số
liệu thay đổi (cụ thể: "+HTTPACTION: 0," — datalen/statuscode thay
đổi mỗi lần, không thể là literal cố định), strstr() match NGAY khi
mới nhận được vài byte đầu của dòng, TRƯỚC KHI phần còn lại
(datalen/statuscode) kịp về qua UART — dẫn tới cb_http_action() nhận
buffer bị cắt cụt kiểu "+HTTPACTION: 0,2" (thay vì
"+HTTPACTION: 0,206,2048") và parse lỗi (AT_ERROR).

============================================================
LỊCH SỬ SỬA TRONG PHIÊN NÀY (3 LẦN, 2 LẦN ĐẦU ĐỀU GÂY REGRESSION)
============================================================
LẦN 1 (SAI — gây treo AT+CPIN?, AT trần, AT+UIMHOTSWAPLEVEL...):
  Kiểm tra ký tự NGAY SAU đoạn res_success đã match trong buffer,
  áp dụng cho MỌI res_success không phân biệt. Bug: với pattern như
  "\r\nOK\r\n" (đã TỰ đủ để xác nhận dòng hoàn chỉnh), ký tự "ngay
  sau" đó không thuộc về response thật — là byte rác/'\0' còn sót
  trong buffer nếu UART chưa gửi gì thêm. Toàn bộ lệnh AT cơ bản bị
  treo tới timeout.

LẦN 2 (SAI — gây treo AT+CMQTTTOPIC, MQTT publish fail):
  Sửa: chỉ chờ thêm ký tự khi res_success KHÔNG tự kết thúc bằng
  \r/\n. Bug: điều kiện này vô tình bắt luôn dấu ">" (data-entry
  prompt dùng bởi AT+CMQTTTOPIC, AT+CMQTTPUB, AT+CCERTDOWN — xem
  a7677s.c, nhiều chỗ gọi send_mqtt_dynamic(..., ">", ...)). Dấu ">"
  là tín hiệu ĐẦY ĐỦ tự thân, modem KHÔNG BAO GIỜ gửi \r\n theo sau
  nó (khác hẳn 1 dòng URC) — chờ mãi không bao giờ tới, treo tới
  timeout.

LẦN 3 (ĐÃ XÁC NHẬN ĐÚNG CHO MQTT — người dùng report "test mqtt ok
rồi"):
  Sửa lại: chỉ chờ thêm byte khi res_success VỪA không tự kết thúc
  bằng \r/\n, VỪA bắt đầu bằng '+' (tức thực sự là 1 tiền tố dòng
  URC như "+HTTPACTION: 0,", KHÔNG phải dấu ">"). Đã grep toàn bộ
  res_success trong a7677s.c VÀ a7677s_http.c để xác nhận bao phủ đủ
  3 loại pattern hiện có trong codebase:
    (a) "\r\nOK\r\n", "\r\n+CMQTTSTART: 0\r\n" kiểu — tự kết thúc
        bằng \r\n → tin ngay, không đổi hành vi so với trước khi có
        bug này.
    (b) ">" — không bắt đầu bằng '+' → tin ngay như (a), không chờ
        thêm.
    (c) "+HTTPACTION: 0," — DUY NHẤT case này bắt đầu bằng '+' VÀ
        không tự kết thúc bằng \r/\n → chờ thêm byte tới khi thấy
        \r hoặc \n ngay sau đoạn match.
  Test MQTT (test_lte_mqtt: publish/subscribe/connect) đã XÁC NHẬN
  OK trên board thật sau lần sửa này.

============================================================
VẤN ĐỀ CÒN LẠI — CHƯA GIẢI QUYẾT, CẦN LÀM NGAY ĐẦU PHIÊN SAU
============================================================
Test HTTP (test_http.c, gọi a7677s_http_get_range()) với log_debug
bật lên cho thấy log DỪNG LẠI (không có gì thêm) NGAY SAU dòng:

    [DEBUG]MODEM : Read : 26 bytes
    [DEBUG]MODEM : Data :
                          +HTTPACTION: 0,206,2048
    [DEBUG]MODEM : 0D 2B 48 54 54 50 41 43 54 49 4F 4E 3A 20 30 2C
                    32 30 36 2C 32 30 34 38 0D 0A

Đọc kỹ hex dump: dòng "+HTTPACTION: 0,206,2048" KẾT THÚC bằng
"0D 0A" (\r\n) — tức theo lý thuyết đã đủ điều kiện line_complete
với logic Lần 3 ở trên (match "+HTTPACTION: 0," rồi ký tự tiếp theo
trong buffer phải là '\r' — CẦN XÁC NHẬN LẠI: chuỗi số "206,2048"
nằm GIỮA "+HTTPACTION: 0," và "\r\n", nên next_char thực chất là
ký tự đầu của "206..." KHÔNG PHẢI \r — ĐÂY CÓ THỂ LÀ BUG THỨ 4,
CHƯA XÁC NHẬN, XEM PHÂN TÍCH BÊN DƯỚI).

>>> NGHI VẤN BUG MỚI (chưa xác nhận, việc đầu tiên cần làm phiên sau):
Logic Lần 3 chờ ký tự NGAY SAU đoạn "+HTTPACTION: 0," (14-15 ký tự)
phải là \r hoặc \n. Nhưng response thật là
"+HTTPACTION: 0,206,2048\r\n" — ký tự ngay sau "+HTTPACTION: 0," là
'2' (đầu của "206"), KHÔNG PHẢI \r. Với logic hiện tại, điều kiện
line_complete sẽ luôn FALSE cho response này (vì sau "0," luôn là
số liệu, không bao giờ là \r/\n ngay lập tức) — nghĩa là bug Lần 3
tuy đã fix đúng cho case "bị cắt cụt giữa dòng" nhưng có thể đã VÔ
TÌNH làm nó KHÔNG BAO GIỜ match được nữa khi dòng đã về ĐỦ, vì code
chỉ kiểm tra ĐÚNG 1 ký tự ngay sau prefix, không quét tiếp tới khi
gặp \r/\n thật sự ở cuối dòng.

ĐÂY LÀ VIỆC ĐẦU TIÊN CẦN LÀM Ở PHIÊN TIẾP THEO: đọc lại đoạn code
trong modem_poll() (dán nguyên văn bên dưới, dòng ~108-135), xác
nhận lại chính xác logic "next_char = match_pos[success_len]" đang
kiểm tra ký tự gì, so với vị trí thật của \r\n trong response đầy đủ
"+HTTPACTION: 0,206,2048\r\n". NHIỀU KHẢ NĂNG cần sửa thành: quét
từ vị trí match_pos + success_len TỚI KHI gặp \r hoặc \n (không chỉ
xem đúng 1 ký tự ngay sau), để chấp nhận số liệu biến đổi ở giữa.

KHÔNG ĐƯỢC GIẢ ĐỊNH TRƯỚC — cần đọc lại code thật + có thể cần thêm
log/test để xác nhận chắc chắn trước khi sửa, đúng quy tắc dự án.

Người dùng CHƯA XÁC NHẬN đã đợi đủ HTTP_TIMEOUT_ACTION_MS (121000ms
= 121 giây) sau dòng log cuối cùng hay chưa — nếu đợi đủ mà vẫn
không thấy log TIMEOUT nào từ modem.c, thì bug trên gần như chắc
chắn đúng (điều kiện line_complete không bao giờ true → isBusy
không bao giờ về 0 → nhưng vẫn phải timeout sau 121s vì waitElapsed
vẫn tăng bình thường ở nhánh không đọc được gì mới — NẾU VẪN KHÔNG
TIMEOUT SAU 121S THÌ CÓ BUG KHÁC NỮA, xem thêm phần "khả năng khác"
bên dưới). Hỏi lại người dùng ngay đầu phiên sau nếu chưa có câu trả
lời.

>>> KHẢ NĂNG KHÁC (chưa loại trừ, chỉ mới nghĩ tới, CHƯA ĐỌC CODE
XÁC NHẬN): nếu buffer 512 byte (MODEM_RX_BUFFER_SIZE) đầy hoặc gần
đầy do các lần đọc tích lũy không được reset đúng lúc trong chuỗi
HTTP nhiều bước (INIT->PARA_URL->PARA_SSL->PARA_HDR->ACTION, mỗi
bước gọi http_send_dynamic() riêng — CẦN XÁC NHẬN mỗi lần gọi có
thực sự reset buff_id/memset buff hay không, vì modem_send_command()
làm việc này nhưng CẦN XEM LẠI s_http_command[] có bị tái sử dụng
đúng cách giữa các bước không). CHƯA ĐỌC KỸ, chỉ là giả thuyết dự
phòng nếu giả thuyết chính (next_char sai vị trí) bị loại trừ.

============================================================
QUY TẮC BẮT BUỘC (kế thừa, không đổi)
============================================================
- RE-CLONE đầu phiên: git clone
  https://github.com/logan123synaptix/WS_v1.git, checkout ft/fota_ws.
- SAU KHI CLONE: áp lại đoạn code đã sửa trong modem.c (dán nguyên
  văn bên dưới) TRƯỚC KHI làm gì khác, vì thay đổi này CHƯA PUSH.
- KHÔNG tin log/mô tả cũ mà không tự đọc lại code thật.
- Không sửa code âm thầm — trình bày nghi vấn → hỏi → chỉ sửa sau
  khi có xác nhận rõ ràng. (Bài học từ 2 lần sửa sai trong phiên
  này: MỖI thay đổi ở modem_poll() phải được review kỹ theo TẤT CẢ
  các res_success pattern hiện có trong cả a7677s.c VÀ a7677s_http.c
  trước khi báo là xong — không chỉ test 1 luồng rồi kết luận.)
- Comment code tiếng Anh, trao đổi tiếng Việt.
- KHÔNG có compiler thật trong container — không build được. Người
  dùng tự build + flash + gửi log qua chat.
- Board test vật lý duy nhất: STM32H563RIV6.
- Log thật/phép đo tay LUÔN thắng datasheet khi có xung đột.
- KHÔNG dùng present_files/xuất patch file — trình chiếu code trực
  tiếp trong chat bằng view/tool xem file, để người dùng tự copy.

============================================================
NỘI DUNG ĐẦY ĐỦ modem.c ĐÃ SỬA (dán nguyên văn để copy lại nếu mất)
============================================================
File: SynaptiX_FDK/components/modules/modem/modem.c

```c
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
             * pre-2026-08-05 behavior.
             *
             * KNOWN OPEN ISSUE as of end of this session (see handoff doc,
             * "VAN DE CON LAI" section): for pattern (c) below, this only
             * checks the SINGLE byte immediately after the matched prefix.
             * For "+HTTPACTION: 0," that next byte is the first digit of
             * <statuscode> (e.g. '2' in "206"), NEVER '\r' or '\n' directly
             * - the real \r\n comes several bytes further, after
             * <statuscode>,<datalen>. This condition may need to become a
             * scan-forward-until-\r-or-\n instead of a single-byte check.
             * NOT YET CONFIRMED against a repro that proves the callback
             * really never fires (vs. just being slow) - see handoff doc
             * before changing this. */
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
                    char next_char = match_pos[success_len];
                    line_complete = (next_char == '\r' || next_char == '\n');
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
```

============================================================
LOG THAM KHẢO — TEST HTTP TREO (bật log_debug), DÙNG ĐỂ ĐỐI CHIẾU
KHI SỬA LẠI
============================================================
(Chỉ đoạn cuối cùng trước khi treo, xem log đầy đủ trong lịch sử
chat nếu cần thêm ngữ cảnh — KHÔNG có trong file này để tránh phình
tài liệu.)

    [DEBUG]A7677S_HTTP : HTTP CMD: AT+HTTPACTION=0
    [DEBUG]MODEM : Send command AT+HTTPACTION=0
    [DEBUG]MODEM : Read : 8 bytes
    [DEBUG]MODEM : Data : AT+HTTPA
    [DEBUG]MODEM : 41 54 2B 48 54 54 50 41
    [DEBUG]MODEM : Read : 14 bytes
    [DEBUG]MODEM : Data : CTION=0
    OK
    [DEBUG]MODEM : 43 54 49 4F 4E 3D 30 0D 0D 0A 4F 4B 0D 0A
    [DEBUG]MODEM : Read : 1 bytes
    [DEBUG]MODEM : Data :
    [DEBUG]MODEM : 0D
    [DEBUG]MODEM : Read : 26 bytes
    [DEBUG]MODEM : Data :
                          +HTTPACTION: 0,206,2048
    [DEBUG]MODEM : 0A 2B 48 54 54 50 41 43 54 49 4F 4E 3A 20 30 2C
                    32 30 36 2C 32 30 34 38 0D 0A
    (log dừng lại đây, không có gì thêm - CHƯA XÁC NHẬN người dùng
    đã đợi đủ 121 giây - HAI VIỆC CẦN LÀM NGAY ĐẦU PHIÊN SAU, THEO
    THỨ TỰ:
      1. Hỏi người dùng đã đợi đủ 121s (HTTP_TIMEOUT_ACTION_MS) hay
         chưa - nếu chưa, yêu cầu đợi/test lại trước khi kết luận
         gì thêm.
      2. Đọc lại logic next_char trong modem_poll() (xem mục "NGHI
         VẤN BUG MỚI" ở trên), xác nhận hoặc loại trừ giả thuyết
         "chỉ kiểm tra đúng 1 ký tự ngay sau prefix, không quét tới
         \r\n thật" bằng cách trace tay qua ví dụ log này.)