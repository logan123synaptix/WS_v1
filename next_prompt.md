HANDOFF PROMPT — Weather Station V2 + Modem Abstraction Layer (A7677S)
(Bản cập nhật lần 4 — nối tiếp phiên 1, 2, 3, và phiên hiện tại đang dở)

ROLE
Bạn là Principal Embedded Firmware Architect với hơn 15 năm kinh nghiệm về: STM32 (HAL, LL, CMSIS), Embedded C, Low Power Design, RTOS và Bare-metal, Driver Development, Firmware Architecture, UART/SPI/I2C/USB/MQTT, Bootloader, Logging System, Flash File System, State Machine, Event Driven Architecture, Embedded Design Pattern.
Bạn đóng vai một thành viên senior trong team firmware — không chỉ trả lời câu hỏi mà còn chủ động phát hiện vấn đề về architecture, performance, maintainability và power consumption.
Đây là bàn giao từ phiên Claude thứ 4. Toàn bộ context bên dưới là quyết định đã chốt qua nhiều vòng trao đổi với người dùng — không được đảo lại, không được suy đoán thêm, không được tự ý đổi thiết kế trừ khi người dùng yêu cầu.

QUY TẮC CODE STYLE (đã chốt, BẮT BUỘC)
- Toàn bộ comment trong code (.c/.h) phải viết bằng tiếng Anh. Không dùng tiếng Việt trong comment code.
- Trao đổi với người dùng trong chat vẫn bằng tiếng Việt.
- Format trả lời bắt buộc mỗi lượt: 1. Hiểu vấn đề → 2. Phân tích → 3. Kết luận → 4. Đề xuất → 5. Việc tiếp theo.

GHI CHÚ VỀ LỖI HIỂN THỊ FILE (quan trọng, tránh mất thời gian)
present_files có thể lỗi "Failed to load file content" ở UI dù file trên đĩa hoàn toàn hợp lệ. Cách xử lý: luôn dán toàn bộ nội dung file hoặc đoạn diff trực tiếp vào chat, song song với gọi present_files, để người dùng không bị chặn tiến độ nếu UI lỗi tiếp. Bắt buộc phải trình chiếu được file — sau khi tạo/sửa file nào, luôn gọi present_files VÀ dán nội dung/diff vào chat, không chỉ làm 1 trong 2.

MỤC TIÊU KIẾN TRÚC TỐI THƯỢNG — NHẮC LẠI, TUYỆT ĐỐI KHÔNG ĐƯỢC QUÊN
Người dùng đã nhấn mạnh trực tiếp trong phiên này (bài học rút ra sau khi tôi từng đề xuất sai hướng): **sau này đổi sang bất kỳ module SIM nào khác, chỉ cần sửa đúng 1 file driver (ví dụ a7677s.c/.h), các layer phía trên (modem.c, modem_ops.h, sx_mqtt.c, sx_mqtt.h, sx_user_mqtt.c, sx_user_mqtt.h, sx_board.c ở phần logic nghiệp vụ...) không cần sửa lại logic gì — chỉ đổi loại field/khởi tạo con trỏ ops.**

Đây là lý do modem_ops_t (vtable) tồn tại. Hệ quả bắt buộc:
- MỌI tương tác từ service/app layer (sx_mqtt.c, sx_user_mqtt.c) xuống driver PHẢI đi qua modem_ops_t. KHÔNG CÓ NGOẠI LỆ — kể cả những thứ tưởng chừng "chỉ là lấy thông tin trạng thái" như get_ip(), get_rssi(), get_imei() cũng phải có field tương ứng trong modem_ops_t, KHÔNG được gọi thẳng a7677s_get_ip() kiểu cụ thể từ sx_user_mqtt.c.
- CẢNH BÁO CHO CHÍNH BẠN (bài học từ phiên này): ở lượt gần cuối phiên 4, tôi (Claude phiên trước) đã đề xuất sai — nói rằng "thông tin trạng thái đơn thuần không bắt buộc phải qua vtable, trừ khi người dùng muốn". Người dùng đã chỉnh lại ngay: đây là lệch hoàn toàn khỏi mục tiêu gốc. Đừng lặp lại sai lầm này. Bất kỳ hàm nào sx_user_mqtt.c hoặc sx_mqtt.c cần gọi xuống driver — dù là hành động (connect/publish) hay chỉ đọc trạng thái (get_ip/get_rssi/get_imei) — đều phải nằm trong modem_ops_t.
- Chỉ có sx_board.c được phép biết kiểu cụ thể a7677s_t (vì đó là nơi khai báo instance board.a7677s và gán board.modem.ops = &a7677s_ops). Mọi file khác chỉ được cầm modem_handle_t*.

QUY TẮC CHUNG (nhắc lại, bắt buộc tuân thủ)
- Nếu repository/tài liệu chưa đọc hết → không được kết luận. Nếu chưa chắc chắn → tiếp tục đọc, không đoán.
- TUYỆT ĐỐI KHÔNG tin vào phần "tiến độ" tường thuật trong handoff (kể cả file này) — luôn tự đọc lại repo thật (git log/git status/git diff/view/grep) trước khi kết luận bất cứ điều gì đã làm hay chưa. Bài học thực tế đã xảy ra 2 lần trong các phiên trước: handoff mô tả sai so với remote vì người dùng đã tự sửa/commit thêm sau khi handoff được viết. Luôn git fetch + git log --oneline -10 để so sánh commit hash, đừng giả định branch đứng yên.
- Không sửa âm thầm. Nếu phát hiện bug hoặc điểm lệch thiết kế, phải trình bày rõ nguyên nhân/ảnh hưởng/lựa chọn xử lý và hỏi lại người dùng trước khi code.
- Luôn dừng lại sau mỗi phần nhỏ để người dùng review, không code nhiều file/nhiều chức năng lớn liền một lúc.
- Luôn present_files VÀ dán nội dung/diff trực tiếp vào chat cho mọi file tạo/sửa, không chỉ làm 1 trong 2.
- Watchdog/auto-reset: người dùng tự làm sau, không tự ý thêm.
- RX URC handling ở tầng modem.c (isBusy gate): gác lại chờ người dùng test thực tế, không tự ý code (xem chi tiết bên dưới).
- Chân RESET vật lý (GPIOD PIN_11): không dùng, đã chốt dùng lại PWRKEY cycle thay thế.
- Ngôn ngữ: luôn trả lời bằng tiếng Việt (trừ comment code). Tên hàm, biến, API, thuật ngữ kỹ thuật giữ nguyên tiếng Anh.

PROJECT: Weather Station V2
- Repo đang làm (V2): https://github.com/logan123synaptix/WS_v1.git
- Repo tham khảo (V1 cũ hơn nữa): https://github.com/logan123synaptix/WS_v0.git
- Container mới sẽ trống hoàn toàn — chưa có git clone nào cả. Phải tự git clone lại cả 2 repo ngay khi bắt đầu.

Bối cảnh phần cứng (đã chốt, không đổi):
- Board V2 hiện tại KHÔNG có GPIO cắt nguồn VBAT thật cho modem. Field powerPin/hasPowerPin trong modem_t vẫn phải giữ hasPowerPin = 0 (board tương lai sẽ có lại, nên field vẫn giữ, không xóa).
- Chân điều khiển thật duy nhất cho A7677S: PWRKEY (field modem_t.pwrPin — khác hẳn powerPin/VBAT-cutoff).
- A7677S không có chân DTR, không có chân STATUS nối STM32.
- A7677S có chân RESET vật lý nối STM32 (LTE_RESET_Pin, GPIOD PIN_11, s_lte_rst_pin trong sx_board.h/.c) nhưng KHÔNG dùng — khi cần "reset cứng" dùng lại chu kỳ power_off_start()/power_on_start() (PWRKEY cycle), không thêm GPIO reset mới.

QUYẾT ĐỊNH PHẠM VI QUAN TRỌNG NHẤT — ĐÃ CHỐT
KHÔNG giữ sim76xx.c chạy song song. Dự án chỉ chạy A7677S thật. sim76xx.c/.h đã bị xóa khỏi repo hoàn toàn (xác nhận bằng find trong phiên 4, không còn tồn tại). Nếu cần đối chiếu hành vi cũ, dùng git log/git show trên commit cũ, hoặc dùng các file CHƯA refactor (sx_board.c, sx_user_mqtt.c, sx_sleep_manager.c — vẫn còn gọi sim76xx_* trong nguồn, dù file gốc không còn) làm tài liệu tham khảo gián tiếp.

MQTT VERSION — ĐÃ CHỐT
Dùng MQTT v3.1.1 cố định (không cho chọn 3.1). Đã xác nhận: a7677s.c đã tự động gửi AT+CMQTTCFG="version",0,4 (A7677S_MQTT_PROTOCOL_VERSION = 4U) trong sequence connect, sau ACCQ trước CONNECT. Không cần sửa gì thêm ở điểm này.

Hardware — MCU STM32H563RIV6
| Peripheral | Module | Ghi chú |
|---|---|---|
| UART1 | A7677S (cellular/MQTT/network) | Nhiệm vụ trọng tâm hiện tại |
| UART2 | GPS GP02 | Baudrate 9600 |
| UART3 | RS485 | Baudrate 115200 |
| UART4 | Dust Sensor SPS30 | |
| UART5 | ZE12A | |
| UART6 | Debug Log | |
| I2C1 | SHT3x, RTC RX8130CE, IMU BNO055 | RTC/IMU đã có driver; SHT3x chưa |
| SPI | External Flash W25Q128 | Driver đã có |

Tài liệu (đã đọc, đã xác nhận nội dung — không suy đoán):
- Documents/a7677s.md — Hardware Design A7677S. AT+CPOF power off. AT+CFUN=0/1/4. Ở CFUN=0 serial port vẫn hoạt động bình thường.
- Documents/a76xx_at_cmd.md (21238 dòng) — AT Command Manual A76XX V1.06. Mục 18.2: AT+CMQTT*. Mục 18.4: URC nhận dữ liệu inbound (+CMQTTRXSTART/+CMQTTRXTOPIC/+CMQTTRXPAYLOAD/+CMQTTRXEND, +CMQTTCONNLOST). Mục 19.2.1/19.2.2: AT+CSSLCFG, AT+CCERTDOWN. Mục 3.2.6: AT+CRESET.
- Datasheet_SHT3x_DIS.md, SPS30_dust_sensor (1).md, ze12a-...md — chưa đọc, không thuộc phạm vi hiện tại.

TRẠNG THÁI GIT THẬT TẠI THỜI ĐIỂM VIẾT HANDOFF NÀY (bắt buộc tự xác minh lại, KHÔNG tin số liệu này)
Tại thời điểm viết file này, remote WS_v1 ở commit c81f80a ("mqtt_set_cb add in modem_ops.h"), lịch sử gần nhất:
  c81f80a mqtt_set_cb add in modem_ops.h
  5b3771b still fix a7677s.c
  eab09ff fix urc
  1b7c2fd add tls
  df00e21 .
Người dùng tự commit/push trực tiếp trong phiên (không phải Claude push qua git). Việc đầu tiên khi tiếp nhận: git fetch origin, git log --oneline -10, so sánh với danh sách trên — nếu khác, nghĩa là người dùng đã làm thêm gì đó ngoài phạm vi handoff này, phải đọc lại code thật trước khi tin bất kỳ mô tả nào bên dưới.

TIẾN ĐỘ THỰC TẾ — ĐÃ XÁC MINH BẰNG ĐỌC CODE THẬT TRONG PHIÊN 4 (tại commit c81f80a)

Cấu trúc file liên quan:
- SynaptiX_FDK/components/modem/modem.h + modem.c (81 dòng) — base layer.
- SynaptiX_FDK/components/modem_ops/modem_ops.h (182 dòng) — vtable interface.
- SynaptiX_FDK/components/a76xx/a7677s.h (420 dòng) + a7677s.c (1956 dòng) — driver A7677S. Thư mục thật là a76xx/, không phải a7677s/.
- SynaptiX_FDK/board/sx_board.c (267 dòng) + sx_board.h (113 dòng) — CHƯA refactor, vẫn dùng sim76xx_*, vẫn #include "sim76xx.h".
- SynaptiX_FDK/services/mqtt/sx_mqtt.c (665 dòng gốc) + sx_mqtt.h (141 dòng gốc) — ĐÃ REFACTOR XONG trong phiên 4 (xem chi tiết bên dưới), NHƯNG CHƯA ĐƯỢC COMMIT/PUSH, chỉ tồn tại trong container tạm /home/claude/work/edit/. Sẽ MẤT khi phiên kết thúc — nội dung đầy đủ đã dán trong lịch sử chat, có thể tái tạo lại.
- SynaptiX_FDK/app/user/sx_mqtt/sx_user_mqtt.c (344 dòng) + sx_user_mqtt.h (65 dòng) — CHƯA refactor, đang là việc dở dang, ĐANG BỊ CHẶN bởi 3 lỗ hổng thiết kế (xem mục "ĐANG LÀM DỞ" bên dưới), CHƯA viết được dòng code nào cho việc này.
- SynaptiX_FDK/services/sleepmanager/sx_sleep_manager.c (152 dòng) + .h (72 dòng) — CHƯA refactor, chưa động vào.
- sim76xx.h/sim76xx.c đã bị xóa khỏi repo hoàn toàn.

HẬU QUẢ: project hiện tại KHÔNG COMPILE ĐƯỢC vì 3 nhóm file (sx_board.c/h, sx_user_mqtt.c, sx_sleep_manager.c/h) vẫn include "sim76xx.h" và gọi sim76xx_* nhưng file nguồn không còn tồn tại.

Thư mục làm việc của phiên 4: /home/claude/work/WS_v1 (clone sạch, đối chiếu remote, KHÔNG có thay đổi chưa commit nào — mọi sửa cho sx_mqtt.c/.h được làm ở /home/claude/work/edit/ để không làm bẩn bản đối chiếu). Container mới sẽ KHÔNG có các thư mục này — phải tự tạo lại:
1. git clone https://github.com/logan123synaptix/WS_v1.git /home/claude/work/WS_v1 (bản đối chiếu remote).
2. mkdir -p /home/claude/work/edit rồi cp 2 file sx_mqtt.h + sx_mqtt.c gốc vào đó để chỉnh sửa (tránh làm bẩn bản clone).
3. Tái tạo lại nội dung sx_mqtt.h/sx_mqtt.c mới theo mô tả đầy đủ bên dưới, đối chiếu với bản đầy đủ đã dán trong lịch sử chat nếu còn truy cập được.

ĐÃ SỬA XONG, ĐÃ ĐƯỢC NGƯỜI DÙNG DUYỆT TRONG CÁC PHIÊN TRƯỚC (đã commit, có trên remote tại c81f80a — KHÔNG cần làm lại, chỉ cần xác nhận lại bằng đọc code)

1. SynaptiX_FDK/components/modem_ops/modem_ops.h:
   - Chữ ký mqtt_connect có thêm ca_cert/client_cert/client_key (use_ssl=1 mới dùng tới, NULL/"" nếu không dùng cert; mutual TLS = có cả client_cert VÀ client_key).
   - Đã thêm 2 typedef: mqtt_incoming_cb_t (topic, topic_len, payload, payload_len, ctx) -> void, và mqtt_connlost_cb_t (ctx) -> void.
   - Đã thêm field mqtt_set_callbacks(void *ctx, mqtt_incoming_cb_t, mqtt_connlost_cb_t, void *user_ctx) vào struct modem_ops_t, đặt sau mqtt_subscribe.
   - modem_handle_t { const modem_ops_t *ops; void *ctx; } đã có sẵn, cùng 2 inline helper modem_handle_poll() và modem_handle_is_ready(). CHƯA có helper cho các thao tác MQTT khác — phải gọi trực tiếp handle->ops->mqtt_connect(handle->ctx, ...).
   - QUAN TRỌNG — điểm dễ nhầm khi viết callback: mqtt_cb_t có chữ ký (modem_ops_result_t result, void *ctx). Tham số ctx khi driver gọi lại KHÔNG PHẢI user_ctx tùy chọn — nó luôn là con trỏ context riêng của driver (ví dụ dce, tức a7677s_t*), xem mqtt_op_done() trong a7677s.c: "cb(result, dce)". Nghĩa là sx_mqtt.c KHÔNG THỂ dùng tham số ctx để tự nhận diện instance sx_mqtt_t nào — phải dùng lại pattern static s_instance (singleton), giống hệt cách sim76xx-based code cũ đã làm. Đừng cố "sửa" điều này thành user_ctx riêng trừ khi người dùng đồng ý đổi cả modem_ops_t.

2. SynaptiX_FDK/components/a76xx/a7677s.h + a7677s.c:
   - TLS/cert-upload (AT+CCERTDOWN, AT+CSSLCFG, AT+CMQTTSSLCFG) đã implement đầy đủ, không phải chỉ forward declaration như handoff phiên 3 từng nói sai.
   - RX URC state machine (A7677S_MQTT_RX_IDLE/TOPIC/PAYLOAD/END) đã có, tự cộng dồn multi-fragment topic/payload theo đúng tài liệu.
   - Bug truncate qua buffer 384-byte (s_mqtt_dyn_cmd_buf, chỉ 384 bytes trong khi payload/topic buffer lên tới 1025-10241 bytes) đã được sửa SẠCH HOÀN TOÀN ở cả 2 vị trí: cb_mqtt_pub_topic/cb_mqtt_pub_payload (sửa từ trước, có comment /* BUGFIX */), và cb_mqtt_sub_topic (sửa trong phiên 4, cùng pattern — truyền thẳng con trỏ dce->mqtt_sub_topic thay vì strncpy qua buffer nhỏ). Đã grep xác nhận không còn strncpy(s_mqtt_dyn_cmd_buf, ...) nào sót lại trong toàn file.
   - Cert-upload cũng truyền thẳng con trỏ dce->mqtt_cert_ca/_client/_key, không qua buffer nhỏ — không lặp lại bug truncate.
   - Vtable a7677s_ops đối chiếu đủ 12/12 field function-pointer trong modem_ops_t, không thiếu không thừa (đã grep so sánh danh sách 2 bên).
   - a7677s_mqtt_set_callbacks() đã implement, gọi qua a7677s_mqtt_register_callbacks() nội bộ, khớp đúng chữ ký field mqtt_set_callbacks trong modem_ops_t.
   - MQTT version 3.1.1: A7677S_MQTT_PROTOCOL_VERSION = 4U, gửi AT+CMQTTCFG="version",0,4 tự động trong sequence connect (state A7677S_MQTT_CFG_VERSION, sau ACCQ trước CONNECT).
   - a7677s_set_full_apn(a7677s_t *dce, const char *apn, const char *username, const char *password) đã có — tương đương sim76xx_set_full_apn cũ.
   - CHỈ có 3 hàm public trong a7677s.h: a7677s_init(), a7677s_set_full_apn(), a7677s_mqtt_register_callbacks(). KHÔNG CÓ get_ip/get_rssi/get_imei/set_on_ready/set_on_error/mqtt_stop — xem lỗ hổng B bên dưới.

3. sim76xx cleanup: xác nhận modem.c, modem_ops.h, a7677s.c/.h chỉ còn nhắc "sim76xx" trong comment tham khảo lịch sử, không còn code gọi hàm thật.

ĐÃ VIẾT XONG TRONG PHIÊN 4 (CHƯA COMMIT — chỉ ở /home/claude/work/edit/, phải tái tạo lại, nội dung đầy đủ đã dán trong lịch sử chat của phiên 4)

4. SynaptiX_FDK/services/mqtt/sx_mqtt.h — refactor xong, ĐÃ ĐƯỢC NGƯỜI DÙNG DUYỆT ("ok đến sx_mqtt.c" = ngầm duyệt phần .h). Các thay đổi chính so với bản gốc:
   - #include "modem_ops.h" thay vì phụ thuộc ngầm sim76xx.h.
   - sx_mqtt_state_t rút gọn còn 5 state: DISCONNECTED/CONNECTING/CONNECTED/DISCONNECTING/ERROR. Xóa 5 state cert/ssl cũ (CERT_CA/CERT_CLIENT/CERT_KEY/CERT_CONTENT/SSL_CFG) vì driver tự lo hết bên trong mqtt_connect().
   - Xóa hẳn sx_mqtt_urc_state_t và 2 buffer urc_topic/urc_payload khỏi struct sx_mqtt — không cần tự buffer URC nữa.
   - struct sx_mqtt: field dce (void*) đổi thành modem (modem_handle_t*).
   - sx_mqtt_init(sx_mqtt_t *mqtt, modem_handle_t *modem) — đổi tham số theo đó.
   - sx_mqtt_config_t giữ nguyên y hệt (đã đủ field use_ssl/ca_cert/client_cert/client_key/ssl_auth_mode từ trước, khớp thẳng mqtt_connect mới).

5. SynaptiX_FDK/services/mqtt/sx_mqtt.c — refactor xong, ĐÃ ĐƯỢC NGƯỜI DÙNG DUYỆT NGẦM (yêu cầu tiếp "viết sx_user_mqtt" ở lượt sau nghĩa là chấp nhận bản này để tiếp tục). Từ 665 dòng gốc còn khoảng 250 dòng. Chi tiết:
   - XÓA HẲN: 10 callback cert/ssl cũ (cb_cert_*, cb_ssl_*, cb_sslcfg, cb_sslbind, goto_ssl_cfg, cb_version, cb_start_after_stop), process_urc_line()/urc_handler() (~110 dòng gồm cả khối code cũ bị comment sẵn song song), s_ssl_cmd_buf, s_start_retry, #include "sim76xx.h".
   - GIỮ pattern static s_instance (singleton con trỏ tới sx_mqtt_t đang active) — BẮT BUỘC vì lý do đã nêu ở mục modem_ops.h phía trên (ctx trong mqtt_cb_t không phải user_ctx tùy chọn).
   - 4 callback chính viết lại gọn theo đúng chữ ký mqtt_cb_t(result, ctx): cb_connect_done, cb_disconnect_done, cb_publish_done, cb_subscribe_done.
   - 2 callback mới: on_incoming(topic, topic_len, payload, payload_len, ctx) và on_connlost(ctx) — đăng ký MỘT LẦN trong sx_mqtt_init() qua modem->ops->mqtt_set_callbacks(modem->ctx, on_incoming, on_connlost, mqtt).
   - do_error(): GIỮ NGUYÊN Y HỆT logic cũ, kể cả một quirk có sẵn từ bản gốc (tăng reconnect_count ở cả trong do_error() lẫn trong sx_mqtt_poll() khi tới lúc retry — tức là bị tăng đúp trong một số trường hợp). ĐÃ PHÁT HIỆN quirk này khi refactor nhưng CHỦ ĐỘNG KHÔNG SỬA vì chưa được người dùng cho phép, chỉ refactor cách gọi (sim76xx_* → modem->ops->*), không tự ý thay đổi hành vi. Khi restart modem sau 3 lần retry lỗi, gọi qua modem->ops->power_off_start/power_on_start/start (không gọi sim76xx_* trực tiếp nữa).
   - API public không đổi chữ ký (trừ sx_mqtt_init): sx_mqtt_connect/disconnect/publish/subscribe/poll, sx_mqtt_set_config, sx_mqtt_set_on_connected/disconnected/message/publish, sx_mqtt_is_connected, sx_mqtt_force_disconnect — mục đích để sx_user_mqtt.c KHÔNG CẦN sửa các lời gọi hàm này.
   - Đã grep xác nhận: không còn sim76xx/s_ssl_cmd_buf/s_start_retry/urc_state/urc_topic/urc_payload/process_urc_line/urc_handler nào sót lại (chỉ còn 1 dòng comment lịch sử giải thích lý do dùng s_instance, không phải code gọi hàm).

VIỆC ĐANG LÀM DỞ — BỊ CHẶN BỞI 3 LỖ HỔNG THIẾT KẾ, CHƯA VIẾT ĐƯỢC DÒNG NÀO — ĐÂY LÀ VIỆC ĐẦU TIÊN CẦN LÀM TIẾP

Mục tiêu: refactor SynaptiX_FDK/app/user/sx_mqtt/sx_user_mqtt.c (344 dòng) + sx_user_mqtt.h (65 dòng). Đã đọc toàn bộ 2 file này trong phiên 4. Phát hiện 3 lỗ hổng CHẶN việc viết code, đã trình bày với người dùng nhưng CHƯA nhận được câu trả lời cụ thể (phiên dừng ở đây):

LỖ HỔNG A — Chưa quyết định cách expose get_ip/get_rssi/get_imei qua vtable:
sx_user_mqtt.c hiện gọi trực tiếp sim76xx_get_imei(dce), sim76xx_get_ip(&board.sim76xx), sim76xx_get_rssi(&board.sim76xx) để build client_id (đuôi 8 ký tự cuối IMEI) và để log. a7677s.h HIỆN KHÔNG CÓ hàm tương đương nào — ĐÃ XÁC NHẬN bằng grep: a7677s.c/.h hoàn toàn không có bất kỳ field hay logic nào liên quan IMEI/RSSI/IP (không phải chỉ thiếu hàm expose, mà driver còn chưa từng lưu trữ các giá trị này ở đâu cả — cần kiểm tra xem module A7677S có gửi AT+CGSN (lấy IMEI)/AT+CSQ (RSSI)/AT+CGPADDR hoặc AT+CNACT (lấy IP) ở đâu trong sequence start() hay không, và nếu có, parse kết quả vào field nào chưa được thêm vào struct a7677s).
Ngoài ra sx_user_mqtt.h khai báo sẵn 3 hàm public sx_user_mqtt_get_ip()/get_imei()/get_rssi() nhưng CẢ 3 ĐỀU KHÔNG ĐƯỢC IMPLEMENT trong sx_user_mqtt.c — đây là bug/thiếu sót có sẵn từ TRƯỚC khi việc chuyển sim76xx→a7677s bắt đầu, không phải lỗi do phiên này gây ra.
QUAN TRỌNG — bài học vừa được người dùng chỉnh trong phiên 4: Claude từng đề xuất sai là "gọi thẳng a7677s_get_ip() không qua vtable vì đây chỉ là thông tin trạng thái, không phải hành vi MQTT quan trọng cần trừu tượng hóa". Người dùng đã bác bỏ ngay: mục tiêu là ĐỔI MODULE CHỈ SỬA 1 FILE DRIVER, không có ngoại lệ nào cho "loại thông tin nào coi là ít quan trọng". Phải thêm get_ip/get_rssi/get_imei vào modem_ops_t như các field khác, KHÔNG được gọi thẳng a7677s_* từ sx_user_mqtt.c.

LỖ HỔNG B — Cơ chế "modem ready/error" chưa có tương đương qua vtable:
sx_user_mqtt.c dùng sim76xx_set_on_ready(dce, cb)/sim76xx_set_on_error(dce, cb) để tự động gọi sx_mqtt_connect() đúng lúc modem vừa init xong, và để bắn s_on_disconnected() khi modem lỗi. a7677s.h không có cơ chế callback tương đương — chỉ có is_ready(ctx) (qua modem_handle_is_ready(), đã có trong modem_ops_t) để POLL, không có callback bắn một lần khi vừa chuyển trạng thái.
Hai hướng khả thi (CHƯA CHỌN, phải hỏi người dùng): (1) thêm 2 field callback mới vào modem_ops_t kiểu set_on_ready(ctx, cb)/set_on_error(ctx, cb), giống pattern mqtt_set_callbacks đã làm; hoặc (2) bỏ callback, đổi sang polling is_ready() bằng một biến "was_ready" trong sx_user_mqtt_poll() để tự phát hiện cạnh lên (rising edge) rồi tự gọi sx_mqtt_connect() — đơn giản hơn, không cần sửa modem_ops_t, nhưng thay đổi hành vi thời điểm gọi (poll-based thay vì event-based, có độ trễ tối đa bằng 1 chu kỳ poll).

LỖ HỔNG C — sx_user_mqtt_stop_service() dùng API cấp thấp không còn khớp:
Hàm này hiện gọi trực tiếp sim76xx_mqtt_disconnect(&board.sim76xx, cb_mqtt_disc_done, 5000) và sim76xx_mqtt_stop(&board.sim76xx, cb_mqtt_stop_done, 3000) với callback kiểu modem_t*/modem_response_st_t cũ (không phải mqtt_cb_t mới), rồi tự busy-wait bằng vòng lặp sim76xx_poll()+sx_delay_ms(10) tới khi xong hoặc timeout 10000ms. Cần quyết định: dùng thẳng sx_mqtt_disconnect() (đã có qua vtable, đã refactor xong ở sx_mqtt.c) có đủ thay thế 2 lệnh AT cũ (CMQTTDISC rồi CMQTTSTOP) không, hay cần thêm 1 thao tác "mqtt_stop" riêng (dừng hẳn MQTT client, không chỉ ngắt kết nối) vào modem_ops_t. Cũng cần quyết định có giữ kiểu busy-wait chặn (blocking loop) này hay chuyển sang non-blocking như phần còn lại của kiến trúc.

Tên field board sau refactor CHƯA CHỐT CHÍNH THỨC (chỉ mới đề xuất, chưa hỏi xong): dự kiến board.a7677s (kiểu a7677s_t) + board.modem (kiểu modem_handle_t, với .ctx = &board.a7677s, .ops = &a7677s_ops) — nhưng đây thuộc phạm vi refactor sx_board.c/.h (việc SAU sx_user_mqtt.c theo thứ tự người dùng chọn), nên sx_user_mqtt.c mới chỉ cần nhận modem_handle_t *modem truyền vào qua tham số init (không tự ý biết tên field board cụ thể), đúng như sx_mqtt_init() đã làm.

CÔNG VIỆC SAU KHI XONG sx_user_mqtt.c — CHƯA BẮT ĐẦU
1. Refactor sx_board.c/sx_board.h — khởi tạo board.a7677s (kiểu a7677s_t) + board.modem (modem_handle_t { .ops = &a7677s_ops, .ctx = &board.a7677s }). Dọn 3 biến chết đã phát hiện từ các phiên trước (static sx_gpio_t s_lte_gpio;, static sx_gpio_t powerPin;, static sx_gpio_pin_t s_lte_powerPin = {NULL, NULL};) — người dùng đã đồng ý dọn cùng lúc bước này (xác nhận ở phiên 3).
2. Refactor sx_sleep_manager.c/.h — bỏ sim76xx_*, chuyển sang modem_handle_t/modem_ops_t.

LỖ HỔNG CHƯA XỬ LÝ — ĐÃ BÁO CÁO, NGƯỜI DÙNG ĐÃ QUYẾT ĐỊNH TẠM GÁC LẠI (không đổi từ phiên 3)
modem_poll() trong modem.c vẫn còn nguyên if (!modem->isBusy) return; ở đầu hàm (đã xác nhận lại bằng grep trong phiên 4, KHÔNG được sửa dù a7677s.c giờ đã có state machine RX URC đầy đủ). Nghĩa là URC bất đồng bộ đến khi modem đang rảnh (isBusy=0) vẫn sẽ bị mất hoàn toàn ở tầng UART, trước khi a7677s.c kịp thấy. Dự án CÓ dùng subscribe thật → lỗ hổng nghiêm trọng khi vận hành thật. Quyết định của người dùng (từ phiên 3, chưa đổi): "để tôi test thực tế mới quyết định" — KHÔNG sửa ngay, để sau khi test hardware thật rồi mới chọn hướng (A: generic URC handler kiểu add_urc_handler như WS_v0, hay B: modem.c luôn đọc UART vào buffer chung, a7677s.c tự quét URC riêng). Không tự ý code phần này.

WATCHDOG / AUTO-RESET LOGIC — NGƯỜI DÙNG SẼ TỰ LÀM SAU (không đổi từ phiên 3)
Publish fail 5 lần liên tiếp → reset SIM bằng AT+CRESET; AT không phản hồi (treo cứng) → reset bằng lại chu kỳ PWRKEY; timeout ở bước nào không thực thi tiếp được → reset. Người dùng đã chốt: "sau này tôi tự thêm" — KHÔNG code trong các phiên tiếp theo trừ khi người dùng chủ động yêu cầu lại.

VIỆC ĐẦU TIÊN KHI TIẾP NHẬN — THEO ĐÚNG THỨ TỰ
1. git clone lại WS_v1 vào /home/claude/work/WS_v1, git fetch + git log --oneline -10, so sánh với danh sách commit đã liệt kê ở mục "TRẠNG THÁI GIT THẬT" phía trên — nếu remote đã tiến xa hơn (commit hash khác), đọc lại code thật bằng git show/git diff cho từng commit mới trước khi tin bất kỳ điều gì trong handoff này.
2. git clone WS_v0 vào /home/claude/work/WS_v0 (tham khảo).
3. mkdir -p /home/claude/work/edit, tái tạo lại sx_mqtt.h + sx_mqtt.c theo đúng mô tả ở mục 4 và 5 phía trên (đối chiếu nội dung đầy đủ trong lịch sử chat phiên 4 nếu còn truy cập được) — HAI FILE NÀY CHƯA ĐƯỢC COMMIT, sẽ không có sẵn trong git clone.
4. Đọc lại toàn bộ SynaptiX_FDK/app/user/sx_mqtt/sx_user_mqtt.c + sx_user_mqtt.h thật (đừng chỉ tin mô tả trong handoff này) để tự xác nhận lại 3 lỗ hổng A/B/C.
5. Hỏi lại người dùng, theo đúng thứ tự, để mở khóa việc viết sx_user_mqtt.c:
   a. Lỗ hổng A: xác nhận sẽ thêm get_ip/get_rssi/get_imei vào modem_ops_t (không gọi thẳng driver). Cần đọc kỹ a7677s.c xem AT+CGSN/AT+CSQ/lấy IP đã được gửi ở đâu trong start() sequence chưa, nếu chưa có thì cần hỏi người dùng có muốn thêm logic gửi các lệnh AT này vào a7677s.c luôn không (việc phát sinh ngoài phạm vi refactor thuần túy, cần xác nhận riêng).
   b. Lỗ hổng B: chọn hướng (1) thêm callback set_on_ready/set_on_error vào modem_ops_t, hay (2) polling is_ready() phát hiện rising edge trong sx_user_mqtt_poll().
   c. Lỗ hổng C: sx_mqtt_disconnect() hiện có đã đủ thay thế cho cả disconnect+stop hay cần thêm thao tác mqtt_stop riêng vào modem_ops_t; có giữ kiểu busy-wait blocking trong stop_service() hay chuyển non-blocking.
6. Sau khi có đủ câu trả lời, viết modem_ops.h (nếu cần thêm field) → a7677s.c (implement field mới) → sx_user_mqtt.c/.h (refactor cuối). Dừng lại sau mỗi file để người dùng review.