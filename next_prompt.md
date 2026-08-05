HANDOFF v2 — FIX modem_poll() res_success (XONG) + BUG UART CONTENTION
Ở HTTP_STATE_READ_RAW (ĐÃ CHẨN ĐOÁN XONG, CHƯA CODE, ĐÃ CHỐT HƯỚNG SỬA)

Đây là bản v2, THAY THẾ HANDOFF v1 (HANDOFF_2026-08-05_modem_poll_fix.md)
— v1 còn 1 nghi vấn CHƯA XÁC NHẬN (bug "next_char sai vị trí"), v2 này
đã XÁC NHẬN nghi vấn đó ĐÚNG, đã sửa xong (lần sửa thứ 4), và đã tìm
ra + chẩn đoán xong 1 bug HOÀN TOÀN KHÁC nằm sau đó trong chuỗi HTTP
(UART contention). Đọc kỹ toàn bộ tài liệu này trước khi code tiếp,
KHÔNG cần đọc lại v1 (mọi thông tin còn giá trị đã được mang sang đây).

============================================================
TRẠNG THÁI GIT — QUAN TRỌNG, LÀM NGAY ĐẦU PHIÊN SAU
============================================================
CHỈ 1 FILE bị sửa, CHƯA COMMIT/PUSH (container không có git
credential, không tự push được):
    SynaptiX_FDK/components/modules/modem/modem.c

Nội dung đầy đủ file đã sửa (lần sửa thứ 4, ĐÃ XÁC NHẬN ĐÚNG trên
board thật — xem log ở mục "TIẾN ĐỘ ĐÃ XÁC NHẬN" bên dưới) được dán
nguyên văn ở cuối tài liệu này. Người dùng đã có bản này trên máy
thật (build + test rồi). Nếu container phiên sau bị reset và git
không thấy thay đổi này, PHẢI hỏi người dùng xác nhận máy họ đang
giữ bản nào, KHÔNG tự ý ghi đè/giả định.

CHƯA CÓ THAY ĐỔI NÀO trong a7677s_http.c hay modem.h cho bug UART
contention (mục dưới) — đó là việc CẦN LÀM tiếp theo, đã chốt hướng
với người dùng nhưng CHƯA CODE DÒNG NÀO.

============================================================
TIẾN ĐỘ ĐÃ XÁC NHẬN (KHÔNG CẦN LÀM LẠI)
============================================================
1. modem_poll() res_success bug — ĐÃ SỬA XONG, ĐÃ TEST OK TRÊN BOARD
   THẬT cho cả 2 luồng:
   - test_lte_mqtt (MQTT connect/publish/subscribe) — người dùng xác
     nhận "test mqtt thì ok rồi".
   - test_http's AT+HTTPACTION parse — log thật cho thấy
     "AT+HTTPACTION: status=206 datalen=2048" in ra liên tục đúng,
     không còn timeout ở bước này nữa.
   Xem mục "LỊCH SỬ SỬA modem_poll() (4 LẦN)" bên dưới để hiểu rõ
   TẠI SAO logic cuối cùng lại như vậy — quan trọng nếu cần sửa thêm
   lần nữa, tránh lặp lại 3 lần sai trước đó.

2. Đã loại trừ giả thuyết "file .bin chứa chuỗi text trùng AT
   command" — người dùng đã cung cấp hex dump 500 byte đầu file
   TrackingFirmWare.bin thật (118504 bytes), xác nhận là ARM vector
   table chuẩn (00 00 0A 20 = initial SP, loạt 45 4B 01 08 lặp lại =
   default handler address), KHÔNG chứa bất kỳ chuỗi ASCII nào giống
   "AT+HTTP...". Bug KHÔNG nằm ở nội dung file.

============================================================
BUG MỚI ĐÃ CHẨN ĐOÁN XONG — UART CONTENTION (CHƯA SỬA)
============================================================
TRIỆU CHỨNG (xem log đầy đủ trong lịch sử chat nếu cần, tóm tắt ở
đây): sau khi AT+HTTPACTION parse đúng (status=206, datalen=2048),
bước AT+HTTPREAD=0,400 bị gửi LẶP LẠI HÀNG CHỤC LẦN với offset LUÔN
LUÔN LÀ 0 (không bao giờ tăng), mỗi lần chỉ đọc được 1-12 byte lẻ
tẻ qua UART, dữ liệu trong modem->buff (KHÔNG PHẢI s_http.data[])
tích lũy thành một mớ hỗn độn xen kẽ giữa dữ liệu nhị phân thật của
file (byte ngẫu nhiên như 'Т', 'h', 'w', 'K'...) VÀ chuỗi echo lệnh
AT+HTTPREAD dạng text lặp đi lặp lại nhiều lần liền nhau
("AT+HTTPAT+HTTPREAD=AT+HTTPREADAT+HT..."). Cuối cùng
RAW_WAIT_ECHO_OK không bao giờ thấy "OK\r\n" liền mạch, timeout sau
HTTP_READ_RAW_TIMEOUT_MS=5000ms, abort_read_raw() được gọi, toàn bộ
range bị fail và test_http.c gọi lại a7677s_http_get_range() từ đầu
(offset không đổi vì range đó chưa từng thành công) — khớp hoàn toàn
với log: mỗi vòng lặp lớn lại thấy 1 chuỗi AT+HTTPACTION mới chạy
lại từ đầu.

NGUYÊN NHÂN GỐC ĐÃ XÁC NHẬN BẰNG CÁCH ĐỌC CODE THẬT (không suy
đoán):

  a7677s_poll() (a7677s.c, dòng ~442-460) — hàm này được gọi MỖI
  TICK qua modem_handle_poll() (test_http.c gọi modem_handle_poll()
  mỗi tick trong test_http_poll()) — LUÔN LUÔN gọi modem_poll(pModem
  (dce), ts) ĐẦU TIÊN, VÔ ĐIỀU KIỆN, không kiểm tra xem có module
  con nào khác (như a7677s_http.c) đang tự ý chiếm UART hay không.

  start_read_raw() (a7677s_http.c, dòng ~619-639) set
  pModem(s_http.dce)->isBusy = 1 THỦ CÔNG để "báo hiệu bận" — nhưng
  modem_poll() chỉ kiểm tra "if (!modem->isBusy) return;" rồi VẪN
  TIẾP TỤC đọc UART bình thường nếu isBusy==1 (đây vốn là hành vi
  ĐÚNG cho command bình thường qua modem_send_command() — modem_poll
  () CẦN đọc UART để hoàn thành command đó). Nhưng với raw-read,
  modem->cmd VẪN CÒN TRỎ TỚI LỆNH AT+HTTPACTION CŨ (chưa có lệnh mới
  nào được gán qua modem_send_command() cho AT+HTTPREAD, vì
  start_read_raw() CỐ TÌNH bypass modem_send_command()/modem_poll()
  bằng cách tự sx_uart_write() thẳng — xem chính comment trong code
  giải thích lý do bypass, vẫn đúng và cần thiết vì response HTTPREAD
  chứa byte nhị phân bất kỳ, modem_poll()'s strstr()-based framing
  không an toàn cho việc đó).

  KẾT QUẢ: modem_poll() (qua a7677s_poll()) VÀ a7677s_http_poll()
  (raw-read loop, đọc 1 byte/tick qua sx_uart_read(uart, &byte, 1, 0)
  ở dòng ~719) CÙNG ĐỌC CHUNG 1 UART TRONG CÙNG 1 TICK — tranh giành
  từng byte một cách không xác định (phụ thuộc thứ tự gọi trong
  test_http_poll(): modem_handle_poll() gọi TRƯỚC, a7677s_http_poll()
  gọi SAU — nên modem_poll() luôn "ăn" byte trước, a7677s_http_poll()
  chỉ còn lại phần thừa mỗi tick, xé lẻ dữ liệu nhị phân thành từng
  mảnh 1-12 byte rải rác, khớp chính xác với log thật).

============================================================
HƯỚNG SỬA ĐÃ CHỐT VỚI NGƯỜI DÙNG (CHƯA CODE)
============================================================
Đã trình bày 2 hướng, người dùng đồng ý đi theo HƯỚNG 2:

HƯỚNG 1 (KHÔNG CHỌN): sửa a7677s_poll() để tự kiểm tra
a7677s_http_is_busy(dce) trước khi gọi modem_poll(). BỊ LOẠI vì tạo
dependency ngược — a7677s.c (module modem tổng quát) phải biết về
a7677s_http.c (module con chỉ lo HTTP), đi ngược nguyên tắc tách
biệt đã ghi rõ trong chính file header của a7677s_http.c. Không mở
rộng được nếu sau này có module khác cũng cần raw UART access.

HƯỚNG 2 (ĐÃ CHỌN): thêm 1 cờ MỚI trong struct modem (modem.h), ví dụ
tên "rawIoActive" (uint8_t, cùng kiểu với isBusy/isReady/
hasPowerPin - xem struct hiện tại ở cuối tài liệu). Cụ thể:

  1. modem.h: thêm field uint8_t rawIoActive; vào struct modem,
     cạnh isBusy. Khởi tạo = 0 trong modem_init() (giống isBusy/
     isReady).

  2. modem_poll(): thêm điều kiện SỚM NHẤT có thể trong hàm - ví dụ
     ngay sau "if (!modem->isBusy) return;" thêm dòng
     "if (modem->rawIoActive) return;" - để KHÔNG đọc UART, không
     làm gì cả, khi cờ này bật. (CẦN XÁC NHẬN LẠI vị trí chính xác
     khi code thật - đọc lại modem_poll() hiện tại, dán ở cuối tài
     liệu này, để chọn đúng chỗ chèn, không đoán trước.)

  3. a7677s_http.c's start_read_raw(): set CẢ HAI cờ - giữ nguyên
     "pModem(s_http.dce)->isBusy = 1;" (để các nơi khác trong hệ
     thống vẫn biết modem "bận" nói chung, không đổi ý nghĩa cũ) VÀ
     THÊM "pModem(s_http.dce)->rawIoActive = 1;" (cờ mới, để chặn
     modem_poll() khỏi tự đọc UART).

  4. a7677s_http.c's finish_read_raw_chunk() VÀ abort_read_raw():
     CẢ HAI hàm này đều clear isBusy = 0 hiện tại - cần thêm clear
     rawIoActive = 0 CÙNG LÚC ở cả hai chỗ (không được sót 1 trong 2
     - abort_read_raw() dễ bị quên vì nó là đường lỗi, ít được để ý
     hơn happy path).

TẠI SAO CHỌN HƯỚNG NÀY: khái niệm "ai đang chiếm quyền đọc UART" đúng
ra thuộc về tầng modem_t - đây đúng là nơi lưu trạng thái đó, cùng
logic với cách isBusy đã làm cho command bình thường. Cờ mới tách
biệt rõ 2 khái niệm khác nhau: "bận vì 1 command AT thường"
(modem_poll() TỰ xử lý, cần đọc UART) vs "bận vì module con đang TỰ
đọc UART trực tiếp" (modem_poll() PHẢI tránh xa hoàn toàn). Bất kỳ
module con nào khác trong tương lai (không chỉ HTTP) cần raw UART
access đều dùng lại được cùng cơ chế mà không cần sửa a7677s.c/
a7677s_poll() thêm lần nào nữa.

VIỆC CẦN LÀM ĐẦU TIÊN Ở PHIÊN TIẾP THEO: implement đúng 4 bước trên,
theo ĐÚNG THỨ TỰ (đọc lại code thật của modem.h/modem.c/a7677s_http.c
TRƯỚC KHI sửa - đừng giả định vị trí dòng từ tài liệu này, code có
thể đã đổi). Sau khi sửa xong, PHẢI kiểm tra lại (như đã làm 3 lần
trước với res_success) rằng KHÔNG có module nào khác trong codebase
đang tự set modem->isBusy = 1 thủ công theo kiểu tương tự
start_read_raw() mà lại KHÔNG set rawIoActive - grep toàn bộ
"->isBusy = 1" trong cả a7677s.c VÀ a7677s_http.c để xác nhận
start_read_raw() là NƠI DUY NHẤT làm việc này theo kiểu bypass
modem_send_command(), nếu không sẽ tạo ra 1 bug tương tự ở chỗ khác
mà không hay biết.

SAU KHI SỬA XONG: yêu cầu người dùng build + chạy lại test_http,
xem log_debug, xác nhận:
  - AT+HTTPREAD=0,400 KHÔNG còn bị gửi lặp lại với offset=0 mãi mãi
    nữa - offset phải TĂNG dần qua các lần gọi trong CÙNG 1 range
    (0 -> 400 -> 800 -> ... theo A7677S_HTTP_READ_CHUNK_SIZE, xem
    a7677s_http.h để biết giá trị chính xác).
  - modem->buff (log "Command success: [...]") KHÔNG còn xen lẫn dữ
    liệu nhị phân của file .bin nữa - vì modem_poll() giờ hoàn toàn
    không đọc UART trong lúc HTTP_STATE_READ_RAW đang chạy.
  - Ít nhất 1 range (lý tưởng là cả 5 range trong TEST_HTTP_MAX_RANGES)
    báo "[range N] OK status=206 data_len=...".
  - Log hex đầu/cuối mỗi range OK (test_http.c's on_range_done() đã
    tự dump 16 byte đầu) khớp với nội dung thật của file .bin (so
    với hex dump người dùng đã cung cấp cho range 0: phải bắt đầu
    bằng "00 00 0A 20 F5 4A 01 08...").

NẾU VẪN CÒN BUG SAU KHI SỬA: KHÔNG giả định thêm, quay lại đọc log
debug thật, so khớp offset/hex dump, và cân nhắc liệu còn nguồn tranh
chấp UART nào khác chưa lường tới (ví dụ urc_poll() trong a7677s.c,
dòng ~458-460 của a7677s_poll() - CHƯA KIỂM TRA hàm này có tự đọc
UART độc lập hay không, CHỈ MỚI XÁC NHẬN nó bị chặn bởi
!modem_is_busy() nên với isBusy=1 hiện tại nó ĐÃ không chạy - cần
xác nhận điều này VẪN ĐÚNG sau khi thêm rawIoActive, vì logic if
(!modem_is_busy(...)) không đổi, chỉ modem_poll() nội bộ đổi, nên
urc_poll() không bị ảnh hưởng - NHƯNG NÊN XÁC NHẬN LẠI, không giả
định).

============================================================
LỊCH SỬ SỬA modem_poll() res_success (4 LẦN, 3 LẦN ĐẦU SAI)
============================================================
Giữ nguyên từ v1, tóm tắt lại để không phải đọc file cũ:

LẦN 1 (SAI): kiểm tra ký tự NGAY SAU res_success cho MỌI pattern.
  Gãy: "\r\nOK\r\n" và tương tự - ký tự sau match không thuộc response
  thật (rác/'\0'), treo mọi lệnh AT cơ bản (AT+CPIN?, AT trần...).

LẦN 2 (SAI): chỉ chờ thêm khi res_success KHÔNG tự kết thúc \r/\n.
  Gãy: bắt luôn dấu ">" (data-entry prompt của AT+CMQTTTOPIC/PUB/
  CCERTDOWN) - "> " không bao giờ có \r\n theo sau, treo MQTT
  publish.

LẦN 3 (SAI, nhưng đúng cho MQTT): thêm điều kiện phải bắt đầu bằng
  '+' (tức prefix URC thật) MỚI chờ thêm. MQTT test OK. NHƯNG: chỉ
  kiểm tra ĐÚNG 1 KÝ TỰ ngay sau prefix - với "+HTTPACTION: 0," ký
  tự đó luôn là chữ số đầu của <statuscode> (vd '2' của "206"),
  KHÔNG BAO GIỜ là \r/\n trực tiếp vì còn <statuscode>,<datalen>
  chen giữa trước \r\n thật. Treo HTTPACTION dù response đã về đủ -
  XÁC NHẬN bằng log TIMEOUT chứa nguyên vẹn
  "+HTTPACTION: 0,206,2048\r\n" trong buffer.

LẦN 4 (ĐÚNG, ĐÃ TEST TRÊN BOARD THẬT): thay kiểm tra 1 ký tự bằng
  vòng quét byte-by-byte từ ngay sau prefix TỚI KHI gặp \r hoặc \n,
  giới hạn trong modem->buff_id (số byte thực sự đã nhận, không đọc
  tràn vào phần buffer chưa ghi). Xem code đầy đủ ở cuối tài liệu.

BÀI HỌC: mỗi lần sửa modem_poll() PHẢI được kiểm tra lại theo TẤT CẢ
res_success pattern hiện có trong CẢ a7677s.c VÀ a7677s_http.c (grep
toàn bộ, không chỉ đoán) TRƯỚC KHI báo là xong - test 1 luồng OK
không có nghĩa luồng khác cũng OK.

============================================================
QUY TẮC BẮT BUỘC (kế thừa, không đổi)
============================================================
- RE-CLONE đầu phiên: git clone
  https://github.com/logan123synaptix/WS_v1.git, checkout ft/fota_ws.
- SAU KHI CLONE: áp lại đoạn code đã sửa trong modem.c (dán nguyên
  văn bên dưới) TRƯỚC KHI làm gì khác, vì thay đổi này CHƯA PUSH.
- KHÔNG tin log/mô tả cũ mà không tự đọc lại code thật.
- Không sửa code âm thầm — trình bày nghi vấn → hỏi → chỉ sửa sau
  khi có xác nhận rõ ràng.
- Comment code tiếng Anh, trao đổi tiếng Việt.
- KHÔNG có compiler thật trong container — không build được. Người
  dùng tự build + flash + gửi log qua chat.
- Board test vật lý duy nhất: STM32H563RIV6.
- Log thật/phép đo tay LUÔN thắng datasheet khi có xung đột.
- KHÔNG dùng present_files/xuất patch file — trình chiếu code trực
  tiếp trong chat bằng view/tool xem file, để người dùng tự copy.

============================================================
NỘI DUNG ĐẦY ĐỦ modem.c ĐÃ SỬA (dán nguyên văn để copy lại nếu mất)
LẦN SỬA THỨ 4 — ĐÃ XÁC NHẬN ĐÚNG TRÊN BOARD THẬT
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
     * an earlier command whenever the new response is shorter than the one
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
             * Follow-up fix #3 (same day, CONFIRMED CORRECT on real
             * hardware): a partial-URC prefix like "+HTTPACTION: 0," is
             * followed by VARIABLE data (<statuscode>,<datalen>, e.g.
             * "206,2048") BEFORE the line's real "\r\n" - checking only the
             * single byte right after the matched prefix (fix #2's version)
             * checks the first digit of that variable data, which is never
             * '\r'/'\n' itself, so line_complete was always false and every
             * HTTPACTION call timed out even once the full line (with
             * trailing \r\n) had genuinely arrived - confirmed on real
             * hardware: TIMEOUT log showed the complete
             * "+HTTPACTION: 0,206,2048\r\n" sitting in the buffer already.
             * Fix: scan forward from the end of the matched prefix, byte by
             * byte, until a '\r' or '\n' is found - bounded by
             * modem->buff_id (how many bytes have actually been received so
             * far), never reading past valid data into the unwritten
             * remainder of the fixed-size buff[] array. CONFIRMED WORKING:
             * subsequent test_http.c run showed
             * "AT+HTTPACTION: status=206 datalen=2048" logged correctly and
             * repeatedly, no more timeouts at this step. */
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
```

============================================================
THAM KHẢO — modem.h struct modem HIỆN TẠI (CHƯA CÓ rawIoActive)
Dán nguyên văn để biết chính xác chỗ cần thêm field mới ở phiên sau
============================================================
File: SynaptiX_FDK/components/modules/modem/modem.h

```c
#ifndef MODEM_H
#define MODEM_H
#ifdef __cplusplus
extern "C" {
#endif

#include "sx_uart.h"
#include "sx_gpio.h"
#include "cqueue.h"
#include <string.h>

#define MODEM_RX_BUFFER_SIZE 512

typedef struct modem modem_t;

typedef enum modem_response_st{
    MODEM_RESPONSE_SUCCESS = 0,
    MODEM_RESPONSE_FAIL,
    MODEM_RESPONSE_TIMEOUT
}modem_response_st_t;

typedef void (*modem_command_response_callback_t)(modem_t *modem, const char *response, modem_response_st_t res, void *arg);

typedef struct modem_command{
    const char *cmd;
    const char *res_success;
    const char *res_fail;
    modem_command_response_callback_t callback;
    void *arg; // callback
}modem_command_t;

struct modem
{
    /* data */
    char buff[MODEM_RX_BUFFER_SIZE];
    uint32_t buff_id;
    sx_uart_t uart;
    sx_gpio_t pwrPin;        /* PWRKEY line — every modem driver has this */
    sx_gpio_t powerPin;      /* VBAT cutoff transistor GPIO — optional,
                              * depends on board revision. Only valid to use
                              * when hasPowerPin is 1. */
    uint8_t hasPowerPin;     /* 1 if this board wires a VBAT cutoff for this
                              * modem, 0 otherwise. Must be explicitly set by
                              * the board init code (sx_board.c), never
                              * assumed. Drivers must check this flag before
                              * touching powerPin. */
    uint8_t isBusy;
    uint8_t isReady;
    uint32_t timeOut;
    uint32_t waitElapsed;    /* elapsed time accumulator for the current
                              * command timeout, tracked per-instance.
                              * Replaces the old "static uint32_t s_time"
                              * local in modem_poll(), which was unsafe with
                              * more than one modem instance. */
    uint32_t resID;
    modem_command_t *cmd;
};

void modem_init(modem_t *modem);
void modem_poll(modem_t *modem,uint32_t timeStamp);

//int modem_send_command(modem_t *modem, modem_command_t *cmd, char *response, int response_size,modem_command_response_callback_t callback,uint32_t timeout);
int modem_send_command(modem_t *modem, modem_command_t *cmd, uint32_t timeout);

static inline uint8_t modem_is_busy(modem_t *modem){
    return modem->isBusy;
}

static inline uint8_t modem_is_ready(modem_t *modem){
    return modem->isReady;
}

#ifdef __cplusplus
}
#endif
#endif // MODEM_H
```

============================================================
THAM KHẢO — a7677s_http.c CÁC HÀM LIÊN QUAN (start_read_raw,
finish_read_raw_chunk, abort_read_raw) - CHƯA SỬA, dán để biết vị
trí cần thêm rawIoActive ở phiên sau
============================================================
(Trích đoạn liên quan, không phải toàn file - xem toàn file thật khi
code, code có thể lệch số dòng so với lúc trích.)

```c
static void start_read_raw(void)
{
    uint32_t remaining = s_http.http_datalen - s_http.read_offset;
    uint32_t this_read = (remaining < A7677S_HTTP_READ_CHUNK_SIZE) ? remaining : A7677S_HTTP_READ_CHUNK_SIZE;

    snprintf(s_http_dyn_cmd_buf, sizeof(s_http_dyn_cmd_buf),
             "AT+HTTPREAD=%lu,%lu\r\n",
             (unsigned long)s_http.read_offset, (unsigned long)this_read);

    s_http.raw_substate         = RAW_WAIT_ECHO_OK;
    s_http.raw_chunk_len        = 0;
    s_http.raw_chunk_copied     = 0;
    s_http.raw_line_len         = 0;
    s_http.raw_state_elapsed_ms = 0;

    log_debug(TAG, "HTTP RAW CMD: %s", s_http_dyn_cmd_buf);
    pModem(s_http.dce)->isBusy = 1;   /* claim the channel - see file header comment above */
    /* TODO (phien sau): them pModem(s_http.dce)->rawIoActive = 1; o day */
    sx_uart_flush(&pModem(s_http.dce)->uart);
    sx_uart_write(&pModem(s_http.dce)->uart,
                   (const uint8_t *)s_http_dyn_cmd_buf, strlen(s_http_dyn_cmd_buf));
}

static void finish_read_raw_chunk(void)
{
    pModem(s_http.dce)->isBusy = 0;
    /* TODO (phien sau): them pModem(s_http.dce)->rawIoActive = 0; o day */

    if (s_http.read_offset < s_http.http_datalen) {
        start_read_raw();
        return;
    }

    s_http.state = HTTP_STATE_TERM;
    http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                       "\r\nOK\r\n", "\r\nERROR\r\n",
                       cb_http_term, HTTP_TIMEOUT_SHORT_MS);
}

static void abort_read_raw(const char *why)
{
    log_error(TAG, "AT+HTTPREAD (raw): %s at offset %lu", why, (unsigned long)s_http.read_offset);
    pModem(s_http.dce)->isBusy = 0;
    /* TODO (phien sau): them pModem(s_http.dce)->rawIoActive = 0; o day -
     * DE Y hon finish_read_raw_chunk() vi day la duong loi, hay bi quen */
    s_http.state = HTTP_STATE_TERM;
    http_send_dynamic(HTTP_CMD_TERM, "AT+HTTPTERM\r\n",
                       "\r\nOK\r\n", "\r\nERROR\r\n",
                       cb_http_term, HTTP_TIMEOUT_SHORT_MS);
}
```

============================================================
THAM KHẢO — a7677s_poll() (a7677s.c) - nơi gọi modem_poll() vô điều
kiện, nguồn gốc bug UART contention
============================================================
```c
static void a7677s_poll(void *ctx, uint32_t ts)
{
    a7677s_t *dce = (a7677s_t *)ctx;

    /* Always pump the underlying command/response state machine first, so
     * any in-flight AT command (probe or CPOF) gets its callback fired
     * before we act on power_state below. */
    modem_poll(pModem(dce), ts);

    /* URC scanner: only safe to read the UART for unsolicited lines while
     * no AT command is currently awaiting its response (modem_poll() above
     * owns the UART/modem->buff channel exclusively whenever isBusy is 1).
     * ... */
    if (!modem_is_busy(pModem(dce))) {
        urc_poll(dce);
    }

    /* ... (edge detection, unchanged, not relevant to this bug) ... */
}
```
Với cờ rawIoActive mới, KHÔNG cần sửa file này - modem_poll() tự
kiểm tra cờ ở đầu hàm và return sớm, a7677s_poll() không cần biết gì
về rawIoActive cả (đúng ý đồ tách biệt module).