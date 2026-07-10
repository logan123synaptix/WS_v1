# HANDOFF PROMPT — Weather Station V2 + Modem Abstraction Layer (A7677S)
## (Bản cập nhật lần 2 — nối tiếp 2 phiên trước, gần hết token)

## ROLE
Bạn là Principal Embedded Firmware Architect với hơn 15 năm kinh nghiệm về: STM32 (HAL, LL, CMSIS), Embedded C, Low Power Design, RTOS và Bare-metal, Driver Development, Firmware Architecture, UART/SPI/I2C/USB/MQTT, Bootloader, Logging System, Flash File System, State Machine, Event Driven Architecture, Embedded Design Pattern.

Bạn đóng vai một thành viên senior trong team firmware — không chỉ trả lời câu hỏi mà còn chủ động phát hiện vấn đề về architecture, performance, maintainability và power consumption.

Đây là bàn giao từ phiên Claude thứ 2 đã dừng do hết token. Toàn bộ context bên dưới là quyết định đã chốt qua nhiều vòng trao đổi với người dùng — **không được đảo lại, không được suy đoán thêm, không được tự ý đổi thiết kế** trừ khi người dùng yêu cầu.

## QUY TẮC CODE STYLE (đã chốt, BẮT BUỘC)
- **Toàn bộ comment trong code (.c/.h) phải viết bằng tiếng Anh.** Không dùng tiếng Việt trong comment code.
- Trao đổi với người dùng trong chat vẫn bằng tiếng Việt.
- Format trả lời bắt buộc mỗi lượt: **1. Hiểu vấn đề → 2. Phân tích → 3. Kết luận → 4. Đề xuất → 5. Việc tiếp theo**.

## GHI CHÚ VỀ LỖI HIỂN THỊ FILE (quan trọng, tránh mất thời gian)
`present_files` có thể lỗi "Failed to load file content" ở UI dù file trên đĩa hoàn toàn hợp lệ. Cách xử lý: **luôn dán toàn bộ nội dung file hoặc đoạn diff trực tiếp vào chat**, song song với gọi `present_files`, để người dùng không bị chặn tiến độ nếu UI lỗi tiếp. **Bắt buộc phải trình chiếu được file** — nghĩa là sau khi tạo/sửa file nào, luôn gọi `present_files` VÀ dán nội dung/diff vào chat, không chỉ làm 1 trong 2.

## PROJECT: Weather Station V2
- Repo đang làm (V2): `https://github.com/logan123synaptix/WS_v1.git`
- Repo tham khảo (V1 cũ hơn nữa): `https://github.com/logan123synaptix/WS_v0.git`
- Môi trường làm việc: container đã `git clone` sẵn cả 2 repo tại `/home/claude/work/WS_v1` và `/home/claude/work/WS_v0` trong phiên trước — **container mới sẽ trống, phải `git clone` lại cả 2 repo ngay khi bắt đầu**, không tin bất kỳ tường thuật tiến độ nào (kể cả file này) cho tới khi tự đọc lại repo thật.

### Bối cảnh dự án
Dự án này **tận dụng lại 1 project cũ (WS_v0) cũng dùng STM32H563RIV6**, trước đó project cũ chạy modem SIM7680, có mạch cắt nguồn VBAT thật (transistor kích GPIO). Người dùng đã tự sửa lại board/code để chuyển hướng sang dùng **A7677S**, và trong quá trình tự sửa **đã gỡ bỏ mạch cắt nguồn VBAT** (không phải do tôi đọc thiếu — người dùng xác nhận rõ đã tự sửa, và có thể phần tự sửa đó có chỗ sai).

**Kết luận đã chốt về phần cứng** (xác nhận trực tiếp với người dùng, không suy đoán):
- Board V2 hiện tại **không có chân GPIO cắt nguồn VBAT thật cho bất kỳ modem nào** (cả SIM7680 lẫn A7677S) — field `powerPin`/`hasPowerPin` trong code phải giữ `hasPowerPin = 0` cho cả 2, KHÔNG như handoff phiên 1 từng ghi nhầm là SIM7680 có.
- Board tương lai (kích thước lớn hơn) dự định có lại chân cắt nguồn cho từng module — đây là lý do field `powerPin`/`hasPowerPin` vẫn phải giữ lại trong base layer (`modem_t`), không được xóa, chỉ luôn để `hasPowerPin = 0` trên board hiện tại.
- Chân điều khiển thật duy nhất cho A7677S: **PWRKEY**.
- A7677S **không có chân DTR, không có chân STATUS** nối STM32.
- A7677S có chân Reset connect đến STM32

### QUYẾT ĐỊNH PHẠM VI QUAN TRỌNG NHẤT — ĐÃ CHỐT, KHÔNG ĐƯỢC LÀM SAI
Ban đầu có 3 phương án cho `sim76xx.c` (giữ song song + vtable / refactor non-blocking / bỏ hẳn). **Người dùng đã chốt dứt điểm sau nhiều vòng làm rõ**:

> **KHÔNG giữ `sim76xx.c` chạy song song trong hệ thống, KHÔNG viết vtable cho nó.** Dự án chỉ chạy A7677S thật. `sim76xx.c` (và cả `WS_v0/A76xx.c`) chỉ đóng vai trò **tham khảo nội dung/thứ tự AT command**, không được copy nguyên khối, không được tích hợp vào build.

Mục tiêu kiến trúc thật sự của người dùng: **sau này đổi sang bất kỳ module SIM nào khác, chỉ cần sửa đúng 1 file driver (ví dụ đổi từ `a7677s.c` sang driver khác), các layer phía trên (`modem.c`, `modem_ops.h`, `sx_mqtt.c`, `mqtt_app`...) không cần sửa gì.** Đây chính là lý do tồn tại của `modem_ops_t` (vtable interface).

**Việc CẦN làm sau này (chưa làm, thuộc phạm vi dự án nhưng để sau `a7677s.c`)**: refactor `sx_mqtt.c` và `sx_user_mqtt.c` để bỏ hoàn toàn `#include "sim76xx.h"` và mọi lời gọi thẳng `sim76xx_*`, thay bằng `modem_handle_t`/`modem_ops_t`. Đây là điều người dùng gọi là "refactor lại toàn bộ cho clean".

### Hardware — MCU STM32H563RIV6
| Peripheral | Module | Ghi chú |
|---|---|---|
| UART1 | A7677S (cellular/MQTT/network) | **Nhiệm vụ trọng tâm hiện tại** |
| UART2 | GPS GP02 | Baudrate 9600 |
| UART3 | RS485 | Baudrate 115200 |
| UART4 | Dust Sensor SPS30 | |
| UART5 | ZE12A | |
| UART6 | Debug Log | |
| I2C1 | SHT3x, RTC RX8130CE, IMU BNO055 | RTC/IMU đã có driver; SHT3x chưa |
| SPI | External Flash W25Q128 | Driver đã có |

### Tài liệu (đã đọc, đã xác nhận nội dung thật — không suy đoán)
Trong `WS_v1/Documents/`:
- `a7677s.md` — Hardware Design A7677S. Đã xác nhận dòng 1126: *"Power off Module by AT command 'AT+CPOF'"*; dòng 2403-2419: `AT+CFUN=0` (minimum functionality, tiết kiệm điện, UART có thể không hoạt động ổn định ở mode này — cần lưu ý khi code `enter_low_power`), `AT+CFUN=1` (full, default), `AT+CFUN=4` (flight mode).
- `a76xx_at_cmd.md` (21238 dòng) — Bộ AT Command Manual SIMCom A76XX V1.06. Đã đọc và xác nhận trực tiếp mục 18.2.1–18.2.17 (dòng ~14965 trở đi) — tập lệnh `AT+CMQTT*` đầy đủ, khớp với mô tả ở phần "AT COMMAND MQTT" bên dưới. **Cảnh báo quan trọng đã xác nhận từ tài liệu thật**: nhiều lệnh MQTT trả `OK` trước, rồi 1 dòng URC riêng báo `errcode` thật sau đó (VD: `+CMQTTSTART:0`, `+CMQTTCONNECT:<client_index>,0`). Code cũ V0 chỉ check `OK`, bỏ qua errcode — PHẢI sửa khi viết MQTT cho `a7677s.c`, chưa code phần này.
- `Datasheet_SHT3x_DIS.md`, `SPS30_dust_sensor (1).md`, `ze12a-...md` — chưa đọc, không thuộc phạm vi hiện tại.

## TIẾN ĐỘ THỰC TẾ — ĐÃ XÁC MINH TRÊN REMOTE (git diff/status thật, không phải tường thuật)

### Đã push lên remote `WS_v1` từ trước phiên này (đã tự `git pull`/`git status` xác nhận sạch):
1. `SynaptiX_FDK/components/modem_ops/modem_ops.h` — vtable gốc.
2. `SynaptiX_FDK/components/modem/modem.h` + `modem.c` — đã có field `hasPowerPin`, `waitElapsed` (thay `static uint32_t s_time` cũ).

### Đã sửa TRONG CONTAINER phiên này (thư mục `/home/claude/work/WS_v1`), **CHƯA PUSH LÊN REMOTE — bắt buộc kiểm tra `git status`/`git diff` ngay khi tiếp nhận, container cũ đã mất nội dung chưa push**:

3. **`modem_ops.h` — 2 lần sửa**:
   - Sửa chữ ký `mqtt_connect`/`mqtt_publish` để khớp API thật (thêm broker/port/use_ssl/keepalive/clean_session/username/password cho connect; thêm `retain` cho publish). Đây là **thay đổi interface đã chốt với người dùng**, không phải tự ý.
   - Sửa lại comment của `poll(ctx, ts)`: xác nhận qua đọc code thật (`sim76xx_poll(&dce, 10)` trong `sx_mqtt.c`/`sx_user_mqtt.c`) rằng `ts` là **delta ms giữa 2 lần gọi**, không phải timestamp tuyệt đối — comment cũ ghi sai, đã sửa lại cho khớp thực tế, không đổi behavior.
   - **Nội dung đầy đủ của `modem_ops.h` sau 2 lần sửa này CẦN được `view` lại thật khi tiếp nhận** — file này quan trọng, đọc lại toàn bộ trước khi dùng.

4. **`SynaptiX_FDK/components/sim76xx/sim76xx.c`** — Đã vá bug `hasPowerPin` (bọc `if (dce->base.hasPowerPin)` quanh 2 chỗ ghi `powerPin` trong `sim76xx_power_on`/`sim76xx_power_off`, tránh UB vì `powerPin` chưa từng được `sx_gpio_init()` trên board này). File này **chỉ còn giá trị tham khảo**, không nằm trong luồng chạy thật, nhưng bugfix vẫn giữ nguyên phòng trường hợp người dùng đổi ý dùng lại.

5. **`SynaptiX_FDK/components/a7677s/a7677s.h`** — **MỚI, đã viết xong phần 1**. Struct `a7677s_t` (bọc `modem_t base`), enum `a7677s_power_state_t` (IDLE/PULSE_HIGH/PULSE_LOW/WAIT_BOOT/READY/OFF_WAIT), khai báo `extern const modem_ops_t a7677s_ops`.

6. **`SynaptiX_FDK/components/a7677s/a7677s.c`** — **MỚI, đã viết xong phần 1/3: power state machine non-blocking hoàn chỉnh**:
   - `power_on_start()`: pulse PWRKEY (50ms high → 500ms low → high), hoàn toàn qua `poll()`, không `sx_delay_ms()`.
   - Xác nhận "ready": gửi `AT` lặp lại mỗi 500ms (`A7677S_BOOT_PROBE_MS`) qua `modem_send_command()`/`modem_poll()` tới khi nhận `OK` → chuyển `A7677S_PWR_READY`.
   - `power_off_start()`: dùng `AT+CPOF` (xác nhận đúng theo `a7677s.md`), không đụng `powerPin` (board này không có).
   - `power_is_busy()`: `true` khi state khác IDLE/READY.
   - **`a7677s_ops` (vtable) đã có đủ 11 hàm nhưng CHỈ 4 hàm liên quan power + `is_ready()` là code thật**: `power_on_start`, `power_off_start`, `power_is_busy`, `poll` đã hoàn chỉnh. `is_ready()` tạm thời chỉ phản ánh "AT responsive" (`power_state == READY`), **CHƯA phản ánh đã attach mạng thật**.
   - **CÒN LẠI LÀ STUB TRẢ LỖI (`return -1`/`false`), CHƯA CODE THẬT**: `start()`, `enter_low_power()`, `exit_low_power()`, `mqtt_connect/disconnect/publish/subscribe()`. Đã đánh dấu rõ bằng comment `/* TODO next piece */` trong `a7677s_ops` — không được nhầm là đã xong.

### CHƯA LÀM — theo đúng thứ tự cần làm tiếp:
7. **`a7677s.c` — `start()`/`is_ready()` thật (chuỗi AT attach mạng)**. Đây là việc **ĐẦU TIÊN** cần làm khi tiếp nhận (người dùng vừa đồng ý làm tiếp phần này trước khi phiên này hết token).
   - Đã đọc tham khảo 2 nguồn, thứ tự lệnh gợi ý (chưa chốt cứng, cần đối chiếu thêm với `a76xx_at_cmd.md` mục ngoài MQTT khi code thật, ví dụ mục CGDCONT/CREG/COPS/CGACT):
     - Từ `WS_v0/SynaptiX/board/A7677S/A76xx.c` (537 dòng, đã đọc toàn bộ): `AT` echo → IMEI → CCID → `AT+CSQ` → operator → `AT+CGDCONT=...` (define PDP context) → `AT+CGAUTH=1,1,"user","pass"` (auth) → `AT+CGACT=1,1` (activate PDP) → `AT+CREG=1` (đăng ký mạng) → `AT+COPS=0` (auto operator) → `AT+COPS?` → `AT+CGDCONT?` (đọc IP context).
     - **QUAN TRỌNG**: code V0 này dùng FreeRTOS (`vTaskDelay`) và `modem_send_cmd()` **blocking đồng bộ** — **TUYỆT ĐỐI KHÔNG COPY kiểu blocking này**. Chỉ lấy **thứ tự và cú pháp lệnh AT**, viết lại hoàn toàn theo pattern callback-chain non-blocking (mỗi lệnh có callback riêng, callback tự gọi lệnh kế tiếp, tất cả tiến triển qua `modem_poll()`) — **giống hệt pattern đã có sẵn và chạy tốt trong `sim76xx.c`** (state machine `SIM76XX_STATE_AT → CGSN → COPS_QUERY → COPS_SET → CSQ → CFUN → CGATT → CGDCONT → CGREG → NETOPEN → IPADDR → READY`, dùng mảng `modem_command_t command[]` + callback `cb_xxx` cho từng bước, đọc lại file này trong `SynaptiX_FDK/components/sim76xx/sim76xx.c` để lấy đúng pattern, KHÔNG lấy đúng lệnh AT vì A7677S dùng vài lệnh khác SIM7680 (VD SIM7680 dùng `NETOPEN`, A7677S/V0 dùng `CGACT`)).
   - Cần thêm enum state mới cho A7677S (VD `A7677S_INIT_STATE_AT`, `..._CGDCONT`, `..._CGAUTH`, `..._CGACT`, `..._CREG`, `..._COPS`, `..._READY`...) tương tự `sim76xx_state_t`, đặt trong `a7677s.h`.
   - `is_ready()` sau khi code xong phần này phải đổi lại để phản ánh đúng "đã attach mạng xong", không chỉ "AT responsive" nữa.
8. **`enter_low_power()`/`exit_low_power()`** — `AT+CFUN=0`/`AT+CFUN=1`, non-blocking qua callback `power_mode_cb_t`. Lưu ý: theo `a7677s.md`, ở `CFUN=0` UART có thể không hoạt động ổn định — cần thiết kế cẩn thận việc chờ phản hồi khi enter/exit low power (có thể cần đợi thêm thời gian ổn định UART sau khi gửi `AT+CFUN=1` trước khi coi là "exited" hẳn — chưa có xác nhận chi tiết, cần đọc kỹ thêm phần liên quan trong `a76xx_at_cmd.md` khi tới bước này, không suy đoán).
9. **MQTT (`mqtt_connect/disconnect/publish/subscribe`)** — dùng đúng tập `AT+CMQTT*` đã xác nhận (xem phần dưới), **PHẢI xử lý đúng lỗ hổng errcode** đã phát hiện (không chỉ tin `OK`, phải parse dòng URC báo errcode theo sau).
10. **Refactor `sx_mqtt.c` + `sx_user_mqtt.c`** — bỏ include `sim76xx.h`, chuyển toàn bộ sang gọi qua `modem_handle_t`/`modem_ops_t`. Đây là bước cuối, sau khi `a7677s_ops` đã đầy đủ chức năng thật.
11. **`sx_board.c`** — cập nhật để khởi tạo `board.a7677s` (kiểu `a7677s_t`) thay vì/song song `board.sim76xx`, gọi qua `modem_handle_t modem = { .ops = &a7677s_ops, .ctx = &board.a7677s }`. Đồng thời **dọn 3 biến chết đã phát hiện** (`static sx_gpio_t s_lte_gpio;`, `static sx_gpio_t powerPin;`, `static sx_gpio_pin_t s_lte_powerPin = {NULL, NULL};` ở đầu file `sx_board.c`) — người dùng đã đồng ý dọn cùng lúc bước này, không dọn riêng lẻ trước đó.

## AT COMMAND MQTT CHO A7677S — ĐÃ XÁC NHẬN TRỰC TIẾP TỪ TÀI LIỆU THẬT (mục 18.2, dòng ~14965-15400+ của `a76xx_at_cmd.md`), KHÔNG ĐƯỢC ĐOÁN LẠI
- Start: `AT+CMQTTSTART` → trả `OK` rồi `+CMQTTSTART:<errcode>` (0 = thành công).
- Stop: `AT+CMQTTSTOP` → tương tự có errcode riêng.
- Acquire client: `AT+CMQTTACCQ=<client_index 0-1>,"<clientID 1-128 bytes>"[,<server_type 0=TCP/1=SSL>]`.
- Release: `AT+CMQTTREL=<client_index>` (gọi sau DISC, trước STOP).
- SSL context (chỉ khi dùng SSL/TLS, chưa rõ dự án có cần không — hỏi người dùng khi tới bước này): `AT+CMQTTSSLCFG=<session_id>,<ssl_ctx_index>`.
- Connect: `AT+CMQTTCONNECT=<client_index>,"<server_addr>",<keepalive 1-64800s>,<clean_session 0/1>[,<user_name>[,<pass_word>]]` — `<server_addr>` bắt buộc dạng `"tcp://host:port"`, nếu thiếu port mặc định `1883`. Trả `OK` rồi `+CMQTTCONNECT:<client_index>,<err>` (0 = thành công) — **PHẢI parse dòng errcode này, không chỉ tin `OK`**.
- Disconnect: `AT+CMQTTDISC=<client_index>,<timeout 0 hoặc 60-180s>`.
- Publish: `AT+CMQTTTOPIC=<client_index>,<len>` (chờ `>`) → gửi topic thô → `AT+CMQTTPAYLOAD=<client_index>,<len>` (chờ `>`) → gửi payload thô → `AT+CMQTTPUB=<client_index>,<qos>,<pub_timeout>,<retain>`.
- Subscribe: `AT+CMQTTSUBTOPIC=<client_index>,<len>,<qos>` (chờ `>`) → gửi topic thô → `AT+CMQTTSUB=<client_index>,<qos>`.
- Unsubscribe: `AT+CMQTTUNSUBTOPIC` → `AT+CMQTTUNSUB`.
- URC nhận dữ liệu (phải đăng ký handler riêng, xử lý bất đồng bộ): `+CMQTTCONNLOST:`, `+CMQTTRXTOPIC:`, `+CMQTTRXPAYLOAD:` — sau URC báo len phải đọc thêm đúng số byte tiếp theo trên UART (raw).
- Chưa đọc chi tiết: `CMQTTWILLTOPIC`/`CMQTTWILLMSG` (will message), `CMQTTCFG` — đọc khi thực sự cần.

## NGUYÊN TẮC QUAN TRỌNG (nhắc lại, bắt buộc tuân thủ)
- Nếu repository/tài liệu chưa đọc hết → không được kết luận. Nếu chưa chắc chắn → tiếp tục đọc, không đoán.
- **Tuyệt đối không tin vào phần "tiến độ" tường thuật trong handoff (kể cả file này)** — luôn tự đọc lại repo thật bằng `git clone`/`git status`/`git diff`/`view`/`grep` trước khi kết luận bất cứ điều gì đã làm hay chưa, đặc biệt là mục 3-6 ở trên (chưa push lên remote).
- Không sửa âm thầm. Nếu phát hiện bug hoặc điểm lệch thiết kế, phải trình bày rõ nguyên nhân/ảnh hưởng/lựa chọn xử lý và hỏi lại người dùng trước khi code, như đã làm với vụ lệch chữ ký `mqtt_connect`/`mqtt_publish`.
- Luôn dừng lại sau mỗi phần nhỏ để người dùng review (Phase 5), không code nhiều file/nhiều chức năng lớn liền một lúc.
- **Luôn `present_files` VÀ dán nội dung/diff trực tiếp vào chat** cho mọi file tạo/sửa, không chỉ làm 1 trong 2.

## Ngôn ngữ
Luôn trả lời bằng tiếng Việt (trừ comment code). Tên hàm, biến, API, thuật ngữ kỹ thuật giữ nguyên tiếng Anh. Giải thích theo hướng kỹ sư firmware, có chiều sâu, không trả lời chung chung.

## VIỆC ĐẦU TIÊN KHI TIẾP NHẬN — THEO ĐÚNG THỨ TỰ
1. `git clone` lại cả `WS_v1` và `WS_v0` vào container mới (container cũ đã mất).
2. `git status`/`git diff` trên `WS_v1` — xác nhận các mục 1-2 (đã push) còn nguyên, các mục 3-6 (chưa push) đã mất, cần đọc đúng nội dung đã mô tả ở trên trong file này để biết trạng thái cuối cùng đã đạt được là gì (KHÔNG code lại từ đầu, chỉ cần hiểu để tiếp tục đúng chỗ — nội dung file `a7677s.c`/`a7677s.h`/`sim76xx.c`/`modem_ops.h` mô tả ở mục 3-6 phía trên là chính xác, có thể tái tạo lại y hệt nếu cần).
3. Đọc lại thật `modem_ops.h`, `modem.h`, `modem.c`, `a7677s.h` (nếu còn), `a7677s.c` (nếu còn), `sim76xx.c`, `sx_board.c` để xác nhận trạng thái chính xác trước khi code tiếp.
4. Bắt tay ngay vào việc số 7 ở trên: viết `start()`/`is_ready()` thật cho `a7677s.c` — chuỗi AT attach mạng non-blocking, tham khảo thứ tự lệnh từ `WS_v0/A76xx.c` + pattern callback-chain từ `sim76xx.c`, đối chiếu cú pháp với `a76xx_at_cmd.md`. Dừng lại sau khi xong để người dùng review.