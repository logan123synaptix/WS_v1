HANDOFF PROMPT — Heartbeat parity với v0 + MQTT RPC/Offline Queue (WS_v1)
(Viết bởi Claude phiên này, bàn giao cho phiên sau. Đây THAY THẾ hoàn toàn nội dung
"next_prompt.md"/handoff cũ — bản trước đã lỗi thời ngay từ lúc viết ra, vì mô tả
tiến độ dựa trên container tạm chưa push, không phải trạng thái remote thật. KHÔNG
tin nội dung mô tả tiến độ trong bất kỳ handoff nào, kể cả bản này — luôn tự
`git fetch` + đọc code thật trước khi kết luận bất cứ điều gì.)

repo chính: https://github.com/logan123synaptix/WS_v1.git (nhánh main)
repo mẫu (tham khảo, KHÔNG port nguyên xi): https://github.com/logan123synaptix/WS_v0.git

============================================================
QUY TẮC BẮT BUỘC (không đổi so với các phiên trước)
============================================================
1. KHÔNG tin mô tả "tiến độ" trong handoff hay trong transcript chat cũ. Luôn
   `git fetch origin` + `git log --oneline -15` + đọc code thật (view/grep) trước khi
   kết luận bất cứ điều gì. Người dùng tự commit/push trực tiếp rất thường xuyên
   (kể cả tự sửa/port lại đúng patch Claude vừa đề xuất), và container của phiên
   trước có thể đã mất code chưa kịp push (đã xảy ra nhiều lần với chính dự án này —
   xem "BÀI HỌC" bên dưới).
2. Không sửa âm thầm. Phát hiện bug → trình bày rõ → hỏi lại người dùng → chỉ code
   sau khi có xác nhận, TRỪ những lỗi compile/logic rành rành thì có thể sửa thẳng
   nhưng PHẢI báo cáo rõ đã sửa gì, ở đâu, vì sao, trong response — không được im
   lặng.
3. Toàn bộ comment code bằng tiếng Anh. Trao đổi với người dùng bằng tiếng Việt.
4. Không có compiler thật trong container (thiếu STM32 HAL headers) — chỉ đọc/grep,
   không đề nghị build. Người dùng tự build ở máy họ.
5. Khi tra cứu datasheet/tài liệu kỹ thuật: tài liệu AT command chính thức A76XX ĐÃ
   CÓ SẴN TRONG REPO tại `Documents/a76xx_at_cmd.md` — đọc trực tiếp file này trước
   (grep theo tên lệnh, ví dụ "COPS"), KHÔNG cần web_search trừ khi file không có
   thông tin cần thiết. Các datasheet sensor khác cũng nằm trong `Documents/`
   (gps_gp02_aithinker.md, Datasheet_SHT3x_DIS.md, SPS30_dust_sensor.md, a7677s.md,
   ze12a-electrochemical-module-manual-v1_0.md, bno055.md).
6. Trước khi tạo/sửa bất kỳ file nào, ĐỌC lại file thật bằng `view`.
7. Trước khi dùng bất kỳ API/macro nào (kể cả có sẵn từ lâu trong project), xác minh
   chữ ký thật bằng cách đọc định nghĩa — đã có tiền lệ 1 macro sai chữ ký nằm im
   trong code nhiều đời mà không ai gọi tới nó cả (xem "BÀI HỌC" mục 2).
8. Người dùng có thể tự làm việc song song ở máy họ (ví dụ: toàn bộ thư mục
   `SynaptiX_FDK/BOOTLOADER_WS/` — bootloader STM32H563 + TinyUSB, ~130 file — được
   người dùng tự viết và tự push, KHÔNG phải việc của Claude, không cần review trừ
   khi người dùng yêu cầu). Đừng nhầm lẫn code người dùng tự thêm với sản phẩm của
   phiên chat.

============================================================
BÀI HỌC TỪ CÁC PHIÊN GẦN ĐÂY — ĐỌC KỸ TRƯỚC KHI LÀM GÌ
============================================================
1. MẤT CODE CHƯA PUSH (đã xảy ra nhiều lần với dự án này): 1 phiên viết xong
   network_config mở rộng + cli_commands nhưng chưa commit/push — mất khi container
   đóng. Một lần khác, phiên viết mqtt_rpc.c/.h + sửa build files nhưng QUÊN áp phần
   sửa app.c (include + wiring on_message/on_connected) — mqtt_rpc.c tồn tại, được
   build, nhưng KHÔNG BAO GIỜ ĐƯỢC GỌI. => LUÔN nhắc người dùng commit + push NGAY
   sau khi code xong, và grep xác nhận MỌI mảnh đã thực sự nối với nhau trước khi
   báo "đã xong".

2. MACRO SAI CHỮ KÝ NẰM IM NHIỀU ĐỜI: file_io.h's `opendir(path)` từng expand ra
   `lfs_dir_open(CONFIG_LITTLEFS, path)` — thiếu tham số `lfs_dir_t *dir`. Đã sửa
   (đã confirmed trên remote, xem mục "TRẠNG THÁI GIT" bên dưới). => 1 API tồn tại
   lâu trong codebase KHÔNG có nghĩa là nó đúng nếu không ai từng gọi nó.

3. HANDOFF CÓ THỂ MÔ TẢ SAI VỊ TRÍ CODE THẬT: 1 phiên trước từng báo "đã thêm
   motionState + gps vào heartbeat", nhưng khi phiên sau đọc lại code thật, 2 field
   đó thực ra nằm trong `build_telemetry_payload()` (đã có sẵn từ trước), KHÔNG có
   trong `build_heartbeat_payload()` — nghĩa là log trong transcript nói "đã xong"
   không khớp code thật. Phiên sau đó phải tự phát hiện qua đọc code trực tiếp rồi
   mới sửa lại đúng chỗ. => Không chỉ git log/diff, mà bản thân MÔ TẢ trong transcript
   (kể cả do Claude tự viết ở phiên trước) cũng phải đối chiếu với code thật, không
   suy diễn "đã confirm trong chat thì chắc đúng".

4. MODULE "TỒN TẠI VÀ BUILD ĐƯỢC" KHÔNG CÓ NGHĨA LÀ ĐÃ WIRING (lặp lại kiểu bug
   mqtt_rpc trước đây, lần này ở BOOTLOADER_WS/): logic bootloader
   (`bootloader/bootloader.c`, `boot_flash.c`) và cấu hình TinyUSB DFU
   (`TUSB/tusb_config.h`, `usb_descriptors.c`) đều viết xong, đọc độc lập thấy hợp
   lý — nhưng `Core/Src/main.c` chỉ init HAL/GPIO/ICACHE/UART/USB-PCD rồi rơi vào
   `while(1){}` RỖNG, không hề gọi `bootloader_init()`/`bootloader_process()`,
   không có `tud_init()`/`tud_task()` nào cả, và không có implementation thật cho
   `jump_to_application`/`read_boot_pin` (2 tham số bắt buộc của
   `bootloader_init()`). Xem mục "BOOTLOADER_WS — TRẠNG THÁI THẬT" bên dưới để biết
   chính xác còn thiếu gì trước khi tin bootloader đã "xong".

============================================================
TRẠNG THÁI GIT THẬT TẠI THỜI ĐIỂM VIẾT HANDOFF NÀY (tự xác minh lại)
============================================================
Remote WS_v1 ở commit f873e2c ("add submodule tiny usb in BOOTLOADER_WS"), sau các
commit gần đây:
  f873e2c add submodule tiny usb in BOOTLOADER_WS  <- .gitmodules mới, trỏ
                                                        SynaptiX_FDK/BOOTLOADER_WS/
                                                        libs/tinyusb/tinyusb tới
                                                        https://github.com/hathach/
                                                        tinyusb.git (PHẢI
                                                        `git submodule update --init
                                                        --recursive` sau khi clone/
                                                        pull, nếu không thư mục đó
                                                        sẽ trống)
  acbdb0f modify handoff prompt                      <- người dùng tự áp bản handoff
                                                        do Claude viết ở phiên trước,
                                                        không tự sửa thêm gì khác
  5e3a034 modify topic heartbeat
  cf96953 add modify according claude   <- gồm cả BOOTLOADER_WS/ (do người dùng tự
                                            thêm, không phải Claude) + phần operator
                                            getter (do Claude phiên trước viết,
                                            người dùng tự áp + push)
  d5b3c5d apply mqtt_rpc_timestamp_heartbeat.patch
  49ee824 delete old code version
  40dccda modify add topic device id
  ...

Đã XÁC NHẬN THẬT (đọc trực tiếp file trên remote, không suy đoán) — các hạng mục
sau ĐÃ HOÀN THÀNH và đã có trên remote, KHÔNG cần làm lại:

--- 1. file_io.h's opendir() macro bug ---
Đã sửa đúng chữ ký `opendir(dir, path)` (SynaptiX_FDK/services/filesystem/
file_io.h dòng 87). offline-queue trong app.c KHÔNG dùng macro này nữa — dùng
`sx_storage_list()` (sx_ex_storage.h) thay thế, xem mục 3 bên dưới.

--- 2. MQTT RPC wiring (app/user/mqtt_rpc/mqtt_rpc.c/.h) ---
Đã wiring đầy đủ trong app.c:
  #include "mqtt_rpc.h"
  mqtt_cfg.on_message   = mqtt_rpc_on_message;
  mqtt_cfg.on_connected = mqtt_rpc_init;
Không còn là dead code như handoff cũ từng cảnh báo.

--- 3. Offline telemetry queue (app.c) ---
Đã có đầy đủ: #define OFFLINE_QUEUE_DIR "/queue", OFFLINE_QUEUE_MAX_FILES 20,
offline_queue_save()/offline_queue_resend_one(), gọi trong APP_CYCLE_SENDING.
Dùng sx_storage_list()/sx_storage_write()/sx_storage_read()/sx_storage_delete()
(sx_ex_storage.h), KHÔNG gọi lfs_dir_open trực tiếp và KHÔNG dùng macro opendir/
readdir/closedir của file_io.h (dù macro đó đã đúng chữ ký) — xem comment trong
app.c dòng ~156-181 giải thích lý do lịch sử.
3 giới hạn CHƯA xử lý (xem "VIỆC CẦN LÀM TIẾP THEO" mục 1 bên dưới) — GIỮ NGUYÊN,
CHƯA có phiên nào động vào từ handoff trước tới giờ:
  a. publish "thành công" chỉ dựa vào sx_user_mqtt_is_connected() trước khi gọi
     sx_user_mqtt_publish() (hàm này trả về void, không có ACK thật) — file bị xóa
     ngay sau khi gọi publish, không chờ xác nhận broker đã nhận.
  b. OFFLINE_QUEUE_MAX_FILES=20 là số tạm chọn, chưa tính theo dung lượng flash
     thật.
  c. Tên file dùng counter RAM s_offline_queue_seq, reset về 0 mỗi lần
     reset/mất điện — file cũ chưa gửi có thể bị ghi đè nếu reset xảy ra giữa
     chừng.

--- 4. Heartbeat parity với WS_v0 — MỚI HOÀN THÀNH Ở PHIÊN NÀY, đã confirm trên
       remote thật ---
So sánh field-by-field build_heartbeat_payload() (app.c) với WS_v0's
heartBeatPayload() (SynaptiX/apps/app.c dòng 137-164 trong repo mẫu) đã xong.
Trạng thái hiện tại — TẤT CẢ field có ý nghĩa của v0 đã có mặt hoặc có lý do chính
đáng để KHÔNG có:

  - deviceID, timestamp, firmwareVersion, uptimeMs, signalStrength: đã có từ
    trước, ngang v0.
  - operator (tên nhà mạng, dạng chữ vd "Viettel Mobile"): MỚI THÊM. Cần 1 thay
    đổi ở tầng driver modem (A7677S vốn không có tính năng này sẵn — CMD_COPS_SET
    trước đây gửi "AT+COPS=0" chỉ set auto-select, không set định dạng tên trả về,
    nên AT+COPS? trả về mã MCC/MNC dạng số như "46001" chứ không phải tên chữ).
    Đã sửa CMD_COPS_SET thành "AT+COPS=0,0" (mode=0 auto, format=0 long
    alphanumeric — xác nhận từ Documents/a76xx_at_cmd.md mục 4.2.2, ví dụ mẫu
    "+COPS:0,0,\"T-Mobile\",8"), thêm field cache a7677s_t.operator_name[33],
    parse trong cb_cops_query() (tách chuỗi trong dấu ngoặc kép), thêm getter
    modem_ops_t.get_operator + a7677s_get_operator() + đăng ký vào a7677s_ops,
    thêm wrapper sx_user_mqtt_get_operator() (app/user/sx_mqtt/sx_user_mqtt.c/.h),
    và field "operator" trong build_heartbeat_payload(). Chỉ có 1 file
    (a7677s.c) implement modem_ops_t nên không có driver nào khác bị ảnh hưởng
    bởi field mới trong vtable.
  - motionState, latitude/longitude (gps): MỚI THÊM vào build_heartbeat_payload().
    Trước đó 2 field này CHỈ có trong build_telemetry_payload() (dù 1 log ở phiên
    trước từng báo nhầm là "đã thêm vào heartbeat" — xem "BÀI HỌC" mục 3). Đã sửa
    dùng đúng getter/pattern y hệt bản trong telemetry
    (accel_app_is_movement_detected(&s_accel_app), board.gps.latitude/longtitude
    với cùng "non-zero lat/long nghĩa là có fix" convention), đặt ngay sau
    railVoltage/railCurrent, trước block sensors trong build_heartbeat_payload().
  - soc (% pin): KHÔNG port — đã xác nhận trong WS_v0, dòng set
    weatherstation.soc bị comment lại, nghĩa là field này trong v0 chỉ đọc giá trị
    rác chưa khởi tạo, chưa từng hoạt động thật. v1 dùng railVoltage/railCurrent
    thay thế (khái niệm khác nhưng hợp lý hơn về mặt kỹ thuật với board này).
  - memory (total/storageUsed): KHÔNG port — trong v0 là hardcode giả (2048, 128
    cứng, không đo thật), port nguyên xi sẽ vi phạm nguyên tắc "không fake data"
    của codebase v1.
  - sensorStatus: v1's "sensors" object (7 sensor: tempHumi/sps30/co/so2/no2/o3/
    h2s) ĐẦY ĐỦ HƠN v0 (6 sensor, thiếu no2 dù có field weatherstation.no2).
  - timestamp: v1 dùng ISO-8601 chuẩn hóa qua RTC, TỐT HƠN v0 (chuỗi NTP thô
    không chuẩn hóa).

=> KẾT LUẬN: heartbeat v1 hiện TƯƠNG ĐỒNG ĐẦY ĐỦ VÀ CẢI TIẾN HƠN v0. Không còn
khoảng trống field-level nào cần lấp thêm ở mục này, TRỪ KHI người dùng muốn thêm
field hoàn toàn mới không có trong v0 (không thuộc phạm vi "đạt parity với v0").

--- 5. BOOTLOADER_WS/ — TRẠNG THÁI THẬT (người dùng tự viết/tự push, Claude chỉ
       ĐỌC và báo cáo, KHÔNG tự sửa gì) ---
Thư mục SynaptiX_FDK/BOOTLOADER_WS/ là dự án con RIÊNG BIỆT (CMake project độc lập,
CMAKE_PROJECT_NAME=bootloader), không phải phần của app chính WS_v1 firmware.
Người dùng tự viết toàn bộ, Claude phiên này chỉ đọc để nắm tình hình theo yêu cầu.
KHÔNG chủ động sửa/thêm gì vào thư mục này trừ khi người dùng yêu cầu cụ thể.

Mục đích (theo readme.md): bootloader STM32H563 hỗ trợ cập nhật firmware qua USB
DFU, dùng partition A/B (primary/secondary) + factory fallback + vùng scratch
trung gian để swap an toàn.

Layout flash thật (bootloader/flash_define.h, tổng 189/256 sector 8KB):
  0x08000000  bootloader           7 sector  (56 KB)
  +56KB       partition metadata   1 sector  (8 KB)   — có magic number 0xDEADBEEF
  +8KB        primary app          60 sector (480 KB)
  +480KB      secondary app        60 sector (480 KB)
  +480KB      factory app          60 sector (480 KB)
  +480KB      scratch              1 sector  (8 KB)

Kiến trúc logic (bootloader/*.c/.h) — ĐÃ VIẾT XONG, đọc độc lập thấy hợp lý:
  - boot_flash.h/.c: vtable BootFlashFunctions_t (init/erase/write/read) tách khỏi
    logic partition. Nhánh CFG_BOOT_MCU==BOOT_MCU_H5 có implementation thật dùng
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, ...), xử lý đúng 2-bank layout
    của STM32H5 (sector > 127 → bank 2, erase_init.Sector = sector % 128).
    LƯU Ý: flash_define.h dòng ~53 có `// #define CFG_BOOT_MCU BOOT_MCU_H5` bị
    COMMENT — đây KHÔNG phải bug, macro được inject qua
    bootloader/CMakeLists.txt's target_compile_definitions
    (CFG_BOOT_MCU=BOOT_MCU_H5), dòng comment trong header chỉ là dead/không cần
    thiết. Đã verify: build thật KHÔNG rơi vào nhánh "no specific implementation"
    ở cuối boot_flash.c.
  - bootloader.c: bootloader_init() đọc lại partition từ flash, tự phát hiện
    corruption/lần boot đầu qua magic_number mismatch và tự khởi tạo lại toàn bộ
    partition table + ghi xuống flash. boot_swap_firmware() (chỉ compile khi
    SECONDARY_FLASH && SCRATCH_FLASH đều 1) swap primary<->secondary từng block
    kích thước = scratch_size, qua scratch làm trung gian — không cần đủ RAM chứa
    cả 2 ảnh cùng lúc, cách làm chuẩn cho MCU RAM hạn chế.
  - boot_err.h: mã lỗi rõ ràng, không có gì bất thường.

VẤN ĐỀ THẬT — CHƯA WIRING, kiểm tra bằng grep trực tiếp trong Core/ và TUSB/, xác
nhận 0 kết quả cho các hàm dưới:
  1. Core/Src/main.c KHÔNG gọi bootloader_init()/bootloader_process() ở bất kỳ đâu.
     Toàn bộ nội dung main() sau khi init HAL/MPU/clock/GPIO/ICACHE/UART/USB-PCD
     là `while(1) {}` RỖNG. Bootloader_t chưa từng được khai báo/khởi tạo trong
     Core/.
  2. TinyUSB app-layer CHƯA init: usb.c (Core/Src/) chỉ dừng ở tầng PCD (HAL
     peripheral controller — HAL_PCD_Init), KHÔNG có tud_init()/tud_task() ở đâu
     cả, dù TUSB/tusb_config.h đã bật CFG_TUD_DFU=1 (standalone DFU-mode) với
     CFG_TUD_DFU_XFER_BUFSIZE=8192 và usb_descriptors.c đã có device/config/string
     descriptor đầy đủ (VID=0xCAFE/PID=0x4DF0 — comment ghi "replace with your
     VID", vẫn là giá trị mẫu, CHƯA đổi; 1 alt-setting DFU tên "Application").
     Các callback tud_dfu_*_cb (get_timeout/download/upload — nơi thật sự ghi dữ
     liệu nhận từ host vào flash qua boot_flash_write()) CHƯA thấy implementation
     nào trong repo.
  3. Không có implementation cụ thể nào cho 2 tham số bắt buộc của
     bootloader_init(): `jump_to_application` (con trỏ hàm nhảy sang địa chỉ ứng
     dụng — thường cần set MSP + VTOR rồi branch, chưa thấy) và `read_boot_pin`
     (đọc GPIO để vào chế độ update — ENTER_BOOT_UPDATE_MODE=0 đã định nghĩa
     trong bootloader.h nhưng chưa thấy hàm đọc GPIO thật nào gọi tới nó).

=> KẾT LUẬN: phần "não" (bootloader.c/boot_flash.c) và phần cấu hình DFU tĩnh
(tusb_config.h/usb_descriptors.c) đã viết xong độc lập, nhưng CHƯA CÓ CẦU NỐI —
main.c chưa gọi tới bootloader hay TinyUSB app-layer nào cả. Đây đúng kiểu bug
"module tồn tại, build được, nhưng không bao giờ được gọi" mà dự án này đã gặp
trước đây với mqtt_rpc (xem BÀI HỌC mục 1 và mục 4). Nếu người dùng báo bootloader
đã dùng được, phiên sau PHẢI tự đọc lại Core/Src/main.c bằng `view` để xác nhận đã
có wiring thật (không suy đoán từ chỗ "trước có ghi chưa xong" mà bỏ qua khả năng
người dùng đã tự thêm phần thiếu).

VIỆC ĐẦU TIÊN CỦA PHIÊN NÀY: `git fetch origin` + `git log --oneline -15` +
`git submodule update --init --recursive` (bắt buộc nếu cần đọc
BOOTLOADER_WS/libs/tinyusb/tinyusb/, submodule mới thêm ở f873e2c) + đọc lại
app.c/a7677s.c/a7677s.h/modem_ops.h/sx_user_mqtt.c VÀ BOOTLOADER_WS/Core/Src/
main.c/usb.c bằng `view` để tự xác nhận những gì handoff này mô tả vẫn khớp code
thật — ĐỪNG giả định mô tả trên là chính xác mãi mãi, người dùng có thể đã tự sửa
thêm gì đó sau khi handoff này được viết.

============================================================
VIỆC CẦN LÀM TIẾP THEO — CẦN HỎI NGƯỜI DÙNG THỨ TỰ ƯU TIÊN
============================================================
1. 3 giới hạn của offline-queue (xem mục 3 ở "TRẠNG THÁI GIT" phía trên — publish
   không chờ ACK thật, MAX_FILES=20 chưa tính theo dung lượng flash, tên file dựa
   trên counter RAM có thể trùng sau reset). Hỏi người dùng có cần xử lý ngay
   không, hay chấp nhận tạm thời.
2. Bootloader/OTA — người dùng đang TỰ LÀM (SynaptiX_FDK/BOOTLOADER_WS/, xem quy
   tắc bắt buộc mục 8). KHÔNG chủ động động vào trừ khi người dùng yêu cầu review
   hoặc tích hợp với app chính.
3. USB MSC để sửa config file trực tiếp (thay thế nhập TLS cert dài qua CLI/RPC) —
   vẫn chỉ là comment "tương lai" trong network_config.h, chưa wiring gì. Chưa
   được yêu cầu bắt đầu.
4. Nếu người dùng muốn thêm field MỚI vào heartbeat/telemetry KHÔNG có trong v0
   (ví dụ theo nhu cầu thực tế mới phát sinh) — đây là việc khác với "đạt parity
   với v0" (đã xong ở mục "TRẠNG THÁI GIT" phía trên), cần hỏi rõ yêu cầu cụ thể.

============================================================
VIỆC ĐẦU TIÊN KHI TIẾP NHẬN — THEO ĐÚNG THỨ TỰ
============================================================
1. git fetch origin + git log --oneline -15 + git status/git diff — xác nhận trạng
   thái thật, so với mục "TRẠNG THÁI GIT" ở trên.
2. Nếu có commit mới hơn 5e3a034, đọc lại toàn bộ các file liên quan bằng `view`
   trước khi giả định phần nào của handoff này còn đúng.
3. Hỏi người dùng ưu tiên nào trong "VIỆC CẦN LÀM TIẾP THEO" trước khi code bất
   cứ gì mới.