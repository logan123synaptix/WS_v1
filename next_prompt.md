HANDOFF — HARDWARE BRING-UP TEST, WS_v1 (STM32H563RIV6) — PHIÊN 8

Viết khi sắp hết token. Đọc kỹ trước khi làm gì. Phiên này KẾ TIẾP trực
tiếp từ phiên 7 (xem handoff phiên 7 trong lịch sử chat nếu cần chi
tiết đầy đủ: fix debug ZE12A qua mux/UART5, xóa CO/H2S khỏi hệ thống).
Phiên 8 KHÔNG động gì tới phần cứng ZE12A — tập trung vào: (A) xác
nhận code xóa CO/H2S của phiên 7 đã push, (B) debug 1 lần "publish
sau wake" hoá ra do người dùng quên cắm anten (không phải bug), (C)
tìm hiểu cơ chế ZE12A sleep/Q&A mode theo yêu cầu người dùng (kết
luận: không sửa gì, chỉ giải thích), (D) THÊM MỚI: cơ chế lưu GPS fix
cuối cùng vào exflash + field fix_gps trong payload — ĐÃ CODE XONG,
CHƯA PUSH, (E) dọn dẹp macro partition rác trong app_config.h + đổi
tên EX_FLASH_OFFSET/EXFLASH_SIZE — ĐÃ CODE XONG, CHƯA PUSH, (F) xác
nhận cơ chế bật/tắt TIM1 theo bơm đã đúng sẵn, không cần sửa, (G) YêU
CẦU MỚI CHƯA LÀM: heartbeat interval runtime-config + rút payload
heartbeat chỉ còn signalStrength, sleep 20 phút.

============================================================
QUAN TRỌNG NHẤT — TRẠNG THÁI GIT ĐẦU PHIÊN 9
============================================================
CÓ 3 FILE ĐANG THAY ĐỔI TRONG CONTAINER, CHƯA COMMIT/PUSH LÊN GITHUB:
  M SynaptiX_FDK/app/app.c
  M SynaptiX_FDK/app/app_config.h
  M SynaptiX_FDK/services/filesystem/sx_fs.c

(git diff --stat: app.c +109/-8 dòng, app_config.h +44/-38 dòng dạng
thay thế toàn khối, sx_fs.c +13/-6 dòng)

Đây LÀ TOÀN BỘ thay đổi của mục D+E bên dưới (GPS log persistence +
dọn macro flash partition). Người dùng ĐÃ YÊU CẦU không dùng
present_files/patch file — trình chiếu code trực tiếp trong chat bằng
view/tool xem file, để người dùng tự copy sang máy build+push. CHƯA
XÁC NHẬN người dùng đã copy/build/push các đoạn code này hay chưa —
người dùng vừa yêu cầu viết handoff ngay sau khi hoàn thành mục E.

VIỆC ĐẦU TIÊN PHIÊN 9: git status/git diff để xem đúng 3 file trên,
HỎI người dùng đã copy/build/push chưa. Nếu chưa, trình chiếu lại
bằng `view` cho người dùng copy — đặc biệt chú ý sx_fs.c vì nếu
người dùng chỉ copy app.c/app_config.h mà quên sx_fs.c thì build sẽ
LỖI NGAY (macro EX_FLASH_OFFSET/EXFLASH_SIZE không được định nghĩa ở
đâu khác ngoài app_config.h, và sx_fs.c là nơi duy nhất dùng chúng —
3 file này PHẢI đi cùng nhau, không thể copy thiếu 1 trong 3).

============================================================
QUY TẮC BẮT BUỘC (không đổi qua các phiên)
============================================================
RE-CLONE đầu phiên: git clone https://github.com/logan123synaptix/WS_v1.git
  rồi kiểm tra lại xem có patch/thay đổi nào của phiên trước còn dang
  dở chưa được copy (xem mục trên).
KHÔNG tin log/mô tả cũ mà không tự đọc lại code thật. Container reset
  giữa phiên — MỌI THAY ĐỔI CHƯA COMMIT/PUSH ĐÃ MẤT nếu không kịp đưa
  cho người dùng trước khi hết token.
Không sửa code âm thầm — trình bày nghi vấn → hỏi → chỉ sửa sau khi
  có xác nhận rõ ràng. Phiên 8 người dùng xác nhận từng bước rất chi
  tiết (đặc biệt vụ GPS log: xác nhận delete-rồi-write, xác nhận thời
  điểm lưu là lúc edge 0->1 fix chứ không phải mỗi SENDING, xác nhận
  ghi lại mỗi lần re-fix dù chập chờn nhiều lần trong 1 cycle).
Comment code tiếng Anh, trao đổi tiếng Việt.
KHÔNG có compiler thật trong container — không build được. Người dùng
  tự build + flash + gửi log qua chat.
Board test vật lý duy nhất: STM32H563RIV6.
Datasheet đầy đủ trong Documents/ — LUÔN tra cứu trước khi đoán
  thông số. Log thật/phép đo tay LUÔN thắng datasheet khi có xung đột.
  Phiên 8 dùng datasheet ZE12A (Documents/ze12a-electrochemical-
  module-manual-v1_0.md) để trả lời câu hỏi sleep/tiết kiệm điện — xem
  mục C.
KHÔNG dùng present_files/xuất patch file — trình chiếu code trực tiếp
  trong chat bằng view/tool xem file, để người dùng tự copy. Container
  vẫn không có git credential để tự push — không cần cố push, chỉ cần
  đưa code đúng, đầy đủ, rõ ràng cho người dùng tự copy.
KHI DEBUG PHẦN CỨNG: đừng đoán mò nhiều bước liên tiếp không hỏi. Mỗi
  giả thuyết cần 1 phép đo/log cụ thể để xác nhận hoặc loại trừ TRƯỚC
  khi chuyển sang giả thuyết kế tiếp.
TRƯỚC KHI XOÁ/ĐỔI TÊN MACRO: luôn grep toàn bộ codebase (--include=
  "*.c" --include="*.h") để xác nhận macro có đang thực sự được dùng ở
  đâu không, trước khi kết luận "rác" — bài học rút ra ở mục E: tên
  gọi macro (PART_GPS_LOG_*) có thể gây hiểu lầm hoàn toàn về vai trò
  thật của nó trong code (thực chất là vùng mount LittleFS toàn hệ
  thống, không phải riêng cho GPS).

============================================================
MỤC A — TÌNH TRẠNG CHUNG HỆ THỐNG (ổn định từ phiên 6-7, KHÔNG động
vào ở phiên 8 trừ các thay đổi nêu ở mục D/E)
============================================================
Các bug đã fix ở phiên 6 (SPS30 flush, shell CLI sau wake, timestamp
UTC+7, MQTT publish treo sau wake qua nhiều lớp, RTC re-sync mỗi
wake) — ĐÃ PUSH, xem handoff phiên 6/7 nếu cần chi tiết đầy đủ.

Debug ZE12A phiên 7 (mux/baudrate/HAL_UART_ErrorCallback thiếu) và
xóa CO/H2S khỏi hệ thống (9 file: main.c, app.c, gas_sensor_app.*,
test_sleep.c, test_ze12a.c, sx_board.c, ze12a.*) — phiên 8 ĐÃ XÁC
NHẬN LẠI BẰNG GIT LOG/GREP: tất cả đã push đúng, commit gần nhất liên
quan là b23cb50 "read sensor ok" (2026-08-02). Grep xác nhận sạch
hoàn toàn CO/H2S trong .c/.h, HAL_UART_ErrorCallback() có mặt trong
sx_board.c, GAS_SENSOR_COUNT=3.

MỘT CHỖ SÓT nhỏ được phát hiện lúc đầu phiên 8 nhưng CHƯA ĐƯỢC NGƯỜI
DÙNG XÁC NHẬN SỬA: SynaptiX_FDK/app/user/test/test_sleep.c dòng 159
vẫn còn:
    static const char *gas_keys[] = { "co", "so2", "no2", "o3", "h2s" };
File này không dùng ze12a.h nên KHÔNG lỗi biên dịch nếu không sửa,
chỉ là không nhất quán với schema payload thật (đã sạch co/h2s ở
app.c). Người dùng CHƯA yêu cầu sửa cái này ở phiên 8 (bị chuyển
hướng sang chủ đề GPS/heartbeat/TIM1 luôn) — NẾU CÓ THỜI GIAN, hỏi
lại người dùng phiên 9 có muốn dọn nốt không.

============================================================
MỤC B — SỰ CỐ "PUBLISH SAU WAKE KHÔNG ĐƯỢC" — HOÁ RA LÀ QUÊN CẮM
ANTEN, KHÔNG PHẢI BUG (không cần làm gì thêm)
============================================================
Người dùng gửi log cho thấy:
  - "MQTT not connected, telemetry queued" ngay ở SENDING đầu tiên
    sau wake.
  - Timestamp payload sai nghiêm trọng: "2082-11-30T10:19:33+07:00".
Mình đã đọc code app.c's APP_CYCLE_SENDING/APP_CYCLE_WAIT_PUBLISH và
is_modem_owned_by_sleep_manager() (fix từ phiên trước, đã confirm
wire đúng ở app_init() dòng ~870, sx_user_mqtt_set_modem_owned_
elsewhere_check()) — code KHÔNG có gì sai, nghi vấn đang đi đúng
hướng "kiểm tra modem/network" thì người dùng tự phát hiện: QUÊN CẮM
ANTEN GSM. Không có tín hiệu → modem không attach mạng → không NITZ
sync (giải thích timestamp 2082 rác) → không MQTT connect được →
telemetry queued offline, đúng behavior kỳ vọng, không phải bug.

BÀI HỌC: khi thấy timestamp RTC bất thường + MQTT not connected cùng
lúc ngay sau wake, ưu tiên hỏi người dùng kiểm tra anten GSM TRƯỚC
khi đào sâu code — 2 triệu chứng này cùng lúc là dấu hiệu mất tín
hiệu mạng, không phải lỗi phần mềm.

Người dùng cũng có gửi 1 đoạn log khác (từ test_ze12a.c, không phải
app.c thật) cho thấy SO2/NO2 "disconnected" xuyên suốt trong khi O3
đọc được liên tục — ĐÂY LÀ CÂU HỎI CHƯA GIẢI QUYẾT, người dùng chưa
trả lời các câu hỏi làm rõ mình đặt ra (board có thay đổi vật lý gì
giữa 2 lần test không, log có bị cắt đầu không) rồi chuyển sang hỏi
chủ đề khác. XEM MỤC "VIỆC TỒN ĐỌNG" bên dưới — cần theo dõi tiếp nếu
người dùng quay lại.

============================================================
MỤC C — CÂU HỎI VỀ SLEEP/TIẾT KIỆM ĐIỆN ZE12A — CHỈ TRẢ LỜI, KHÔNG
SỬA CODE
============================================================
Người dùng hỏi có cách nào sleep/tiết kiệm điện ZE12A không. Tra
datasheet (Documents/ze12a-electrochemical-module-manual-v1_0.md):
datasheet CẢNH BÁO RÕ không nên cắt nguồn ngắt quãng module này
("frequent power-off will cause serious deviations in displayed
values", khuyến nghị pin dự phòng nếu bắt buộc ngắt nguồn) và ghi rõ
bản chất cảm biến điện hóa lão hóa theo thời gian bất kể có cấp điện
hay không ("no relationship whether it is powered on").

Người dùng hỏi tiếp "code hiện tại có dùng QA mode không" — grep xác
nhận: KHÔNG dùng Q&A mode lúc hoạt động bình thường. Luồng thật:
  - Lúc thức (ON_PUMP->SENSING->SENDING): Active Upload mode xuyên
    suốt.
  - Lúc chuẩn bị sleep (sx_sleep_manager.c's _gas_sensor_qa_mode_
    start(), step 5 trong sleep sequence): chuyển sang Q&A mode —
    NHƯNG chỉ để tránh UART5 nhận rác lúc MCU ngủ, KHÔNG cắt nguồn
    module, KHÔNG phải low-power state thực sự.
  - Lúc wake (sx_sleep_manager.c's _gas_sensor_active_mode_start(),
    step 5 wake sequence): chuyển ngay lại Active Upload mode.
  => Module ZE12A được cấp nguồn LIÊN TỤC xuyên suốt cả sleep lẫn
     wake. Q&A mode chỉ đổi giao thức UART, không tiết kiệm điện đáng
     kể theo cách hệ thống đang dùng nó.

KẾT LUẬN: KHÔNG SỬA GÌ — người dùng chưa yêu cầu cắt nguồn ZE12A sau
khi nghe giải thích trade-off này, có thể sẽ hỏi lại ở phiên sau nếu
muốn triển khai (sẽ cần thêm GPIO cắt nguồn VDD giống cách SPS30 dùng
EN_PW_DUST — CHƯA CÓ SẴN cho ZE12A).

============================================================
MỤC D — GPS LOG PERSISTENCE + fix_gps FIELD — ĐÃ CODE XONG, CHƯA PUSH
(xem MỤC QUAN TRỌNG NHẤT ở đầu file)
============================================================
Yêu cầu người dùng (xác nhận từng điểm rõ ràng qua nhiều lượt hỏi-
đáp): khi GPS bắt được fix, lưu vào file trên exflash; khi có fix
MỚI (transition 0->1, kể cả chập chờn nhiều lần trong 1 cycle) thì
erase (delete) file cũ rồi ghi lại (KHÔNG dùng overwrite ngầm của
sx_storage_write(), người dùng muốn 2 bước delete-rồi-write tường
minh); payload thêm field "fix_gps": 1 (đang fix) / 0 (không fix);
khi fix_gps=0 thì lấy tọa độ từ file đã lưu thay vì null.

CHỈ SỬA 1 FILE: SynaptiX_FDK/app/app.c. Tóm tắt:
1. Macro GPS_LOG_PATH "/log_gps" (LƯU Ý: sau đó ở mục E phát hiện
   app_config.h vốn có sẵn 1 macro TÊN GIỐNG "GPS_LOG_FILE_PATH"
   nhưng KHÔNG được dùng ở đâu — đã xóa macro rác đó ở mục E, macro
   GPS_LOG_PATH trong app.c là macro THẬT đang dùng, không đổi tên
   theo GPS_LOG_FILE_PATH vì macro kia đã bị xóa hoàn toàn).
2. struct gps_log_record_t { float latitude; float longtitude; } —
   record duy nhất, không phải lịch sử nhiều điểm.
3. static bool s_gps_was_fixed — nhớ trạng thái fix tick trước, dùng
   để edge-detect 0->1 (không ghi flash mỗi tick khi fix ổn định
   nhiều tick liên tục).
4. gps_log_save_fix(lat, lon) — gọi sx_storage_delete(GPS_LOG_PATH)
   RỒI sx_storage_write(GPS_LOG_PATH, &rec, sizeof(rec)) — 2 bước
   tường minh theo đúng yêu cầu người dùng. sx_storage_delete() trả
   SX_STORAGE_ERR_NOT_FOUND nếu file chưa tồn tại (lần lưu đầu tiên)
   NHƯNG không log lỗi ồn ào, an toàn gọi vô điều kiện — đã xác nhận
   bằng cách đọc sx_ex_storage.c's sx_storage_delete() implementation
   (dùng remove(), trả NOT_FOUND nếu remove() < 0, không crash).
5. gps_log_read_last(*out_lat, *out_lon) — trả false nếu file chưa
   từng tồn tại (board mới/chưa từng fix từ lúc sống), dùng
   sx_storage_exists() trước rồi mới sx_storage_read().
6. Edge-detect đặt NGAY SAU gps_process(&board.gps, delta_ms) trong
   app_process() (chạy mỗi tick, không phải chỉ lúc SENDING) — per
   yêu cầu người dùng: bắt fix ngay khi nó xảy ra giữa cycle, không
   đợi tới lúc build payload.
7. Cả build_telemetry_payload() VÀ build_heartbeat_payload() đều sửa
   giống nhau (2 chỗ, ~dòng 541 và ~641 sau khi mục E chèn thêm dòng
   phía trên làm lệch số dòng — XEM LẠI SỐ DÒNG THẬT bằng grep
   "fix_gps" trước khi sửa tiếp, đừng tin số dòng ghi ở đây):
   - Nếu board.gps.latitude/longtitude != 0 (đang có fix): publish
     tọa độ RAM hiện tại + fix_gps:1.
   - Nếu không: gọi gps_log_read_last(), nếu có dữ liệu thì publish
     tọa độ đó (STALE, không phải live) + fix_gps:0; nếu chưa từng
     lưu (board mới) thì null/null + fix_gps:0.

CHƯA BUILD/TEST được (không có compiler) — VIỆC ĐẦU PHIÊN 9: sau khi
xác nhận người dùng đã copy/build, kiểm tra qua log thật:
  - Lúc GPS mới fix lần đầu sau boot: có log "GPS fix acquired, saved
    to /log_gps: lat=... lon=..." không.
  - Payload telemetry/heartbeat có field "fix_gps" đúng 1/0 theo tình
    trạng fix hiện tại không.
  - Lúc GPS mất fix giữa chừng (ví dụ che ăng-ten), payload có
    fallback đúng về tọa độ đã lưu (không phải null) không, và
    fix_gps chuyển về 0 đúng không.
  - Test lại tình huống chập chờn (fix rồi mất rồi fix lại nhiều lần
    trong ngắn hạn) xem có ghi flash lặp lại nhiều lần như kỳ vọng
    không (log "GPS fix acquired..." xuất hiện mỗi lần re-fix).

============================================================
MỤC E — DỌN MACRO PARTITION RÁC + ĐỔI TÊN EX_FLASH_OFFSET/EXFLASH_
SIZE — ĐÃ CODE XONG, CHƯA PUSH
============================================================
Người dùng nghi ngờ 1 khối macro trong app_config.h là "code rác từ
project cũ":
    PART_BOOTLOADER_OFFSET/SIZE, PART_MQTT_CONFIG_OFFSET/SIZE,
    PART_MISC_OFFSET/SIZE, PART_MSC_DISK_WIN/SIZE, PART_GPS_LOG_
    OFFSET/SIZE
Đã grep toàn bộ codebase (--include="*.c" --include="*.h") để kiểm
tra TRƯỚC KHI xóa gì (đúng quy tắc bắt buộc) — kết quả:
  - 8 macro: PART_BOOTLOADER_*, PART_MQTT_CONFIG_*, PART_MISC_*,
    PART_MSC_DISK_* — XÁC NHẬN THẬT SỰ LÀ RÁC, không dùng ở bất kỳ
    đâu ngoài định nghĩa. ĐÃ XÓA.
  - PART_GPS_LOG_OFFSET và PART_GPS_LOG_SIZE — PHÁT HIỆN QUAN TRỌNG:
    KHÔNG PHẢI RÁC, đang được dùng thật trong SynaptiX_FDK/services/
    filesystem/sx_fs.c (lfs_flash_read/write/erase, sx_fs_init) —
    đây chính là offset+size của TOÀN BỘ vùng flash mà LittleFS mount
    vào (tức toàn bộ hệ thống file /queue, /log_gps, mọi thứ
    sx_storage_*() dùng). Tên gọi "PART_GPS_LOG_*" là tàn dư gây hiểu
    lầm từ thiết kế cũ (có thể từng định dành 1 vùng riêng chỉ cho
    GPS log), thực tế đang phục vụ TOÀN BỘ filesystem, không riêng
    GPS.
  - Các macro path liên quan (GPS_LOG_FILE_PATH, IMU_CALIB_FILE_PATH,
    GPS_CSV_FILE_PATH, GPS_CSV_HEADER, GPS_LOG_READ_CHUNK) — cũng
    grep xác nhận KHÔNG dùng ở đâu — ĐÃ XÓA (rác thật).

Theo yêu cầu người dùng (dùng toàn bộ 16MB chip cho LittleFS thay vì
chỉ ~9MB như PART_GPS_LOG_SIZE cũ tính toán, và đổi tên cho đúng ý
nghĩa thật):
  #define EX_FLASH_OFFSET   (0x000000U)        // = 0, toàn bộ chip
  #define EXFLASH_SIZE      FLASH_TOTAL_SIZE    // = 16MB, không phải ~9MB

sx_fs.c: đổi mọi PART_GPS_LOG_OFFSET -> EX_FLASH_OFFSET,
PART_GPS_LOG_SIZE -> EXFLASH_SIZE (3+1 chỗ dùng: lfs_flash_read,
lfs_flash_write, lfs_flash_erase dùng OFFSET; sx_fs_init dùng SIZE).

PHÁT HIỆN PHỤ QUAN TRỌNG: sx_fs.c TRƯỚC ĐÓ KHÔNG include app_config.h
ở đâu cả (chỉ include sx_fs.h + logger.h) — macro PART_GPS_LOG_*
trước đây chỉ compile được nhờ 1 chain include gián tiếp không rõ
ràng nào đó trong toàn project (rất có thể qua global/precompiled
header của IDE, không tường minh trong code). ĐÃ THÊM
#include "app_config.h" TƯỜNG MINH vào đầu sx_fs.c để không phụ
thuộc vào may rủi build-order nữa — ĐÂY LÀ THAY ĐỔI CẦN THIẾT, không
phải optional, nếu thiếu dòng include này rất có thể build sẽ lỗi
"undefined identifier EX_FLASH_OFFSET/EXFLASH_SIZE" tùy vào cách IDE
generate Makefile.

AN TOÀN DỮ LIỆU: vì offset không đổi (vẫn 0, chỉ là trước đây macro
tính offset lệch dựa trên tổng nhiều PART_* cộng dồn, giờ = 0 thẳng)
— CẦN LƯU Ý: offset thật sự CÓ THỂ ĐÃ THAY ĐỔI nếu board hiện tại
đang chạy firmware CŨ (trước phiên 8) vì PART_GPS_LOG_OFFSET cũ =
PART_MSC_DISK_WIN + PART_MSC_DISK_SIZE = 1MB+2MB+1MB+3MB = 7MB, KHÔNG
PHẢI 0! Tức là: NẾU BOARD ĐANG CÓ DỮ LIỆU THẬT TRONG /queue hoặc
/log_gps TỪ FIRMWARE CŨ (trước phiên 8), sau khi flash firmware mới
với EX_FLASH_OFFSET=0, LittleFS SẼ MOUNT VÀO VÙNG FLASH KHÁC (offset
0 thay vì offset 7MB) — DỮ LIỆU CŨ SẼ KHÔNG ĐỌC ĐƯỢC NỮA (không mất
vật lý, vẫn nằm ở offset 7MB cũ, nhưng LittleFS mount ở offset 0 sẽ
thấy vùng đó là chưa format/rác). Đây LÀ ĐIỀU CẦN CẢNH BÁO NGƯỜI DÙNG
Ở PHIÊN 9 nếu chưa kịp nói: nếu board có dữ liệu quan trọng trong
queue/log_gps từ trước, việc đổi offset này sẽ khiến LittleFS coi
như đang mount vào 1 chip trống rỗng lúc đầu (không phải lỗi, nhưng
mất khả năng đọc lại dữ liệu cũ) — sx_fs_init()/file_system_init() sẽ
tự format lại nếu chưa nhận diện được filesystem hợp lệ ở vùng mới
(giống board hoàn toàn mới). Nếu người dùng cần giữ dữ liệu cũ, PHẢI
đọc ra trước khi flash firmware mới, hoặc chấp nhận mất/ tự cân nhắc.
MÌNH CHƯA KỊP CẢNH BÁO ĐIỀU NÀY CHO NGƯỜI DÙNG TRƯỚC KHI HẾT TOKEN —
VIỆC ĐẦU PHIÊN 9 PHẢI NÓI RÕ ĐIỀU NÀY.

============================================================
MỤC F — CÂU HỎI VỀ TIM1/PWM BƠM — CHỈ TRẢ LỜI, KHÔNG SỬA (code đã
đúng sẵn)
============================================================
Người dùng muốn: chỉ chạy TIM1 (PWM) khi cần bật bơm, không cần thì
thôi. Đã đọc sx_pwm_sw.h, sx_pump.c, sx_timer.c — XÁC NHẬN CODE HIỆN
TẠI ĐÃ ĐÚNG YÊU CẦU NÀY TỪ TRƯỚC, KHÔNG CẦN SỬA GÌ:
  - Đây là software (bit-banged) PWM qua GPIO, TIM1 chỉ dùng làm
    nguồn ngắt định kỳ (ISR tick generator), KHÔNG phải hardware PWM
    output channel thật.
  - pump_off() -> sx_pwm_software_stop() -> sx_timer_stop() ->
    HAL_TIM_Base_Stop_IT() — DỪNG HẲN counter + interrupt của TIM1.
  - pump_on()/pump_set_power() -> sx_pwm_software_start() ->
    sx_timer_start_hw() (nếu !pwm->running) -> HAL_TIM_Base_Start_IT()
    — CHỈ KHỞI ĐỘNG LẠI counter khi thật sự cần bật bơm.
  - MX_TIM1_Init() (CubeMX-generated, gọi 1 lần lúc boot trong
    main.c) chỉ cấu hình thanh ghi (Prescaler/ARR/ClockSource), KHÔNG
    tự start counter (HAL_TIM_Base_Init() không start) — clock TIM1
    được RCC-enable qua MspInit nhưng timer không đếm/không tốn điện
    đáng kể cho tới khi Start_IT được gọi thật sự lúc bơm bật.
  => KẾT LUẬN: hệ thống đã tự động chỉ chạy TIM1 khi bơm đang hoạt
     động. Không có việc gì cần làm thêm ở mục này trừ khi người dùng
     có bằng chứng đo đạc thực tế (dòng tiêu thụ, hiện tượng gì đó)
     mâu thuẫn với những gì code cho thấy — NẾU CÓ, cần đo/log cụ thể
     trước khi nghi ngờ tiếp (theo đúng quy tắc debug phần cứng).

============================================================
MỤC G — YÊU CẦU MỚI CHƯA LÀM: HEARTBEAT INTERVAL RUNTIME-CONFIG +
RÚT GỌN PAYLOAD + SLEEP 20 PHÚT
============================================================
Người dùng yêu cầu (CHƯA CODE GÌ, mới dừng ở bước mình đọc code nền
tảng liên quan rồi hỏi làm rõ — NGƯỜI DÙNG CHƯA TRẢ LỜI CÁC CÂU HỎI
LÀM RÕ, chuyển sang hỏi mục F rồi giờ hỏi viết handoff):

1. Topic heartbeat sẽ bắn theo khoảng thời gian TIME_PUB_HEARTBEAT
   trong app_config.h (tên biến người dùng tự đề xuất, hiện app_
   config.h CHƯA CÓ macro này, cần tạo mới) — hiện tại heartbeat đang
   bắn theo HEARTBEAT_CYCLE_INTERVAL=4U (đếm SỐ CHU KỲ SENDING, không
   phải mili giây trực tiếp, xem app.c's s_sending_cycle_count) mỗi 4
   cycle. Người dùng nói "hiện tại heartbeat bắn 5 phút 1 lần" — đây
   là kết quả GIÁN TIẾP của 4 cycles x period hiện tại (SLEEP_TIME_MS
   mặc định 5 phút x ~1 cycle time), CẦN XÁC NHẬN LẠI công thức thật
   ở phiên 9 vì SLEEP_TIME_MS sắp đổi thành 20 phút (xem điểm 3) —
   nếu vẫn giữ HEARTBEAT_CYCLE_INTERVAL=4 dạng đếm cycle, heartbeat
   interval thực tế sẽ tự động giãn ra thành ~80 phút một khi sleep
   đổi thành 20 phút, KHÔNG PHẢI 5 phút như người dùng muốn giữ — đây
   CHÍNH LÀ LÝ DO người dùng muốn tách heartbeat interval ra thành
   1 giá trị thời gian ĐỘC LẬP với sleep cycle, không đếm theo số
   cycle nữa.

2. Cần thêm: shell CLI command MỚI + MQTT RPC command MỚI để
   config runtime giá trị TIME_PUB_HEARTBEAT này — ĐÃ XEM CONVENTION
   CÓ SẴN (network_config_t's pump_on_ms/sensing_ms/sleep_ms +
   network_config_set_pump_on_ms() etc. + shell_commands.c's
   "settings -c -pump/-sensing/-sleep ..." + mqtt_rpc.c's "setParams"
   method) — RẤT CÓ KHẢ NĂNG heartbeat_ms nên là 1 field MỚI trong
   network_config_t (SynaptiX_FDK/app/user/network_config/
   network_config.h/.c) theo đúng pattern đã có, KHÔNG PHẢI define
   cứng trong app_config.h như người dùng gợi ý ban đầu — CẦN HỎI LẠI
   người dùng ở phiên 9 xác nhận hướng này (network_config runtime-
   editable, giống pump/sensing/sleep) thay vì app_config.h compile-
   time #define, vì người dùng đã nói rõ muốn "thêm lệnh shell, rpc
   để config nó" — tức chắc chắn cần runtime-editable, app_config.h
   #define đơn thuần KHÔNG đủ (giống cách 3 field pump_on_ms/
   sensing_ms/sleep_ms cũ đã chuyển từ #define sang network_config_t
   từ phiên trước, xem app.c dòng ~73-85 comment giải thích lý do).

3. Payload heartbeat: người dùng muốn "chỉ cần bắn signal strength" —
   CHƯA XÁC NHẬN RÕ có giữ deviceID/timestamp không (để biết bản tin
   của thiết bị nào lúc nào) hay bỏ hết chỉ còn đúng 1 field
   signalStrength — ĐÃ HỎI NGƯỜI DÙNG NHƯNG CHƯA CÓ CÂU TRẢ LỜI. Hiện
   trạng build_heartbeat_payload() (app.c dòng ~505 trở đi, SỐ DÒNG
   CÓ THỂ ĐÃ XÊ DỊCH do mục D/E chèn thêm code phía trên — grep lại
   trước khi sửa) đang có: deviceID, timestamp, uptimeMs,
   firmwareVersion, signalStrength, operator, railVoltage,
   railCurrent, motionState, latitude/longitude/fix_gps (vừa thêm ở
   mục D), object "sensors" (per-sensor OK/FAIL: tempHumi, sps30,
   so2, no2, o3, accel — cần grep lại thứ tự đầy đủ, đoạn cuối hàm
   chưa xem hết ở phiên 8). CẦN NGƯỜI DÙNG XÁC NHẬN RÕ payload cuối
   cùng trước khi sửa — đừng tự đoán field nào giữ/bỏ.

4. Sleep 20 phút: người dùng muốn "cho device ngủ 20 phút mới dậy đo
   cảm biến + bật bơm bắn topic data" — ĐÃ XÁC NHẬN ĐÂY LÀ ĐÚNG CHU KỲ
   sleep_ms HIỆN CÓ SẴN trong network_config_t (không phải state mới)
   — chỉ cần đổi giá trị thành 1200000U (20 phút = 20*60*1000). CẦN
   HỎI: đổi default trong app_config.h's SLEEP_TIME_MS (hiện
   5*60*1000) hay chỉ set runtime qua CLI "settings -c -sleep 1200"
   trên board thật (không đổi compile-time default)? CHƯA CÓ CÂU TRẢ
   LỜI của người dùng.

VIỆC ĐẦU PHIÊN 9 CHO MỤC G (sau khi xử lý xong mục Quan Trọng Nhất ở
đầu file): hỏi lại người dùng rõ ràng 3 điểm chưa xác nhận ở trên
(network_config_t field mới hay app_config.h #define; payload
heartbeat rút gọn còn field gì chính xác; đổi default SLEEP_TIME_MS
hay chỉ set runtime) rồi mới bắt tay sửa. Việc sửa dự kiến động vào
CẢ 4 FILE: network_config.h/.c (field mới + getter/setter),
shell_commands.c (command mới, theo mẫu "-pump"/"-sensing"/"-sleep"
đã có ở dòng ~205-222 và ví dụ dùng ở dòng ~58), mqtt_rpc.c ("setParams"
method mở rộng), app.c (build_heartbeat_payload() rút gọn + logic
tính heartbeat theo thời gian thay vì đếm cycle — CẦN THIẾT KẾ LẠI
s_sending_cycle_count/HEARTBEAT_CYCLE_INTERVAL, có thể chuyển sang
1 biến tick_ms riêng cộng dồn qua app_process() giống cách sleep/
pump/sensing đang cộng dồn s_cycle_tick_ms, KHÔNG PHẢI đếm số lần
SENDING nữa vì SENDING giờ cách nhau 20 phút thay vì vài phút).

============================================================
VIỆC TỒN ĐỌNG TỪ CÁC PHIÊN TRƯỚC — CHƯA ĐỘNG VÀO Ở PHIÊN 8
============================================================
  - test_sleep.c dòng 159 còn "co"/"h2s" trong gas_keys[] (xem mục A)
    — ưu tiên thấp, không lỗi build, chỉ không nhất quán schema.
  - Log test_ze12a.c cho thấy SO2/NO2 "disconnected" xuyên suốt trong
    khi O3 đọc được — CHƯA GIẢI QUYẾT, người dùng chưa trả lời câu
    hỏi làm rõ (board có đổi gì vật lý không, log có bị cắt đầu
    không) — nếu người dùng quay lại chủ đề này, hỏi lại 2 câu đó
    trước khi đoán tiếp, và xin log ĐẦY ĐỦ tối thiểu 15-20s liên tục
    không cắt.
  - ACCEL_APP_FILTER_ALPHA chưa điều chỉnh theo period mới (từ phiên
    3-4) — càng cũ càng ít liên quan, có thể bỏ qua trừ khi được nhắc
    lại.
  - log_debug -> log_info cho temp/humi — ưu tiên thấp, chưa ai yêu
    cầu.
  - err=-4 SPS30 SHDLC CRC mismatch — thấy 1 lần lẻ tẻ ở STOP_
    MEASUREMENT trong phiên 6/7, chưa xác định pattern hay nhiễu 1
    lần — theo dõi tiếp nếu người dùng báo lại.

============================================================
VIỆC CẦN LÀM ĐẦU PHIÊN 9 (thứ tự ưu tiên)
============================================================
1. Re-clone, git status/diff — xác nhận 3 file mục D+E đã được người
   dùng copy/build/push chưa. Nếu chưa, trình chiếu lại đủ CẢ 3 FILE
   (app.c, app_config.h, sx_fs.c — thiếu 1 file là build lỗi ngay do
   macro EX_FLASH_OFFSET/EXFLASH_SIZE).
2. CẢNH BÁO NGƯỜI DÙNG về rủi ro mất khả năng đọc dữ liệu cũ trên
   exflash nếu board đang có dữ liệu quan trọng từ firmware trước khi
   đổi EX_FLASH_OFFSET (xem chi tiết mục E) — CHƯA KỊP NÓI Ở PHIÊN 8.
3. Xác nhận qua log thật: GPS fix log lưu đúng, fix_gps field đúng,
   fallback đọc flash đúng khi mất fix (xem checklist đầy đủ ở cuối
   mục D).
4. Hỏi lại 3 điểm chưa xác nhận của mục G (network_config field mới
   hay define cứng; payload heartbeat rút gọn còn field gì; đổi
   default sleep hay chỉ set runtime) rồi mới code phần heartbeat +
   sleep 20 phút.
5. Nếu có thời gian dư: hỏi người dùng có muốn dọn nốt test_sleep.c's
   gas_keys[] (mục A) và theo dõi lại vụ SO2/NO2 disconnected trong
   test_ze12a.c log (mục B/tồn đọng) không.