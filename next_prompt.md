HANDOFF PROMPT — Sleep/Wake wiring fix + config/bootloader research (WS_v1)
(Viết bởi Claude phiên này, bàn giao cho phiên sau. Đây THAY THẾ hoàn toàn nội dung
"HANDOFF_EOF" cũ từng nằm trong lịch sử chat — bản đó đã lỗi thời ngay từ lúc viết ra,
phần lớn "việc chưa làm" nó liệt kê thực ra đã xong từ lâu qua nhiều commit sau đó.
KHÔNG tin nội dung mô tả tiến độ trong bất kỳ handoff nào, kể cả bản này — luôn tự
`git fetch` + đọc code thật trước khi kết luận.)

repo chính: https://github.com/logan123synaptix/WS_v1.git (nhánh main)
repo mẫu (tham khảo, KHÔNG port nguyên xi): https://github.com/logan123synaptix/WS_v0.git

============================================================
QUY TẮC BẮT BUỘC (không đổi so với các phiên trước)
============================================================
1. KHÔNG tin mô tả "tiến độ" trong handoff. Luôn `git fetch origin` + `git log
   --oneline -15` + đọc code thật (view/grep) trước khi kết luận bất cứ điều gì.
   Người dùng tự commit/push trực tiếp rất thường xuyên.
2. Không sửa âm thầm. Phát hiện bug → trình bày rõ → hỏi lại người dùng → chỉ code
   sau khi có xác nhận, TRỪ những lỗi compile rành rành (ví dụ enum không tồn tại)
   thì có thể sửa thẳng nhưng phải báo cáo rõ đã sửa gì trong response.
3. Toàn bộ comment code bằng tiếng Anh. Trao đổi với người dùng bằng tiếng Việt.
4. Không có compiler thật trong container (thiếu STM32 HAL headers) — chỉ đọc/grep,
   không đề nghị build. Người dùng tự build ở máy họ.
5. Khi tra cứu datasheet/tài liệu kỹ thuật, luôn đọc trực tiếp nguồn hoặc web_search
   chính thức trước khi khẳng định con số/hành vi kỹ thuật.
6. Trước khi tạo/sửa bất kỳ file nào, ĐỌC lại file thật bằng `view`.

============================================================
TRẠNG THÁI GIT THẬT TẠI THỜI ĐIỂM VIẾT HANDOFF NÀY (tự xác minh lại)
============================================================
Remote WS_v1 ở commit 7bfa5dd ("fix flow") — đây là commit người dùng tự push để giải
quyết 3 việc: (a) network config đọc-flash-nếu-có/default-nếu-không, (b) poll GPS mỗi
tick, (c) sleep cả board sau khi publish. (a) và (b) đã làm đúng. (c) có 1 lỗ hổng +
1 bug compile mà Claude phiên này đã tìm ra và VÁ, nhưng CHƯA COMMIT/PUSH — chỉ nằm
trong container tạm của phiên trước, sẽ MẤT khi phiên đó kết thúc. Việc đầu tiên của
phiên này là hỏi người dùng đã tự áp lại các sửa này chưa, hoặc áp lại giúp họ.

Nội dung 2 chỗ sửa (cả hai đều trong app_process()/app_init() thuộc
SynaptiX_FDK/app/app.c, cộng 1 dòng comment trong SynaptiX_FDK/app/app_config.h):

--- Sửa 1 (bug compile thật) ---
File: SynaptiX_FDK/app/app.c, trong app_init()
Trước: s_cycle_state   = APP_CYCLE_IDLE;
Sau:   s_cycle_state   = APP_CYCLE_ON_PUMP;
Lý do: APP_CYCLE_IDLE không tồn tại trong enum app_cycle_state_t (chỉ có ON_PUMP/
SENSING/SENDING/SLEEPING/WAKING — IDLE đã bị bỏ từ một refactor trước, có comment giải
thích rõ tại dòng ~100-108 của app.c). Dòng cũ sẽ KHÔNG BUILD ĐƯỢC nếu để nguyên.

--- Sửa 2 (lỗ hổng logic: sleep không bao giờ thực sự chạy) ---
File: SynaptiX_FDK/app/app.c, trong app_process()
Vấn đề: app_cycle_process()'s case APP_CYCLE_SENDING (dòng ~256-258, code người dùng
tự viết, ĐÚNG và không cần sửa) set s_app_mode = APP_MODE_ENTER_SLEEP ngay sau khi
publish MQTT xong. Nhưng app_process() (hàm gọi app_cycle_process()) TRƯỚC ĐÂY chỉ có
"if (s_app_mode == APP_MODE_FULL_POWER) { app_cycle_process(delta_ms); }" — không có
nhánh nào xử lý APP_MODE_ENTER_SLEEP hay APP_MODE_WAKEUP. Kết quả: s_app_mode đổi
thành ENTER_SLEEP nhưng không ai đọc/dùng giá trị đó — sx_sleep_manager_enter_sleep()
(hàm blocking, ĐÃ VIẾT ĐẦY ĐỦ, đúng implementation, nằm ở sx_sleep_manager.c dòng 282)
KHÔNG BAO GIỜ ĐƯỢC GỌI. Board không bao giờ thực sự vào STOP mode; chương trình đứng
yên ở trạng thái lơ lửng vô hạn.

Đã thêm 2 nhánh else-if còn thiếu vào cuối app_process():

    if (s_app_mode == APP_MODE_FULL_POWER) {
        app_cycle_process(delta_ms);
    } else if (s_app_mode == APP_MODE_ENTER_SLEEP) {
        log_info(TAG, "Entering sleep for %lu ms", (unsigned long)APP_CYCLE_PERIOD_MS);
        sx_sleep_manager_enter_sleep(&s_sleep_mgr, APP_CYCLE_PERIOD_MS / 1000);
        /* Execution resumes here after the RTC wakeup timer fires. */
        s_app_mode    = APP_MODE_WAKEUP;
        s_cycle_state = APP_CYCLE_WAKING;
        log_info(TAG, "Woke up - running wake sequence");
    } else if (s_app_mode == APP_MODE_WAKEUP) {
        sx_sleep_manager_wake_process(&s_sleep_mgr, delta_ms);
        app_cycle_process(delta_ms);
    }

Luồng đầy đủ sau khi vá (đã người dùng xác nhận đúng ý — xem "BỐI CẢNH: SLEEP" bên
dưới): SENDING (publish xong) → set ENTER_SLEEP → app_process() gọi
sx_sleep_manager_enter_sleep() (blocking: chạy hết 6 sleep_steps [gps_power_off,
modem_power_off, sps30_power_off, pump_off, gas_sensor_qa_mode, accel_suspend] RỒI MỚI
cho MCU vào STOP mode thật qua tier-1 sx_sleep_enter_stop(), dùng RTC wakeup timer =
APP_CYCLE_PERIOD_MS/1000 giây) → khi RTC bắn, MCU dậy, hàm return → APP_MODE_WAKEUP →
mỗi tick gọi sx_sleep_manager_wake_process() chạy 6 wake_steps [gps_on, gps_wait_fix,
modem_power_on, modem_wait_ready, gas_sensor_active_mode, accel_resume] → khi
sx_sleep_manager_is_wake_done() true, APP_CYCLE_WAKING tự reset về FULL_POWER/ON_PUMP,
lặp lại từ đầu.

--- Sửa 3 (dọn comment sai lệch, không đổi logic) ---
File: SynaptiX_FDK/app/app_config.h, dòng định nghĩa APP_CYCLE_PERIOD_MS
Comment cũ ghi nhầm APP_CYCLE_PERIOD_MS là "thời gian giữa 2 lần SENDING" (tức chu kỳ
TỔNG). Người dùng đã xác nhận trực tiếp: SLEEP_TIME_MS/APP_CYCLE_PERIOD_MS là THỜI
LƯỢNG NGỦ THẬT SỰ (sleep duration), KHÔNG PHẢI chu kỳ tổng — chu kỳ tổng dài hơn giá
trị này (cộng thêm APP_PUMP_ON_MS + APP_SENSING_MS + thời gian publish). Đã sửa lại
comment cho khớp. Người dùng cũng nói muốn giá trị này config được lúc runtime sau
này (giống hướng flash-config đã làm cho network_config) — CHƯA LÀM, xem "VIỆC CẦN
LÀM" bên dưới.

VIỆC ĐẦU TIÊN: `git status`/`git diff` xem 3 sửa trên còn ở trạng thái uncommitted hay
đã được người dùng tự áp dụng/commit chưa. Nếu remote (`git log origin/main`) đã có
commit mới hơn 7bfa5dd, đọc lại toàn bộ app.c/app_config.h bằng `view` để xem người
dùng đã tự sửa theo cách nào — CÓ THỂ KHÁC với bản vá trên, không giả định giống hệt.

============================================================
BỐI CẢNH: KIẾN TRÚC SLEEP 3-TẦNG (đã hoàn thiện, đã verify đọc code thật phiên này)
============================================================
  Tier 1 — components/peripherals/sleep/sx_sleep.c/.h
    Chỉ biết STOP mode + RTC wakeup timer STM32H5. Dùng hook (pre_stop_hook/
    post_wake_hook) để board cắm logic riêng vào (UART abort/resume, USB IRQ).

  Tier 2 — services/sleep_service/sx_sleep_service.c/.h
    Generic step-sequencing engine. Không biết GPS/modem/SPS30 là gì, chỉ chạy tuần
    tự 1 mảng sx_sleep_step_t[] cho wake và 1 mảng cho sleep.

  Tier 3 — app/user/sx_sleep_manager/sx_sleep_manager.c/.h
    Biết cụ thể GPS/modem/SPS30/pump/ZE12A/accel là gì. Build 6 wake_steps + 6
    sleep_steps (đã liệt kê ở trên), API chính:
      sx_sleep_manager_init(mgr, sleep, modem, gps, sps30_app, pump_gpio, accel_app)
      sx_sleep_manager_enter_sleep(mgr, sleep_sec)   // BLOCKING
      sx_sleep_manager_wake_process(mgr, delta_ms)   // gọi mỗi tick khi đang wake
      sx_sleep_manager_is_wake_done(mgr)
      sx_sleep_manager_reset_wake(mgr)

sx_board.c's board_sleep_pre_stop_hook()/post_wake_hook() (tier-1 hooks) ĐÃ abort cả
4 UART (LTE/GPS/DUST/EXTEND) trước STOP mode và có 4 hàm board_*_uart_resume_it() gọi
on-demand từ đúng wake_steps tương ứng — đã verify đọc code thật, KHÔNG cần sửa gì.

sps30_app_start_cycle() (app/user/sps30_app/sps30_app.c) tự re-arm UART_DUST + bật lại
EN_PW_DUST mỗi khi được gọi — không cần wake_step riêng cho SPS30, vì ON_PUMP→SENSING
(gọi start_cycle()) chạy ngay sau APP_CYCLE_WAKING xong.

============================================================
BỐI CẢNH: NETWORK CONFIG (đã đúng, không cần sửa)
============================================================
app/user/network_config/network_config.c: network_config_init() đọc
sx_storage_size(NETWORK_CONFIG_FLASH_PATH) — nếu >0 thì sx_storage_read() dùng luôn,
nếu không (file không tồn tại hoặc rỗng) thì build_defaults() từ app_config.h's
MQTT_HOST/MQTT_PORT/... rồi network_config_save() ghi lại ngay. Không có kiểm tra
version phức tạp — đúng như người dùng yêu cầu ("đọc file nếu có, dùng default nếu
không có/rỗng"). sx_storage_init() được gọi trong sx_board.c's spi_storage_init()
TRƯỚC app_init() — thứ tự đúng. LƯU Ý NHỎ (không phải bug chặn, chỉ là thiếu
observability): return code của sx_storage_init() bị bỏ qua ở sx_board.c dòng 112,
nếu SPI/flash lỗi lúc boot sẽ tự động rơi về default một cách im lặng, không log lỗi
rõ ràng cho biết phần cứng flash có vấn đề.

============================================================
BỐI CẢNH: VIỆC CHƯA LÀM — RUNTIME CONFIG QUA CLI/RPC (đối chiếu WS_v0)
============================================================
Đã đọc kỹ WS_v0 (SynaptiX/apps/app_settings.c/.h + cli_shell_command.c + app.c's
RPCHandle()) theo yêu cầu người dùng "trước đây repo mẫu set config bằng cách nào".
Kết luận:

WS_v0 dùng 1 struct global APP_Settings_t app_setting (timeSendDataSeconds,
timeSendHeartbeatSeconds, dutyCyclePercent, timeOnPumpSeconds, timeSensingSeconds) với
pattern load y hệt network_config.c của WS_v1 hiện tại (đọc file /settings.json qua
littlefs, kiểm magic_number, nếu sai/thiếu thì dùng default từ app_config.h rồi save
lại) — WS_v1 đã port đúng ý tưởng này cho network_config, CHƯA port cho timing config
(APP_PUMP_ON_MS/APP_SENSING_MS/SLEEP_TIME_MS vẫn là #define compile-time cứng).

WS_v0 có 2 KÊNH ghi runtime vào app_setting, cả hai cuối cùng đều gọi
app_setting_save():
  1. CLI shell qua UART console: lệnh `settings -c -data <n> -pump <n> -sensing <n>
     -duty <n> -hearbeat <n>` (cli_shell_command.c's cli_settings()), có validate ràng
     buộc lẫn nhau (vd: pump < timeSendData - timeSensing).
  2. Thingsboard RPC qua MQTT: app.c's RPCHandle(), method "setParams", nhận JSON qua
     topic RPC_REQUEST_API, parse từng field bằng cJSON, ghi vào app_setting, publish
     lại RPC_RESPONSE_API.

WS_v1 CHƯA có kênh nào trong 2 kênh này. app_config.h's comment (dòng ~11-16, không
đổi bởi Claude) ghi rõ đây là "planned follow-up — NOT implemented yet — qua USB CDC/
MSC (sx_cdc/sx_msc đã tồn tại sẵn trong project), người dùng nói chưa cần code ngay
lúc trước — CẦN HỎI LẠI người dùng có muốn bắt đầu việc này trong phiên tới không, và
nếu có thì qua kênh nào (CDC console kiểu WS_v0's CLI, MQTT RPC giống Thingsboard
style, hay USB MSC file-edit).

============================================================
BỐI CẢNH: BOOTLOADER / OTA — CHỈ CÓ Ở WS_v0, WS_v1 CHƯA CÓ GÌ CẢ
============================================================
Người dùng hỏi WS_v0 có OTA/bootloader không — đã điều tra kỹ bằng cách đọc LINKER
SCRIPT THẬT (theo đúng gợi ý của người dùng, không chỉ nhìn tên file .c), kết luận:

WS_v0 có 2 CƠ CHẾ RIÊNG BIỆT:

1. Bootloader UART-frame tự viết, CÓ HOẠT ĐỘNG THẬT (implementation đầy đủ, không
   phải stub):
   - Thư mục bootloader/ là 1 PROJECT BUILD RIÊNG, có Makefile riêng
     (bootloader/Makefile) và linker script riêng (bootloader/STM32H523xx_FLASH.ld:
     FLASH ORIGIN=0x8000000, LENGTH=32K).
   - Giao thức: frame nhị phân qua UART1 (start=0x7C, end=0xF7, checksum byte-sum đơn
     giản), lệnh READ_FW/ERASE_FW/WRITE_FW/CONNECT_FW/JUMP_FW
     (bootloader/bootloader.h/.c).
   - Kích hoạt: đọc chân PA12 lúc boot — HIGH thì vào chế độ nhận firmware qua UART1,
     LOW thì goto_application() (set MSP+VTOR rồi nhảy tới APPLICATION_ADDRESS =
     0x08008000, set trong bootloader.h).
   - Có IWDG watchdog refresh trong cả 2 nhánh (chờ nút và lúc update).

2. USB DFU class chuẩn qua TinyUSB/AL94 Composite — CÓ ĐĂNG KÝ THẬT lên USB bus
   (usb_device.c dòng 139 gọi USBD_DFU_RegisterMedia(&hUsbDevice, &USBD_DFU_fops)),
   NHƯNG toàn bộ 6 hàm callback thật (MEM_If_Init/DeInit/Erase/Write/Read/GetStatus,
   trong Middlewares/.../usbd_dfu_if.c) LÀ STUB RỖNG CHƯA ĐƯỢC LẬP TRÌNH — chỉ trả về
   USBD_OK, không đọc/ghi/xóa gì thật (MEM_If_Read còn trả về (uint8_t*)USBD_OK = con
   trỏ NULL, sẽ crash nếu bị gọi thật). Đây là template CubeMX generate sẵn, enumerate
   được trên máy tính (dfu-util sẽ thấy thiết bị) nhưng KHÔNG update được firmware
   thật nào.

BUG THẬT PHÁT HIỆN TRONG WS_v0 (không phải WS_v1, chỉ để tham khảo, KHÔNG port bug
này sang): linker script app chính (./STM32H523xx_FLASH.ld, dùng bởi root Makefile)
có FLASH ORIGIN=0x8000000 — GIỐNG HỆT bootloader, KHÔNG dịch lên 0x08008000 dù
bootloader.h's APPLICATION_ADDRESS=0x08008000 giả định app nằm ở đó. Đồng thời
Core/Src/system_stm32h5xx.c's VECT_TAB_OFFSET=0x00 (không đổi cho app). Nếu build+flash
đúng nguyên trạng 2 project này, app sẽ ĐÈ LÊN bootloader thay vì nằm nối tiếp sau nó
— cấu hình build của app root trong repo mẫu CHƯA HOÀN THIỆN đúng ý đồ dual-image, dù
cấu trúc project (2 Makefile + 2 .ld riêng) đúng hướng dual-image thật.

WS_v1 HIỆN TẠI: KHÔNG có thư mục bootloader/ nào (chưa port gì từ WS_v0's
bootloader/). thư viện lib/tinyusb/src/class/dfu/ tồn tại NHƯNG chỉ là mã nguồn
TinyUSB gốc chưa được kích hoạt — đã grep xác nhận KHÔNG có CFG_TUD_DFU nào được định
nghĩa, không có descriptor nào gắn class DFU trong code của project (ngoài thư viện).
OTA/bootloader là MẢNG HOÀN TOÀN CHƯA LÀM ở WS_v1, chưa được người dùng yêu cầu bắt
đầu — chỉ mới dừng ở bước điều tra/so sánh với repo mẫu.

============================================================
VIỆC CẦN LÀM TIẾP THEO — CẦN HỎI NGƯỜI DÙNG THỨ TỰ ƯU TIÊN
============================================================
1. (Nếu chưa) Áp lại/commit 3 sửa ở mục "TRẠNG THÁI GIT" phía trên.
2. Runtime config cho timing (APP_PUMP_ON_MS/APP_SENSING_MS/SLEEP_TIME_MS) — người
   dùng đã xác nhận muốn SLEEP_TIME_MS config được lúc runtime. Cần hỏi: qua kênh nào
   (CDC console, MQTT RPC, USB MSC file), và có gộp chung với network_config_t hay
   tạo 1 config struct riêng (giống WS_v0's tách biệt network vs app_setting)?
3. Bootloader/OTA — CHƯA được yêu cầu bắt đầu, chỉ mới điều tra xong. Hỏi người dùng
   có muốn bắt đầu port bootloader UART-frame kiểu WS_v0 sang WS_v1 không (khuyến
   nghị hướng này hơn DFU vì nó THẬT SỰ hoạt động trong repo mẫu, DFU chỉ là stub
   rỗng chưa ai lập trình) — và nếu làm, cần thiết kế lại linker/vector-offset đúng
   (KHÔNG lặp lại bug ORIGIN=0x8000000 trùng lặp đã phát hiện ở WS_v0).
4. (Từ handoff cũ, có thể đã lỗi thời — TỰ VERIFY LẠI bằng code thật, đừng tin thẳng):
   offline-queue khi mất mạng (SENDING hiện chỉ log_warn rồi drop nếu MQTT không kết
   nối, không có fallback lưu file/littlefs để gửi lại sau — xem app.c dòng ~251-254),
   và CDC/MSC input layer để sửa network_config lúc runtime (network_config có sẵn
   setter/save nhưng chưa ai gọi chúng từ đâu ngoài code — verify lại bằng grep trước
   khi báo cáo).

============================================================
VIỆC ĐẦU TIÊN KHI TIẾP NHẬN — THEO ĐÚNG THỨ TỰ
============================================================
1. git fetch origin + git log --oneline -15 + git status/git diff — xác nhận trạng
   thái thật của app.c/app_config.h, so với mục "TRẠNG THÁI GIT" ở trên.
2. Nếu 3 sửa (bug compile APP_CYCLE_IDLE, thiếu nhánh ENTER_SLEEP/WAKEUP, comment
   APP_CYCLE_PERIOD_MS) chưa có trên remote lẫn chưa uncommitted trong container —
   hỏi người dùng có muốn Claude áp lại không (nội dung chính xác đã ghi đầy đủ ở
   trên, có thể copy thẳng).
3. Hỏi người dùng ưu tiên nào trong "VIỆC CẦN LÀM TIẾP THEO" (mục 2/3/4) trước khi
   code bất cứ gì mới.