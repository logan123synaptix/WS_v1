HANDOFF PROMPT — Sleep Architecture + Sensor App Layer (Weather Station V2, WS_v1)
(Viết bởi Claude phiên trước, bàn giao cho phiên sau vì gần hết token. Đây THAY THẾ
hoàn toàn các next_prompt.md trước đó — nội dung cũ đã lỗi thời, những gì nó mô tả là
"việc chưa làm" phần lớn đã xong. KHÔNG đọc các phiên bản next_prompt.md cũ.)

repo hiện tại: https://github.com/logan123synaptix/WS_v1.git
repo mẫu (tham khảo, KHÔNG port nguyên xi): https://github.com/logan123synaptix/WS_v0.git

============================================================
QUY TẮC BẮT BUỘC — ĐỌC KỸ, ĐÂY LÀ BÀI HỌC THẬT TỪ CÁC PHIÊN TRƯỚC
============================================================
1. KHÔNG tin bất kỳ mô tả "tiến độ" nào trong bản này. Luôn tự `git fetch` +
   `git log --oneline -15` + đọc code thật (view/grep) trước khi kết luận bất cứ điều
   gì. Người dùng tự commit/push trực tiếp RẤT THƯỜNG XUYÊN, kể cả song song trong
   lúc Claude đang làm việc — đã xảy ra nhiều lần thật trong các phiên trước.
2. TRƯỚC KHI PULL: luôn `git fetch origin` rồi `git diff HEAD origin/main --stat` để
   biết phạm vi khác biệt. Nếu có local changes chưa commit VÀ remote có commit mới:
   - Nếu diff cho thấy remote là superset của local (không mất gì) → an toàn
     `git checkout -- .` rồi `git pull` thẳng.
   - Nếu có khả năng xung đột thật → `git stash` → `git pull` → `git stash pop` để
     lộ rõ conflict markers.
   - TUYỆT ĐỐI KHÔNG dùng regex/script tự động xóa conflict marker hàng loạt nếu file
     có NHIỀU HƠN 1 block conflict cách xa nhau — đã xảy ra sự cố thật: regex greedy
     gộp nhầm 2 block ở xa nhau, xóa mất `typedef struct Board{` và nhiều field khác
     trong sx_board.h. Resolve TỪNG block bằng str_replace thủ công, đọc kỹ 2 bên
     trước khi quyết định giữ bên nào.
   - Sau khi resolve, LUÔN `grep -c "<<<<<<<\|=======\|>>>>>>>"` phải ra 0 cho MỌI
     file từng có conflict, và đọc lại toàn bộ file bằng `view` để xác nhận cấu trúc
     nguyên vẹn (đếm brace, đếm typedef struct...).
3. Không sửa âm thầm. Phát hiện bug/lệch thiết kế → trình bày rõ → hỏi lại người dùng
   → chỉ code sau khi có xác nhận. Người dùng đánh giá cao việc bị chặn lại khi phát
   hiện vấn đề an toàn phần cứng thật (xem vụ ADS1115 divider bên dưới) — đừng ngại
   dừng lại hỏi khi có nghi vấn thật, nhưng cũng đừng hỏi những gì đã có bằng chứng
   rõ ràng để tự trả lời được (grep/đọc datasheet trước khi hỏi).
4. Toàn bộ comment code bằng tiếng Anh. Trao đổi với người dùng bằng tiếng Việt.
5. Không có compiler thật trong container (thiếu STM32 HAL headers) — chỉ đọc/grep để
   rà soát, không đề nghị build từng bước nhỏ. Người dùng tự build ở máy họ và đã xác
   nhận build thành công nhiều lần trong các phiên trước.
6. Trước khi tạo/sửa bất kỳ file nào, ĐỌC lại file thật bằng `view` — đừng tin nội
   dung mô tả trong handoff này, nó có thể đã lỗi thời ngay khi bạn đọc.
7. present_files chỉ dùng khi người dùng yêu cầu xem file.
8. QUAN TRỌNG: khi tra cứu datasheet/tài liệu kỹ thuật (SPS30, SHT3x, ZE12A, ADS1115,
   STM32H5 reference manual), LUÔN đọc trực tiếp file trong Documents/ hoặc web_search
   nguồn chính thức trước khi khẳng định con số/hành vi kỹ thuật. Nhiều lần trong các
   phiên trước, việc "nhớ nhầm" hoặc bỏ sót 1 phần tài liệu (ví dụ ZE12A's Question &
   Answer mode) đã dẫn tới kết luận sai ban đầu — chỉ được sửa đúng sau khi người dùng
   chỉ ra và Claude tra cứu lại. Luôn ưu tiên tra cứu lại thay vì tin trí nhớ.

============================================================
TRẠNG THÁI GIT THẬT TẠI THỜI ĐIỂM VIẾT HANDOFF NÀY (tự xác minh lại)
============================================================
Remote WS_v1 ở commit 6d6accd ("fix miss ;"). Local container tại thời điểm viết
handoff này có các file sau ĐÃ SỬA NHƯNG CHƯA COMMIT (do hết token giữa chừng):
  - SynaptiX_FDK/app/user/sps30_app/sps30_app.c       (MỚI VIẾT — xem mục "sps30_app" bên dưới)
  - SynaptiX_FDK/app/user/sps30_app/sps30_app.h       (MỚI VIẾT)
  - SynaptiX_FDK/app/user/sx_temp_humi/sx_temp_humi.c (MỚI VIẾT — xem mục "sx_temp_humi" bên dưới)
  - SynaptiX_FDK/app/user/sx_temp_humi/sx_temp_humi.h (MỚI VIẾT)
  - SynaptiX_FDK/app/user/sx_sleep_manager/sx_sleep_manager.h (sửa nhỏ: thêm #include "sht3x.h")
  - SynaptiX_FDK/board/sx_board.c   (thêm hàm sx_board_get_sps30_power_gpio())
  - SynaptiX_FDK/board/sx_board.h   (thêm khai báo hàm trên)
  - SynaptiX_FDK/components/modules/ze12a/ze12a.c (thêm gas_sensor_switch_to_active_mode(),
    chuyển 3 mảng CMD_* từ .h sang .c làm static const, sửa ze12a_send_command nhận const)
  - SynaptiX_FDK/components/modules/ze12a/ze12a.h (bỏ mảng const khỏi header, thêm khai báo
    gas_sensor_switch_to_active_mode())

VIỆC ĐẦU TIÊN: `git status` xem còn đúng như trên không. Nếu đúng, hỏi người dùng có
muốn Claude commit các file này luôn không (gợi ý message: "add sps30_app, sx_temp_humi,
sps30 power gpio getter, ze12a active-mode switch + header cleanup") trước khi làm việc
mới, để tránh trộn lẫn nhiều việc khác nhau trong 1 commit sau này.

============================================================
BỐI CẢNH: KIẾN TRÚC 3-TẦNG SLEEP (đã hoàn thiện phần lớn, người dùng tự thiết kế phần
                                    lớn kiến trúc này, Claude chỉ hỗ trợ + sửa lỗi)
============================================================
Người dùng chủ động đề xuất kiến trúc 3 tầng để sx_sleep có thể TÁI SỬ DỤNG ĐƯỢC cho
các mạch/dự án khác trong tương lai (không hard-code riêng cho board WS_v1 này). Claude
đồng tình đây là hướng tốt hơn đề xuất ban đầu của mình. Đã hoàn thiện:

  Tier 1 — components/peripherals/sleep/sx_sleep.c/.h
    Chỉ biết về STOP mode + RTC wakeup timer của STM32H5. ZERO kiến thức về UART/I2C/
    SPI/USB nào cả. Dùng cơ chế HOOK (pre_stop_hook/post_wake_hook, kiểu con trỏ hàm
    sx_sleep_hook_t) để board tự cắm logic riêng vào — đây là thiết kế linh hoạt hơn
    cách Claude phiên trước từng đề xuất (mảng UART cố định), người dùng/Claude Code tự
    chuyển sang cách này. sx_sleep_init() nhận (ops, pDriver, pre_stop_hook,
    post_wake_hook, hook_ctx).

  Tier 2 — services/sleep_service/sx_sleep_service.c/.h
    Generic step-sequencing ENGINE. Biết có 1 mảng wake_steps[] và 1 mảng sleep_steps[]
    cần chạy tuần tự, nhưng KHÔNG biết từng step làm gì cụ thể (không GPS, không modem,
    không SPS30 — không gì cả). Mỗi step là 1 cặp {start(ctx), is_done(ctx), ctx, name}
    (struct sx_sleep_step_t). Đây LÀ NƠI TÁI SỬ DỤNG ĐƯỢC cho dự án khác.
    (Lưu ý: thư mục dịch chuyển từ services/sleepmanager/ sang services/sleep_service/,
    tên struct/hàm đổi từ sx_sleep_manager_* sang sx_sleep_service_* — ĐÃ ĐỔI XONG bởi
    người dùng/Claude Code, không phải việc cần làm nữa.)

  Tier 3 — app/user/sx_sleep_manager/sx_sleep_manager.c/.h
    Đây là nơi DUY NHẤT trong toàn bộ sleep stack biết cụ thể "GPS wake step" hay "modem
    power-down step" là gì. Build mảng sx_sleep_step_t[] rồi giao cho tier 2.
    HIỆN TẠI (đã xác nhận đọc code thật): sx_sleep_manager_t chỉ sequencing GPS + modem
    (4 wake_steps: gps_on, gps_wait_fix, modem_power_on, modem_wait_ready; 2 sleep_steps:
    gps_power_off, modem_power_off). CHƯA tích hợp SPS30/SHT3x/ZE12A/ADS1115/pump vào
    step nào cả — đây chính là VIỆC TIẾP THEO cần làm, xem mục "VIỆC CẦN LÀM" bên dưới.
    Struct sx_sleep_manager_t đã có sẵn field pump_io (sx_gpio_t) và sht3x (SHT3X_T*)
    NHƯNG HOÀN TOÀN CHƯA ĐƯỢC DÙNG trong .c — có vẻ người dùng/Claude Code đã chuẩn bị
    chỗ trống cho việc này nhưng chưa điền logic. TỰ ĐỌC LẠI FILE THẬT để xác nhận.

    QUAN TRỌNG: sx_sleep_manager_init() CHƯA ĐƯỢC GỌI Ở ĐÂU CẢ trong sx_board.c hay
    app.c (đã grep xác nhận lúc viết handoff này) — toàn bộ hạ tầng sleep 3-tầng đã
    xây xong nhưng CHƯA được kích hoạt thật trong luồng chạy. sx_board.c hiện chỉ gọi
    sx_sleep_init() (tier 1) với board_sleep_pre_stop_hook/board_sleep_post_wake_hook
    (2 hàm này CHỈ abort UART_LTE + UART_GPS, KHÔNG có UART_DUST/UART_EXTEND/I2C1/SPI1
    — đã xác nhận đọc code thật).

============================================================
BỐI CẢNH: HÀNH VI SLEEP CỦA TỪNG SENSOR (đã tra cứu kỹ, đã người dùng xác nhận đúng)
============================================================
- SPS30: CÓ mạch cắt điện vật lý riêng (EN_PW_DUST, opto TLP181 + MOSFET N-channel
  AO4828, active-HIGH — đã xác nhận qua ảnh schematic người dùng gửi, không phải đoán).
  Trước khi sleep: sps30_stop_measurement() -> sps30_sleep() (SHDLC cmd 0x10) -> cắt
  EN_PW_DUST xuống LOW. Đã viết đầy đủ trong sps30_app.c (xem bên dưới).

- Pump: CÓ mạch cắt điện vật lý riêng (EN_PW_PUMP, cùng cơ chế opto+MOSFET). Chỉ cần
  gọi pump_off() (sx_pump.c — LƯU Ý tên file đã đổi từ pump.c/.h sang sx_pump.c/.h bởi
  người dùng, xem "TRẠNG THÁI GIT" ở next_prompt.md CŨ để biết nhưng đã lỗi thời, tự
  grep lại tên file thật) trước khi sleep. CHƯA được gọi ở đâu trong sleep flow.

- SHT3x: KHÔNG có mạch cắt điện riêng (dùng chung I2C1). Nhưng driver dùng
  sht3x_measure_single_shot() (không phải periodic mode) — theo tài liệu Sensirion/
  RIOT-OS driver (đã web-search xác nhận), single-shot mode khiến sensor TỰ ĐỘNG về
  trạng thái idle/low-power giữa các lần đo, KHÔNG CẦN gọi SHT3X_CMD_BREAK hay bất kỳ
  lệnh sleep nào trước khi vào STOP mode. Người dùng đã tự chỉ ra và Claude xác nhận
  đúng — đây là điểm quan trọng, ĐỪNG thêm sht3x_sleep()/CMD_BREAK vào step nào, không
  cần thiết. (I2C1 tự mất clock trong STOP mode bất kể trạng thái SHT3x, không có gì để
  "sleep" thêm ở tầng ứng dụng.)

- ADS1115: tương tự SHT3x — single-shot mode (ADS1115_readSingleEnded() gửi config rồi
  tự polling bit OS), không có "chế độ đo liên tục" đang chạy cần dừng. Không cần step
  sleep riêng.

- ZE12A: CÓ 2 chế độ vận hành, chuyển qua lệnh UART (Documents/
  ze12a-electrochemical-module-manual-v1_0.md, table 5/6/7) — Active Upload mode (mặc
  định, tự phát khung liên tục — đây là mode gas_sensor_poll() hiện tại giả định) và
  Question & Answer mode (chỉ trả lời khi được hỏi). ĐÃ THÊM 2 hàm vào ze12a.c/.h:
  gas_sensor_switch_to_qa_mode() (table 5) và gas_sensor_switch_to_active_mode() (table
  6, MỚI THÊM bởi Claude phiên này vì thiếu). Ý định đã xác nhận với người dùng: trước
  sleep gọi switch_to_qa_mode(), sau khi wake gọi switch_to_active_mode() để
  gas_sensor_poll() hoạt động lại bình thường. CHƯA được gọi ở đâu trong sleep flow —
  cần thêm vào sx_sleep_manager's step arrays.
  LƯU Ý: cảm biến điện hóa bên trong ZE12A vẫn tiêu thụ điện bình thường dù ở QA mode —
  QA mode chỉ giảm tải UART/xử lý, KHÔNG PHẢI tiết kiệm điện thật của bản thân cảm biến
  (không có cơ chế nào làm được điều đó, đã tra datasheet xác nhận không có lệnh
  sleep/standby thật).

============================================================
BỐI CẢNH: HAI FILE MỚI VỪA VIẾT XONG TRONG PHIÊN NÀY
============================================================

--- SynaptiX_FDK/app/user/sx_temp_humi/sx_temp_humi.c/.h ---
State machine non-blocking 2 trạng thái: IDLE (đếm tới SX_TEMP_HUMI_SAMPLE_PERIOD_MS=
5000ms) -> MEASURING (gửi sht3x_measure_single_shot(HIGHREP_STRETCH), đợi
SX_TEMP_HUMI_MEASURE_WAIT_MS=20ms) -> đọc sht3x_read_measurement() -> quay lại IDLE.
KHÔNG có hàm sleep (lý do: xem mục "HÀNH VI SLEEP" ở trên). API:
  sx_temp_humi_init(sx_temp_humi_t*, SHT3X_T*)
  sx_temp_humi_poll(sx_temp_humi_t*, uint32_t delta_ms)
  sx_temp_humi_is_ready() / get_temperature() / get_humidity()
CHƯA được instantiate/gọi ở sx_board.c hay app.c — cần 1 biến sx_temp_humi_t toàn cục
(hoặc trong Board_t/AppState nào đó) + gọi init() 1 lần + poll() mỗi tick.

--- SynaptiX_FDK/app/user/sps30_app/sps30_app.c/.h ---
State machine non-blocking 8 trạng thái, port từ WS_v0's sps30_app.c NHƯNG:
  - ĐÃ BỎ heuristic mc_pm10p0==mc_pm2p5 (quyết định đã chốt từ nhiều phiên trước, xác
    nhận lại qua đọc datasheet: không phải cơ chế lỗi chính thức, chỉ là suy đoán sai
    của code cũ — không port).
  - THÊM 2 state POWER_ON/SLEEP bao quanh state machine gốc, vì board WS_v1 CÓ mạch
    EN_PW_DUST mà board tham khảo WS_v0 KHÔNG có.
Trình tự: IDLE -> POWER_ON (bật EN_PW_DUST, đợi SPS30_APP_POWER_ON_SETTLE_MS=1000ms,
GIÁ TRỊ NÀY KHÔNG CÓ TRONG DATASHEET, chỉ là ước lượng bảo thủ theo giá trị WS_v0 từng
dùng — CẦN NGƯỜI DÙNG XÁC NHẬN/TEST TRÊN PHẦN CỨNG THẬT) -> WAKE_UP
(sps30_wake_up_sequence()) -> START_MEASUREMENT -> WAIT_MEASUREMENT (5000ms, cũng là
giá trị kinh nghiệm từ WS_v0, datasheet nói "typical" 8s cho số đọc ổn định) ->
READ_MEASUREMENT -> STOP_MEASUREMENT -> DONE.
API:
  sps30_app_init(sps30_app_t*, sx_gpio_t *power_gpio)  // power_gpio lấy từ
                                                         // sx_board_get_sps30_power_gpio()
  sps30_app_start_cycle(sps30_app_t*)   // bắt đầu 1 chu kỳ đo, no-op nếu đang chạy dở
  sps30_app_poll(sps30_app_t*, uint32_t delta_ms)
  sps30_app_is_cycle_done() / has_measurement() / get_measurement()
  sps30_app_reset()  // dọn lại IDLE sau khi đã xử lý xong DONE
  sps30_app_sleep_step_start(void *ctx) / sps30_app_sleep_step_is_done(void *ctx)
      // ĐÚNG CHỮ KÝ sx_sleep_step_t — gọi trực tiếp được từ sx_sleep_manager's
      // sleep_steps[] array, không cần viết wrapper riêng. Cảnh báo: chỉ nên gọi khi
      // cycle đang IDLE/DONE, không phải giữa chừng đo (sẽ log_warn nhưng vẫn cắt điện,
      // caller/sx_sleep_manager chịu trách nhiệm không gọi sai lúc).
CHƯA được instantiate/gọi ở sx_board.c hay app.c.

Trong lúc viết, Claude phát hiện + tự sửa 1 lỗi thiết kế nhỏ trong chính code mình viết
(không phải lỗi người dùng): cách "phát hiện lần đầu vào state POWER_ON" ban đầu dùng
so sánh `state_elapsed_ms == delta_ms` (mong manh, có thể sai nếu delta_ms thay đổi) —
đã sửa bằng cách chuyển hành động bật GPIO sang sps30_app_start_cycle() (chỉ chạy đúng
1 lần/chu kỳ theo bản chất lời gọi, không cần đoán từ thời gian). Đã sửa xong, không
cần làm gì thêm ở điểm này.

============================================================
BỐI CẢNH: CÁC MODULE DRIVER (KHÔNG PHẢI APP LAYER) — ĐÃ HOÀN THIỆN, CHỈ CẦN BIẾT VỊ TRÍ
============================================================
- components/modules/sps30/ — Sensirion SHDLC driver đầy đủ (sps30_uart.c/.h,
  sensirion_uart_hal.c/.h, sensirion_streaming_shdlc.c/.h...). Kiến trúc nhiều-file cố
  ý (người dùng chọn giữ nguyên kiến trúc gốc Sensirion, đã chốt nhiều phiên trước,
  ĐỪNG đề xuất gộp lại thành 1 file).
- components/modules/sht3x/ — driver I2C hoàn thiện, dùng sx_i2c_t abstraction.
- components/modules/ze12a/ — driver UART qua mux TMUX4052 hoàn thiện, giờ có thêm
  QA-mode/active-mode switch (xem trên).
- components/modules/ads1115/ — ĐÃ REFACTOR HOÀN TOÀN từ bản gốc WS_v0 (bỏ biến toàn
  cục + FreeRTOS mutex, chuyển sang struct ADS1115_T + sx_i2c_t, non-RTOS). R16 (shunt
  cho kênh dòng AIN1/adc_current qua INA180A1 gain=20V/V) đã được người dùng xác nhận
  = 0.1 ohm, data rate = 250SPS (xem commit "ADS init in board with R16=0.1ohm & data
  rate: 250" trên git log) — KHÔNG CÒN LÀ OPEN QUESTION NỮA, nhưng comment cũ trong
  sx_board.h có thể vẫn còn ghi "OPEN QUESTION, UNRESOLVED" — TỰ ĐỌC LẠI, có thể cần dọn
  comment đó cho khớp giá trị đã chốt (không phải việc bắt buộc, chỉ là dọn dẹp).
- components/modules/pump/sx_pump.c/.h — (đổi tên từ pump.c/.h) đơn giản, chỉ bật/tắt
  GPIO qua EN_PW_PUMP.

============================================================
BỐI CẢNH: CÁC PHẦN KHÁC ĐÃ CÓ SẴN, CHƯA GHÉP VÀO LUỒNG CHẠY THẬT
============================================================
- app/user/thingsboard/thingsboard_client.c/.h — ĐÃ VIẾT HOÀN CHỈNH (thin layer đúng
  phạm vi: init/poll/is_connected/publish_telemetry/publish_channel/publish_attribute/
  stop, dùng sx_user_mqtt.h bên dưới). CHƯA có nơi nào gọi nó — app.c hiện chỉ có khung
  app_init()/app_process() gần rỗng (11 dòng). Cờ USE_THINGSBOARD trong app_config.h
  hiện đang tắt (=0) — người dùng sẽ tự bật khi cần, KHÔNG PHẢI quyết định của Claude.
- app_config.h có sẵn SLEEP_TIME_MS = 5*60*1000 (5 phút) — do người dùng tự thêm, dùng
  cho chu kỳ sleep chính (khác SX_TIME_IN_SLEEP=60000 mặc định của tier-1, cái đó vẫn
  còn trong sx_sleep.h nhưng SLEEP_TIME_MS mới là giá trị business logic cần dùng).
- RS485 (UART3) — người dùng nói "cứ để lại chưa cần động đến bây giờ". Đã có cấu hình
  baudrate 115200 trong sx_board.c nhưng CHƯA sx_uart_init(), CHƯA driver GPIO cho chân
  DE (RS485_RDE_Pin), CHƯA có module Modbus nào dùng nó.
- sensirion_shdlc.c/.h (bản buffer-based API của Sensirion, KHÔNG dùng — sps30_uart.c
  chỉ dùng streaming API) — code chết, người dùng chưa xác nhận muốn xóa, ĐỪNG tự ý xóa.

============================================================
VIỆC CẦN LÀM TIẾP THEO — THEO THỨ TỰ ĐỀ XUẤT
============================================================

1. (Nếu chưa làm) Commit các file đang staged/modified liệt kê ở mục "TRẠNG THÁI GIT".

2. Mở rộng sx_sleep_manager (tier 3) để tích hợp SPS30/pump/ZE12A vào sleep/wake flow:
   - Thêm sleep_step gọi sps30_app_sleep_step_start()/is_done() (đã có sẵn, chữ ký
     khớp thẳng sx_sleep_step_t, chỉ cần thêm vào mảng s_sleep_steps[] và tăng count).
   - Thêm sleep_step gọi pump_off() (sx_pump.c) — cần viết wrapper start()/is_done()
     nhỏ vì pump_off() không có sẵn chữ ký khớp sx_sleep_step_t.
   - Thêm sleep_step gọi gas_sensor_switch_to_qa_mode() — tương tự, cần wrapper.
   - Thêm wake_step gọi gas_sensor_switch_to_active_mode() — tương tự.
   - KHÔNG cần thêm step cho SHT3x/ADS1115 (xem lý do ở mục "HÀNH VI SLEEP").
   - Cập nhật sx_sleep_manager_init() để nhận thêm con trỏ sps30_app_t*, sx_gpio_t*
     (pump), và không cần gì thêm cho ZE12A (module đó tự quản lý state nội bộ qua
     static, giống UART instance).
   - Struct sx_sleep_manager_t đã có sẵn field pump_io/sht3x chưa dùng — QUYẾT ĐỊNH:
     có dùng field sht3x đã khai báo sẵn không (nếu không cần sleep step cho SHT3x theo
     kết luận trên, field này có thể thừa — hỏi người dùng có muốn xóa field sht3x khỏi
     struct hay giữ lại phòng khi cần sau này).

3. Mở rộng board_sleep_pre_stop_hook()/post_wake_hook() (sx_board.c) để abort/resume
   thêm UART_DUST (SPS30) và UART_EXTEND (ZE12A) — hiện chỉ có UART_LTE/UART_GPS. Cân
   nhắc: SPS30 UART có cần abort không nếu EN_PW_DUST đã cắt điện hoàn toàn trước đó
   (qua sps30_app_sleep_step_start(), chạy TRƯỚC khi vào STOP mode)? Có thể không cần
   abort UART riêng nếu nguồn đã cắt — nhưng ZE12A vẫn có điện (không cắt được), UART
   của nó có thể cần abort để tránh nhận byte rác trong lúc STOP mode. Đây là quyết
   định thiết kế cần hỏi người dùng, không tự ý chọn.

4. Gọi sx_sleep_manager_init() thật ở đâu đó (sx_board.c cuối sx_board_init(), hoặc
   app.c's app_init()) — hiện CHƯA được gọi ở đâu cả. Cần các con trỏ: &board.modem,
   &board.gps, sx_board_get_sps30_power_gpio() (đã có), và biến sps30_app_t/sx_temp_humi_t
   toàn cục nào đó cần được khai báo trước.

5. Instantiate sx_temp_humi_t và sps30_app_t ở đâu đó (gợi ý: Board_t trong sx_board.h,
   hoặc biến static trong app.c — CHƯA QUYẾT ĐỊNH, hỏi người dùng theo đúng tinh thần
   "app layer không nên biết chi tiết driver, nhưng cũng không nên mọi thứ đều nhét
   vào Board_t" đã thảo luận trước đây khi thiết kế thư mục app/user/*).

6. Viết state machine chính trong app.c (hiện gần như rỗng): IDLE -> ON_PUMP ->
   SENSING -> SENDING -> SLEEPING -> WAKING -> IDLE, gọi vào sps30_app/sx_temp_humi/
   gas_sensor/power_monitor (CHƯA VIẾT, xem mục 7) + thingsboard_client + sx_sleep_manager.
   Tham khảo state machine gốc WS_v0/SynaptiX/apps/app.c (đã đọc kỹ ở phiên trước, có
   sensor_task + reading_sensor_task 2 task riêng — nhưng WS_v1 non-RTOS nên cần viết
   lại thành 1 hàm poll tuần tự, không phải 2 task song song).

7. CHƯA VIẾT: gas_sensor_app (poll ZE12A, tương đương lớp app cho driver ze12a.c —
   hiện gas_sensor_poll() được gọi trực tiếp từ đâu đó hay chưa gọi ở đâu cả, cần grep
   xác nhận) và power_monitor_app (đọc ADS1115: adc_vol/adc_current). Tên thư mục đã
   thống nhất với người dùng ở phiên trước: gas_sensor_app/, power_monitor_app/ (khác
   với sx_temp_humi/sps30_app đã dùng tên người dùng tự đặt, không theo đúng convention
   "_app" suffix — đây là bình thường, người dùng có quyền đặt tên khác lúc code thật).

============================================================
VIỆC ĐẦU TIÊN KHI TIẾP NHẬN — THEO ĐÚNG THỨ TỰ
============================================================
1. git fetch origin + git log --oneline -15 + git status — xác nhận trạng thái thật,
   so sánh với mục "TRẠNG THÁI GIT" ở trên, KHÔNG tin nếu khác.
2. Đọc lại toàn bộ sx_sleep_manager.c/.h, sx_sleep_service.c/.h, sx_sleep.c/.h bằng
   view — xác nhận đúng kiến trúc 3-tầng mô tả ở trên còn nguyên vẹn.
3. Đọc sps30_app.c/.h và sx_temp_humi.c/.h (mới viết trong phiên này) bằng view —
   xác nhận đúng nội dung mô tả, đừng tin tóm tắt trong handoff.
4. Grep tìm gas_sensor_poll() và ADS1115_readSingleEnded() được gọi ở đâu hiện tại
   (có thể chưa có ở đâu cả — cần xác nhận thay vì giả định).
5. Hỏi người dùng xác nhận thứ tự ưu tiên trong mục "VIỆC CẦN LÀM TIẾP THEO" trước khi
   code — đặc biệt câu hỏi ở bước 3 (có cần abort UART_DUST không nếu đã cắt điện) và
   bước 5 (đặt sps30_app_t/sx_temp_humi_t instance ở đâu).
HANDOFF_EOF

Đã push code lên git, đồng thời bạn ko hiểu ý tưởng à. Đầu tiên nó sẽ chạy với các thông số được config trong app_config, sau đó (mở rộng sau này) sau khi truyển host, port,.... từ tầng application UI xuống thì nó nhận và lưu vào flash rồi dùng cái đấy về sau, có nghĩa hướng code sẽ là phải đọc file nếu có file đấy tồn tại thì đọc file rồi dùng các thông số đã lưu, nếu ko có file hoặc có file nhưng rỗng thì dùng default, đồng thời ở trong app tôi chưa thấy bạn poll gps và tiếp nữa vấn đề sleep bạn vẫn chưa giải quyết được theo ý của tôi, bạn phải hiểu là khi mạch sleep , master là stm32 sleep, thì các module đi kèm mới sleep theo ví dụ khi publish xong các thông tin (những thông tin gì thì bạn tự đọc repo mẫu để biết), sau khi pub xong nó sẽ rơi vào sleep (cả mạch luôn)