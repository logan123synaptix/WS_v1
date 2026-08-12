HANDOFF — WS_v1 (nhánh ft/heartbeat) — TIẾN ĐỘ PHIÊN NÀY
============================================================
Ngày: 2026-08-12
Nhánh làm việc: ft/heartbeat

repo: https://github.com/logan123synaptix/WS_v1.git
clone và pull ft/heartbeat trước

============================================================
TRẠNG THÁI GIT — QUAN TRỌNG, ĐỌC TRƯỚC
============================================================
5 file đang có thay đổi CHƯA COMMIT trên máy làm việc (chưa push),
đã kiểm tra ngoặc cân bằng (script Python đếm { } ( )), KHÔNG BUILD
THẬT (không có toolchain ARM trong môi trường làm việc), CHƯA CHẠY
TRÊN BOARD:

    SynaptiX_FDK/app/app.c
    SynaptiX_FDK/app/user/sx_sleep_manager/sx_sleep_manager.c
    SynaptiX_FDK/app/user/sx_sleep_manager/sx_sleep_manager.h
    SynaptiX_FDK/components/modules/a76xx/a7677s.c
    SynaptiX_FDK/components/modules/a76xx/a7677s.h

============================================================
ĐÃ FIX, ĐÃ XÁC NHẬN QUA HARDWARE THẬT (ổn định)
============================================================

1. HB_ONLY modem PWRKEY không lên nguồn — ĐÃ FIX, ĐÃ CONFIRM HW
--------------------------------------------------------------
Triệu chứng: heartbeat mini-wake (HB_ONLY) power-on modem xong bắn
toàn TIMEOUT [NULL], trong khi full-wake (lúc publish data) luôn
power-on modem thành công.

Nguyên nhân: full-wake có ext_flash_wake + gps_on + gps_wait_fix
(có thể tới cả phút) chạy TRƯỚC modem_power_on — tạo ra khoảng nghỉ
nguồn dài giữa power-off trước đó và power-on tiếp theo. HB_ONLY chỉ
đợi HB_ONLY_ZE12A_ACTIVE_MS=9.5s cố định rồi power-on ngay — không
đủ thời gian cooldown thật sự modem cần sau power-off (con số chính
xác chưa đo được, không có trong datasheet).

Fix: thêm cổng cooldown riêng HB_ONLY_MODEM_COOLDOWN_MS=15000
(sx_sleep_manager.h) — giữ 15s KHÔNG ĐỤNG modem/UART trước khi
_hb_only_publish_process() gọi power_on_start(), tách biệt hoàn
toàn khỏi HB_ONLY_ZE12A_ACTIVE_MS (ý nghĩa khác nhau — dwell time
cảm biến khí, không phải cooldown nguồn modem). Field mới trong
struct: hb_only_cooldown_done, hb_only_cooldown_elapsed_ms.
XÁC NHẬN: đã build+flash+test trên board thật, modem power-on
thành công trở lại trong HB_ONLY, không còn TIMEOUT [NULL].
15000ms là số chọn bảo thủ (dựa theo A7677S_OFF_SETTLE_MS=4500ms +
margin rộng) — CHƯA ĐO ĐƯỢC con số tối thiểu thật sự cần thiết,
có thể giảm nếu muốn heartbeat publish nhanh hơn, cần test dần.

2. Payload MQTT bị cắt giữa chừng — ĐÃ FIX, CHƯA CONFIRM HW
--------------------------------------------------------------
Triệu chứng: log firmware in ra JSON đầy đủ trước khi publish,
nhưng subscriber Python độc lập (mqtt_log_subscriber.py, xem file
đính kèm phiên này) nhận được payload bị cắt cụt giữa chừng —
xác nhận cắt xảy ra thật trên đường truyền, không phải do MQTT
Explorer hiển thị rút gọn (giả thuyết ban đầu, đã loại trừ).

Nguyên nhân: cb_mqtt_pub_payload_data() và cb_mqtt_pub_topic_data()
(a7677s.c) — bước gửi RAW BYTES (topic string / payload JSON) qua
UART rồi chờ modem echo lại "\r\nOK\r\n" xác nhận đã nhận đủ — dùng
chung A7677S_TIMEOUT_AT=2500ms, vốn chỉ để dành cho AT command NGẮN
(AT+CREG?, AT+CGSN, vài chục ký tự). Với payload ~500 byte, trên
điều kiện mạng thật (khác môi trường bàn/test yên tĩnh lúc trước
luôn ổn), modem có thể mất hơn 2.5s để nhận+đệm xong rồi mới echo
OK — timeout xảy ra giữa chừng, phần data chưa gửi hết bị bỏ dở,
nhưng AT+CMQTTPUB phía sau vẫn chạy tiếp trên state dở dang -> báo
"MQTT publish OK" nhưng nội dung thực chất bị cắt.

Fix: tách hằng số riêng A7677S_TIMEOUT_MQTT_PUB_PAYLOAD_DATA=8000ms
(a7677s.h) dùng cho cả 2 bước gửi raw bytes (topic data, payload
data), thay A7677S_TIMEOUT_AT cũ.
CHƯA CONFIRM: người dùng cần build+flash+chạy lại
mqtt_log_subscriber.py để xác nhận payload không còn bị cắt. Nếu
vẫn còn cắt ở 8000ms, có thể cần tăng thêm hoặc tính theo tỷ lệ độ
dài payload thay vì hằng số cố định.

============================================================
★★★ VẤN ĐỀ CÒN LẠI CHƯA XONG — TRỌNG TÂM CẦN LÀM TIẾP ★★★
============================================================

3. sensorStatus trong heartbeat sai trạng thái 3 cảm biến khí
--------------------------------------------------------------
Board thật chỉ có 3 module ZE12A cắm thật: SO2/NO2/O3 (đã xác nhận
với người dùng — CO/H2S không có phần cứng, mãi mãi FAIL là đúng,
KHÔNG PHẢI bug).

TRIỆU CHỨNG: SO2/NO2/O3 dù đọc được số liệu thật tốt trong cùng
lap's telemetry (ví dụ so2:53, no2:5, o3:12 trong payload data),
sensorStatus trong heartbeat CÙNG LAP đó vẫn báo FAIL cho một số/
tất cả 3 loại này — không nhất quán giữa các lần chạy (có lúc
SO2/O3 lên OK còn NO2 FAIL, có lúc cả 3 FAIL).

ĐÃ ĐIỀU TRA, ĐÃ LOẠI TRỪ CÁC GIẢ THUYẾT SAU (không phải nguyên
nhân, đã xác nhận bằng log/test cụ thể):
  - KHÔNG PHẢI lỗi mux/GPIO S0-S1 phần cứng: test_ze12a.c chạy
    standalone (không qua STOP mode) luôn đọc đủ cả 3 kênh (0,1,2)
    ổn định, ngay cả sau khi reset MCU nhiều lần.
  - KHÔNG PHẢI vấn đề round-robin/modulo trong ze12a.c: công thức
    (channel & 0x01 -> S0, channel & 0x02 -> S1) đã verify đúng
    qua code lẫn qua test_ze12a.c.
  - Field hb_only_gas_snapshot_type[]/hb_only_gas_snapshot_connected[]
    (sx_sleep_manager.h/.c, thêm trong phiên trước) và
    sx_sleep_manager_hb_only_gas_connected() (app.c dùng thay
    gas_sensor_app_is_connected() live khi đang trong ngữ cảnh
    HB_ONLY) — fix này ĐÃ ĐÚNG cho vấn đề nó nhắm tới (isConnected
    bị age-out theo GAS_SENSOR_TIMEOUT_MS=10s do phase 2 tốn hơn
    10s modem handshake) nhưng KHÔNG PHẢI nguyên nhân chính của bug
    #3 này (log thật cho thấy sai cả ở đường full-wake, không chỉ
    HB_ONLY, nơi fix đó không áp dụng).

NGHI VẤN MẠNH NHẤT, CHƯA XÁC NHẬN, CẦN ĐIỀU TRA TIẾP:
  UART_EXTEND (ZE12A's UART5) bị board_sleep_pre_stop_hook()
  (sx_board.c) gọi HAL_UART_Abort() MỌI LẦN vào STOP mode — kể cả
  khi ZE12A vẫn có điện xuyên suốt. Sau khi thoát STOP, UART5's RX
  interrupt CHẾT cho tới khi có ai gọi board_extend_uart_resume_it()
  re-arm lại. Full-wake's wake step 'gas_sensor_active_mode'
  (_gas_sensor_active_mode_start(), sx_sleep_manager.c dòng ~170)
  ĐÃ gọi resume đúng. HB_ONLY's sx_sleep_manager_hb_only_start()
  TRƯỚC ĐÂY KHÔNG gọi resume trước gas_sensor_switch_to_active_mode()
  — ĐÃ FIX field này ở phiên trước đó (thêm board_extend_uart_resume_it()
  vào sx_sleep_manager_hb_only_start(), xem sx_sleep_manager.c). Fix
  này NẰM TRONG các file đã sửa nhưng CHƯA COMMIT ở trên.

  TUY NHIÊN: log thật gần nhất (phiên này) cho thấy sensorStatus
  sai CẢ Ở LAP FULL-WAKE (không chỉ HB_ONLY) — nơi resume UART đã
  luôn đúng từ trước. Vậy giả thuyết "thiếu resume UART_EXTEND"
  CHỈ giải thích được PHẦN HB_ONLY của bug, KHÔNG giải thích được
  phần full-wake. Cần điều tra thêm ở full-wake path — hướng gợi ý:
    a. Kiểm tra timing giữa gas_sensor_active_mode wake step (sớm
       trong wake sequence) và lúc build_heartbeat_payload() thực
       sự chạy (SENDING state, cuối chu kỳ) — khoảng cách này có
       thể vẫn vượt GAS_SENSOR_TIMEOUT_MS=10s cho một số kênh nếu
       SENSING/GPS-wait kéo dài, y hệt cơ chế age-out đã fix cho
       HB_ONLY nhưng full-wake CHƯA CÓ snapshot tương tự — có thể
       cần áp dụng CÙNG PATTERN snapshot (chụp isConnected ngay
       cuối SENSING, trước khi build payload) cho cả đường full-wake,
       không chỉ HB_ONLY.
    b. Log GPS timeout dài (110000ms, thấy trong log phiên trước)
       kéo dài toàn bộ wake sequence trước khi tới SENDING — rất
       có khả năng đủ để làm một số kênh ZE12A age-out dù ZE12A đã
       broadcast đều đặn suốt session (không phải lỗi ZE12A, mà là
       khoảng cách quá xa giữa "sensor check ổn" và "lúc build
       payload" trong cùng 1 lap dài).
    c. CẦN LOG THẬT có timestamp rõ ràng (dùng mqtt_log_subscriber.py
       đối chiếu với firmware log có timestamp) để đo chính xác
       khoảng cách thời gian giữa lần cuối mỗi kênh ZE12A có frame
       hợp lệ và lúc sensorStatus thực sự được build, so với 10s
       threshold — hiện chưa có bằng chứng đo trực tiếp, chỉ mới
       suy luận từ log gần đúng.

  Nếu giả thuyết (a) đúng: cách fix hợp lý là tổng quát hoá cơ chế
  snapshot đã làm cho HB_ONLY (hb_only_gas_snapshot_type[]/
  hb_only_gas_snapshot_connected[]) thành 1 snapshot dùng chung cho
  CẢ 2 đường (full-wake và HB_ONLY), chụp ngay khi SENSING/sensor
  check kết thúc, thay vì đọc live isConnected() lúc build payload
  — tránh phụ thuộc vào GAS_SENSOR_TIMEOUT_MS's 10s so với khoảng
  cách thời gian thực tế biến động (GPS timeout, mạng chậm, v.v.)
  của từng lap.

============================================================
CÔNG CỤ MỚI TẠO PHIÊN NÀY
============================================================
mqtt_log_subscriber.py (Python, cần `pip install paho-mqtt`) —
subscribe hanoi/air_quality/data/# và hanoi/air_quality/heartbeat/#
trên broker.hivemq.com:1883, log mọi message (timestamp + topic +
payload, không tự cắt bớt) vào log_test_weatherstation.log. Dùng
công cụ này để verify payload không còn bị cắt (mục 2) VÀ để đo
chính xác timing cho bug #3 (đối chiếu timestamp nhận được với
firmware log's timestamp nội bộ). Chạy:
    python3 mqtt_log_subscriber.py
    python3 mqtt_log_subscriber.py --device-id 001   (lọc theo device)

============================================================
QUY TẮC BẮT BUỘC (kế thừa, không đổi)
============================================================
- Nhánh làm việc: ft/heartbeat.
- RE-CLONE/PULL đầu phiên: git pull origin ft/heartbeat ngay khi
  bắt đầu, luôn kiểm tra git status trước để biết có thay đổi local
  chưa commit không.
- KHÔNG tin log/mô tả cũ mà không tự đọc lại code thật.
- KHÔNG có compiler ARM thật trong container — không build được.
  Người dùng tự build + flash + gửi log qua chat. Chỉ kiểm tra được
  cú pháp cơ bản (ngoặc cân bằng) bằng script, KHÔNG thay thế cho
  build thật.
- Board test vật lý duy nhất: STM32H563RIV6.
- Log thật/phép đo tay LUÔN thắng datasheet/giả thuyết khi có xung
  đột — bug #3 phiên này là ví dụ điển hình: giả thuyết ban đầu
  (mux GPIO hỏng) bị loại trừ hoàn toàn nhờ test_ze12a.c standalone.
- KHÔNG khẳng định chắc chắn hơn những gì bằng chứng thật sự cho
  thấy — bug #3 vẫn CHƯA XÁC ĐỊNH được nguyên nhân gốc chắc chắn,
  chỉ mới có nghi vấn mạnh (mục a/b/c ở trên), cần log có timestamp
  chính xác hơn để xác nhận trước khi sửa tiếp.