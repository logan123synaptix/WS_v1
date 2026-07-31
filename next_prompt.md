HANDOFF — HARDWARE BRING-UP TEST, WS_v1 (STM32H563RIV6) — PHIÊN 5

Viết khi sắp hết token. Đọc kỹ các handoff phiên trước (trong lịch sử
chat, ít nhất phiên 3 và 4) trước khi làm gì — phiên này KẾ TIẾP trực
tiếp từ phiên 4. LƯU Ý: handoff phiên 4 có phần lo ngại về việc
container không push được lên GitHub — ĐIỀU ĐÓ ĐÃ ĐƯỢC GIẢI QUYẾT,
người dùng tự lấy code ra và push thành công (xem git log hiện tại có
nhiều commit mới: "new config", "add code fix crash sx_sleep_manager"
x2, "add new code claude", "fix with claude 1" — các fix của phiên 3
đã persist). KHÔNG cần lo về vấn đề push nữa trừ khi gặp lại lỗi tương
tự.

============================================================
QUY TẮC BẮT BUỘC (không đổi qua các phiên)
============================================================
RE-CLONE đầu phiên: git clone https://github.com/logan123synaptix/WS_v1.git
KHÔNG tin log/mô tả cũ mà không tự đọc lại code thật. Container reset
giữa phiên — MỌI THAY ĐỔI CHƯA COMMIT/PUSH TRONG PHIÊN NÀY ĐÃ MẤT.
Không sửa code âm thầm — trình bày nghi vấn → hỏi → chỉ sửa sau khi
có xác nhận rõ ràng.
Comment code tiếng Anh, trao đổi tiếng Việt.
KHÔNG có compiler thật trong container — không build được. Người dùng
tự build + flash + gửi log qua chat.
Board test vật lý duy nhất: STM32H563RIV6.
Datasheet đầy đủ trong Documents/ — LUÔN tra cứu trước khi đoán
thông số. Log thật/phép đo tay LUÔN thắng datasheet khi có xung đột.
Nếu gặp lại lỗi "could not read Username for 'https://github.com'"
khi cố git push từ container — đây là giới hạn môi trường đã biết
(container không có credential git), KHÔNG PHẢI lỗi mới. Xuất patch
file qua present_files và nhờ người dùng tự đồng bộ, như đã làm ở
phiên 3-4.

============================================================
TÌNH TRẠNG TỔNG QUAN ĐẦU PHIÊN 5 — ĐỌC KỸ
============================================================
Dự án đã tiến triển RẤT NHIỀU so với các handoff trước. Tóm tắt người
dùng xác nhận trực tiếp (cuối phiên 4/đầu phiên 5): "hiện tại những
cảm biến đã mua được trên board đã đọc được khi sleep và pub lên
mqtt, có thể dùng shell để config". Cụ thể:

1. **Sleep/wake cycle qua test_sleep.c: ỔN ĐỊNH, không còn treo.**
   - VDD_EXT (1.8V) tắt đúng về 0V sau power-off (xác nhận bằng đo tay).
   - Publish sau khi wake không còn bị treo/mất tín hiệu giữa chừng.
   - Race condition I2C1 lúc WAKING (temp/humi/accel) đã fix từ phiên 3,
     vẫn ổn định.
   Chi tiết kỹ thuật của các fix này xem phiên 3-4's handoff, không
   lặp lại ở đây — chỉ cần biết: ĐÃ XONG, ĐÃ TEST THẬT, ĐỪNG ĐỘNG VÀO
   trừ khi có bug mới phát sinh liên quan.

2. **app.c (app thật, không phải test) — ĐÃ CHUYỂN SANG DÙNG ĐƯỢC,
   với các fix mới trong phiên này (phiên 5, vừa làm xong — CHƯA CÓ
   THỜI GIAN NGƯỜI DÙNG TEST TRÊN BOARD, cần test đầu phiên 6):**

   a. **BUG MQTT BROKER "REPLACE_ME_BROKER_HOST" — ĐÃ XÁC ĐỊNH NGUYÊN
      NHÂN, NGƯỜI DÙNG ĐÃ TỰ SỬA CODE (không phải mình sửa):**
      - app_config.h có USE_THINGSBOARD=1. network_config.c's
        build_defaults() có nhánh #if/#else riêng dựa theo
        USE_THINGSBOARD — khi =1, nó dùng placeholder cứng
        "REPLACE_ME_BROKER_HOST" thay vì broker thật (vì code cũ giả
        định nếu USE_THINGSBOARD=1 thì sẽ dùng Thingsboard client
        thật, nhưng thực tế app.c luôn dùng plain MQTT bất kể cờ này
        — xem comment ở app_config.h dòng ~27-37).
      - Người dùng đã tự thêm macro HOST_THINGSBOART =
        "broker.hivemq.com" vào app_config.h (nhánh #if
        USE_THINGSBOARD) và network_config.c's build_defaults() đã
        đổi sang dùng macro này thay vì "REPLACE_ME_BROKER_HOST".
        Code hiện tại (đầu phiên 5) đã đúng.
      - NHƯNG: network_config_init() chỉ dùng build_defaults() khi
        file config trên flash KHÔNG TỒN TẠI hoặc RỖNG — nếu board đã
        từng chạy qua với code default cũ, giá trị
        "REPLACE_ME_BROKER_HOST" đã bị ghi xuống LittleFS
        (NETWORK_CONFIG_FLASH_PATH) và SẼ TIẾP TỤC ĐƯỢC DÙNG dù code
        default đã sửa đúng, vì flash cũ được ưu tiên đọc trước. Đây
        là hành vi ĐÚNG THIẾT KẾ (flash-persisted, runtime-editable),
        không phải bug — nhưng gây nhầm lẫn nếu không biết.
      - GIẢI PHÁP ĐÃ CHỌN: dùng cơ chế runtime-editable có sẵn qua CLI
        shell (xem mục 3 bên dưới) để set lại host trực tiếp, ghi đè
        giá trị cũ trong flash — KHÔNG cần xoá file/reflash.
      - VIỆC CẦN LÀM ĐẦU PHIÊN 6: hỏi người dùng đã chạy lệnh
        `settings -c -host broker.hivemq.com` (hoặc host thật họ
        muốn) qua CLI chưa, và kiểm tra log/`settings -i` xem host đã
        đúng chưa.

   b. **CLI Shell qua UART6 — ĐÃ CÓ SẴN TỪ TRƯỚC (shell_app.c +
      shell_commands.c), không phải code mới, chỉ mới được người
      dùng biết tới/dùng tới trong phiên 5:**
      - Lệnh `settings -i` xem config hiện tại.
      - Lệnh `settings -c -host <str> -port <n> -clientid <str>
        -user <str> -pass <str> -keepalive <n> -apn <str> -apnuser
        <str> -apnpass <str> -deviceid <str> -pump <s> -duty <0-100>
        -sensing <s> -sleep <s>` để config bất kỳ subset flags nào.
      - Ký tự kết thúc dòng: CHỈ CẦN `\n` (LF) — cli_shell.c's
        cli_shell_receive_char() bỏ qua hoàn toàn ký tự `\r` (return
        ngay không xử lý gì), nên gửi `\r\n` (CRLF) cũng không sao,
        `\r` chỉ bị lờ đi.
      - Sau khi settings -c, KHÔNG CẦN REBOOT — broker mới áp dụng
        vào lần MQTT (re)connect tiếp theo, timing (pump/sensing/
        sleep) áp dụng vào cycle tick tiếp theo (thông báo in ra ngay
        sau lệnh).
      - Baud rate UART6 CHƯA XÁC NHẬN CHÍNH XÁC trong phiên 5 (không
        kịp tra) — nếu người dùng hỏi lại, CẦN ĐỌC sx_board.c's UART6
        init để xác nhận baud rate thật trước khi trả lời.

   c. **Topic MQTT app.c publish lên (khác hẳn test_sleep.c's
      "synaptix/test/sleep_cycle"):**
      - Telemetry (dữ liệu sensor): "hanoi/air_quality/data/" +
        device_id → mặc định "hanoi/air_quality/data/001"
        (build_telemetry_topic(), app.c dòng ~146-148). Publish mỗi
        chu kỳ SENDING (app.c dòng ~591-613, case APP_CYCLE_SENDING).
        Người dùng ĐÃ XÁC NHẬN thấy topic này pub được trên broker.
      - Heartbeat: "hanoi/air_quality/heartbeat/" + device_id →
        "hanoi/air_quality/heartbeat/001". CHỈ publish mỗi
        HEARTBEAT_CYCLE_INTERVAL=4 chu kỳ SENDING một lần (không phải
        mỗi cycle), ~27 phút giữa 2 lần heartbeat theo timing mặc
        định hiện tại (app.c's send_heartbeat_if_due(), dòng
        ~523-541). Người dùng CHƯA XÁC NHẬN đã thấy heartbeat pub hay
        chưa (cần đợi đủ ~27 phút hoặc tạm giảm
        HEARTBEAT_CYCLE_INTERVAL để test nhanh hơn — CHƯA LÀM, cần
        hỏi người dùng có muốn giảm tạm để test không).
      - device_id đổi được qua CLI: `settings -c -deviceid <str>`.

3. **Warning "read failed" khi chạy app.c (KHÁC với test_sleep.c) —
   ĐÃ PHÁT HIỆN NGHI VẤN, CHƯA XÁC NHẬN/CHƯA SỬA:**
   Người dùng xác nhận: chạy app.c thấy warning log giống kiểu
   "TEMP_HUMI measure cmd failed"/"ACCEL_APP linear accel read
   failed" (y hệt bug WAKING đã fix ở test_sleep.c phiên 3). NGHI VẤN
   MẠNH (chưa xác nhận bằng đọc code cụ thể trong phiên 5, HẾT THỜI
   GIAN): app.c's app_process() gọi accel_app_poll()/
   sx_temp_humi_poll() HOÀN TOÀN VÔ ĐIỀU KIỆN mỗi tick (app.c dòng
   ~756-757), KHÔNG CÓ GUARD theo state nào cả — khác hẳn
   test_sleep.c đã có guard `if (s_state != TEST_SLEEP_STATE_WAKING)`
   từ phiên 3. Vì app.c dùng CHUNG sx_sleep_manager.c (cùng
   s_sleep_mgr, cùng 6 wake_steps/6 sleep_steps) với test_sleep.c, RẤT
   CÓ THỂ app.c đang dính lại ĐÚNG BUG RACE-CONDITION mà test_sleep.c
   đã bị trước khi fix (BNO055 còn SUSPEND cho tới wake_steps[5],
   SHT3x cùng bus I2C1, đọc sớm lúc app_mode==APP_MODE_WAKEUP gây race).
   VIỆC CẦN LÀM ĐẦU PHIÊN 6: đọc lại app.c's app_process() đầy đủ (đặc
   biệt phần xử lý APP_MODE_WAKEUP/APP_MODE_SLEEP nếu có state tương
   đương s_state của test_sleep.c), XÁC NHẬN với người dùng đây đúng
   là cùng loại race-condition, rồi áp dụng guard tương tự (bọc
   accel_app_poll()/sx_temp_humi_poll() bằng điều kiện "không đang
   trong wake sequence", logic y hệt fix ở test_sleep.c phiên 3) —
   NHƯNG app.c's state machine phức tạp hơn test_sleep.c (có
   APP_CYCLE_ON_PUMP/SENSING/SENDING/SLEEPING và app_mode riêng), cần
   đọc kỹ để áp guard đúng chỗ, đừng copy máy móc từ test_sleep.c.

============================================================
VIỆC ĐANG LÀM DỞ — TỐI ƯU NĂNG LƯỢNG (bắt đầu cuối phiên 5, CHƯA SỬA
GÌ, chỉ mới điều tra + trình bày nghi vấn cho người dùng)
============================================================
Bối cảnh: người dùng đo dòng tiêu thụ thực tế bằng đồng hồ đo:
  - Full power (đang chạy bình thường): 120mA
  - STOP mode / sleep (kỳ vọng thấp nhất): 50mA — NGƯỜI DÙNG NÓI RÕ
    "đây vẫn chưa được" — tức là kỳ vọng phải thấp hơn nhiều.
  - Giữ nút RESET (mọi thứ đứng yên, không code chạy — baseline phần
    cứng thuần): 22mA — người dùng xác nhận "22mA là đạt" (mốc mục
    tiêu).
  => Có khoảng ~28mA "rò" trong STOP mode cần tìm và cắt.

Đã điều tra (đọc code, KHÔNG có compiler nên KHÔNG build/test được
trong phiên 5) toàn bộ 6 sleep_steps hiện có trong sx_sleep_manager.c
(gps_power_off, modem_power_off, sps30_power_off, pump_off,
gas_sensor_qa_mode, accel_suspend) + đối chiếu với toàn bộ peripheral
thật trong struct Board (sx_board.h).

KẾT LUẬN RÕ RÀNG NHẤT, ĐÃ CÓ BẰNG CHỨNG TỪ CODE (CHƯA SỬA, ĐANG CHỜ
XÁC NHẬN NGƯỜI DÙNG — hỏi xong thì hết token, CHƯA NHẬN ĐƯỢC CÂU TRẢ
LỜI):

1. **W25Q128 (external flash SPI) — NGUỒN RÒ RÕ RÀNG NHẤT, gần như
   chắc chắn cần fix đầu tiên:**
   Driver components/modules/external_flash/sx_W25Q128.c ĐÃ CÓ SẴN
   sx_W25Q128_sleep_on() / sx_W25Q128_sleep_off() (Deep Power-Down
   mode qua lệnh SPI 0xB9/0xAB — chip's own low-power mode, không cần
   GPIO cắt nguồn riêng, "works regardless of board wiring" theo
   comment trong sx_W25Q128.h dòng ~48-49) NHƯNG CHƯA TỪNG ĐƯỢC GỌI Ở
   ĐÂU trong toàn bộ codebase (đã grep xác nhận, 0 kết quả ngoài định
   nghĩa). Flash đứng ở active/standby mode bình thường suốt cả STOP
   mode — không vào Deep Power-Down. Đây khớp với ghi chú "External
   flash chưa có sleep_step" đã có từ 2 handoff trước (phiên 2-3),
   giờ đã xác định rõ driver có sẵn, chỉ cần thêm 1 sleep_step gọi nó.
   VIỆC CẦN LÀM: hỏi người dùng xác nhận, rồi thêm sleep_step thứ 7
   vào sx_sleep_manager.c (s_sleep_steps[6]) gọi
   sx_W25Q128_sleep_on() lúc vào sleep, và thêm 1 wake_step (hoặc mở
   rộng 1 wake_step có sẵn) gọi sx_W25Q128_sleep_off() lúc wake —
   CẦN ĐỌC KỸ sx_sleep_manager.h's struct để biết cần thêm field ctx
   nào (con trỏ tới board.q128) trước khi sửa, và LƯU Ý mảng
   s_wake_steps[6]/s_sleep_steps[6] hiện đang HARDCODE SIZE=6 ở
   sx_sleep_manager_init() — cần đổi cả kích thước mảng lẫn số lượng
   truyền vào sx_sleep_service_init() (hiện đang gọi với 6, 6 — xem
   dòng cuối sx_sleep_manager_init()).

2. **Nghi vấn CHƯA ĐỦ BẰNG CHỨNG, cần điều tra thêm (đã hỏi người
   dùng, CHƯA CÓ CÂU TRẢ LỜI khi hết token):**
   - RTC (RX8130CE, I2C) có cơ chế low-power riêng cần kích hoạt
     không? CHƯA ĐỌC rx8130ce.c/.h trong phiên 5.
   - sx_sleep.c (tier 1, generic STOP-mode) — CHƯA ĐỌC LẠI trong
     phiên 5 để xác nhận HAL_PWR_EnterSTOPMode() được gọi với option
     nào (PWR_LOWPOWERREGULATOR_ON hay MAINREGULATOR_ON,
     PWR_STOPENTRY_WFI hay WFE) — lựa chọn regulator mode ảnh hưởng
     TRỰC TIẾP tới dòng tiêu thụ nền của MCU trong STOP mode (có thể
     là nguồn rò lớn hơn cả W25Q128 nếu đang dùng MAINREGULATOR thay
     vì LOWPOWERREGULATOR). ĐÂY CÓ THỂ LÀ NGHI VẤN QUAN TRỌNG NHẤT,
     CHƯA KỊP ĐIỀU TRA — ưu tiên đọc sx_sleep.c đầu phiên 6 TRƯỚC KHI
     kết luận thêm về W25Q128 có đủ để giải thích hết 28mA hay không.
   - I2C1/SPI1 peripheral clock có tự bị cắt trong STOP mode hay
     không (phụ thuộc RCC config, không chỉ phụ thuộc PWR mode) —
     CHƯA ĐIỀU TRA.
   - ads1115 (power_monitor_app) và sht3x: ĐÃ XÁC NHẬN dùng
     single-shot mode qua đọc code (ads1115.c dòng ~39-43 dùng
     ADS1115_MODE bit trong config register cho single-shot; sht3x.c
     có hàm sht3x_measure_single_shot()) — chip tự về idle giữa các
     lần đo, dòng tiêu thụ µA-level, KHÔNG PHẢI nguồn rò đáng kể, đã
     loại trừ khỏi danh sách nghi vấn.
   - Board CHỈ CÓ 2 chân GPIO enable nguồn thật sự (theo sx_board.h):
     EN_PW_DUST (SPS30) và EN_PW_PUMP (bơm) — CẢ 2 ĐÃ CÓ TRONG
     sleep_steps rồi (sps30_power_off, pump_off). Không có GPIO cắt
     nguồn riêng cho I2C sensors/RTC/flash — các thiết bị này dùng
     chung 1 rail nguồn board, chỉ "sleep" được qua lệnh nội bộ của
     từng chip (như W25Q128's Deep Power-Down), không thể cắt nguồn
     vật lý riêng lẻ bằng phần mềm thuần (cần thêm GPIO/transistor
     phần cứng nếu muốn cắt nguồn thật cho nhóm này — KHÔNG PHẢI việc
     phần mềm có thể tự làm được).

VIỆC CẦN LÀM ĐẦU PHIÊN 6 (thứ tự ưu tiên):
1. Re-clone, đọc git log — CHẠY LỆNH XÁC NHẬN 2 fix phiên 3 vẫn còn
   (xem mục QUY TẮC ở handoff phiên 4 nếu cần, nhưng theo git log đầu
   phiên 5 đã thấy các commit đó tồn tại nên khả năng cao vẫn OK,
   XÁC NHẬN LẠI CHO CHẮC).
2. Đọc sx_sleep.c (tier 1) để xác nhận/loại trừ nghi vấn PWR regulator
   mode — đây là việc điều tra CHƯA KỊP LÀM ở phiên 5, ưu tiên cao vì
   có thể là nguồn rò điện lớn nhất.
3. Đọc rx8130ce.c/.h xem RTC có cần lệnh low-power riêng không.
4. Tổng hợp lại toàn bộ nghi vấn (W25Q128 + PWR mode + RTC + I2C/SPI
   clock) trình bày cho người dùng MỘT LẦN, hỏi rõ muốn sửa theo thứ
   tự nào trước — ĐỪNG tự ý sửa W25Q128 trước khi có bức tranh đầy đủ,
   vì có thể PWR regulator mode mới là nguyên nhân chính, sửa W25Q128
   trước rồi test có thể gây hiểu lầm "đã hết rò" trong khi vẫn còn
   nguồn rò lớn hơn chưa đụng tới.
5. Sau khi người dùng xác nhận hướng, sửa từng phần một, đợi test
   trên board thật giữa mỗi lần sửa (đúng quy tắc dự án — không sửa
   dồn nhiều thứ rồi mới test, khó xác định thứ nào thực sự có tác
   dụng).
6. Song song/sau đó: điều tra bug "read failed" khi chạy app.c (mục
   3 ở trên) — nghi vấn race-condition WAKING giống hệt bug đã fix ở
   test_sleep.c phiên 3, nhưng app.c's state machine phức tạp hơn,
   cần đọc kỹ app_process() trước khi áp fix.
7. Xác nhận với người dùng bug broker "REPLACE_ME_BROKER_HOST" đã
   được giải quyết bằng lệnh CLI `settings -c -host ...` chưa (mục 2a
   ở trên).
8. Hỏi người dùng có muốn tạm giảm HEARTBEAT_CYCLE_INTERVAL để test
   nhanh heartbeat publish không (mục 2c ở trên).
9. Các việc tồn đọng từ phiên 3-4 (ưu tiên thấp hơn, xem handoff phiên
   4 nếu cần chi tiết): timestamp payload sai "2087-00-00T...",
   ACCEL_APP_FILTER_ALPHA chưa điều chỉnh theo period mới, log_debug
   -> log_info cho temp/humi.