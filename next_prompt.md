HANDOFF — HARDWARE BRING-UP TEST, WS_v1 (STM32H563RIV6) — PHIÊN 3

Viết khi sắp hết token. Đọc kỹ 2 handoff phiên trước (trong lịch sử chat)
trước khi làm gì — phiên này KẾ TIẾP trực tiếp từ phiên 2, đang dở 1 fix
CHƯA ĐƯỢC THỰC HIỆN (xem mục "VIỆC CẦN LÀM NGAY ĐẦU PHIÊN" bên dưới).

============================================================
QUY TẮC BẮT BUỘC (không đổi qua các phiên)
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
thông số. Log thật LUÔN thắng datasheet khi có xung đột (đã xảy ra
nhiều lần: PWRKEY polarity, CSQ/CMQTT response spacing — xem phiên 1-2).
============================================================
VIỆC CẦN LÀM NGAY ĐẦU PHIÊN — FIX CHƯA THỰC HIỆN, ĐÃ ĐƯỢC XÁC NHẬN

Người dùng đã xác nhận đồng ý (câu trả lời cuối cùng trước khi hết
token), nhưng CHƯA CÓ THỜI GIAN THỰC HIỆN. Đây là việc ĐẦU TIÊN cần làm:

Bug xác nhận: trong SynaptiX_FDK/app/user/test/test_sleep.c, hàm
test_sleep_poll() gọi VÔ ĐIỀU KIỆN mỗi tick:

c
void test_sleep_poll(uint32_t delta_ms)
{
    sx_user_mqtt_poll(delta_ms);
    sx_temp_humi_poll(&s_th, delta_ms);      // <-- BUG: gọi cả lúc đang WAKING
    accel_app_poll(&s_accel, delta_ms);      // <-- BUG: gọi cả lúc đang WAKING
    gps_process(&board.gps, delta_ms);
    ...
}

Bằng chứng từ log thật (đã xác nhận, xem log đầy đủ trong lịch sử
chat): sau khi <<< Woke from STOP mode, trong lúc
s_state == TEST_SLEEP_STATE_WAKING (đang chạy 6 wake_steps của
sx_sleep_manager.c, CHƯA XONG), log cho thấy:

[INFO]SX_SLEEP_SVC : wake step 'gps_wait_fix' starting   <- mới ở bước 2/6
[WARNING]TEMP_HUMI : read failed (2)
[WARNING]ACCEL_APP : linear accel read failed

accel_resume là wake_steps[5] (bước CUỐI CÙNG trong 6 bước) —
nghĩa là khi TEMP_HUMI/ACCEL_APP cố đọc I2C1 ở early wake (bước 2/6),
BNO055 vẫn còn ở SUSPEND mode (do sleep_step trước đó set), nên đọc
thất bại là ĐÚNG KỲ VỌNG — đây là race condition do timing/thứ tự gọi
hàm sai, KHÔNG PHẢI bug I2C bus vật lý thật (không cần bus-recovery gì
thêm — bus-recovery I2C đã được xử lý ở phiên 2, xem mục "Bug đã fix"
bên dưới, đó là fix khác, KHÔNG liên quan tới vấn đề mới này).

Fix cần làm (ĐÃ ĐƯỢC NGƯỜI DÙNG XÁC NHẬN "đúng, sửa theo hướng này"):
tạm dừng gọi sx_temp_humi_poll()/accel_app_poll() trong lúc
s_state == TEST_SLEEP_STATE_WAKING, chỉ gọi lại sau khi
sx_sleep_manager_is_wake_done(&s_sleep_mgr) trả về true (hoặc đơn giản
hơn: chỉ gọi 2 hàm này khi s_state == TEST_SLEEP_STATE_PUBLISH, vì đó
là state duy nhất mà cảm biến thực sự "sống" và cần đọc để build
payload).

Cách sửa cụ thể đề xuất (chưa làm, cần đọc lại code thật trước khi
áp dụng chính xác theo state hiện tại của file):

c
void test_sleep_poll(uint32_t delta_ms)
{
    sx_user_mqtt_poll(delta_ms);
    gps_process(&board.gps, delta_ms);   // GPS có power/state riêng, có
                                          // thể vẫn cần chạy mọi lúc — CẦN
                                          // KIỂM TRA LẠI, không giả định
    /* Only poll temp/humi + accel when NOT in the middle of a wake
     * sequence — accel_resume is wake_steps[5] (last step), so BNO055
     * is still in SUSPEND until wake is fully done; SHT3x is on the same
     * I2C1 bus. Reading either mid-wake races the wake_steps and produces
     * spurious "read failed" spam, not a real bus fault. */
    if (s_state != TEST_SLEEP_STATE_WAKING) {
        sx_temp_humi_poll(&s_th, delta_ms);
        accel_app_poll(&s_accel, delta_ms);
    }
    ...
}

LƯU Ý QUAN TRỌNG: PHẢI đọc lại toàn bộ test_sleep_poll() thật trong
repo trước khi áp dụng — có thể người dùng đã tự sửa gì đó giữa phiên,
hoặc thứ tự các dòng code đã khác so với đoạn trích trên (lấy từ log
cuối phiên 2, xem lại phiên 2 trong lịch sử để đối chiếu bản gốc). Đây
chỉ là ĐỀ XUẤT, không phải code đã xác nhận cuối cùng — logic tổng thể
(chỉ đọc cảm biến ngoài lúc WAKING) đã được người dùng đồng ý, nhưng
CÁCH VIẾT CHI TIẾT cần tự soi lại cho khớp code thật.

Sau khi sửa, cần hỏi lại người dùng: có cần áp dụng cùng logic guard này
cho gps_process() không (GPS có 2 wake_steps riêng — gps_on và
gps_wait_fix — có thể cũng bị đọc sớm tương tự, CHƯA ĐIỀU TRA, cần xem
kỹ code GPS driver có bug tương tự không trước khi kết luận).

============================================================
TIẾN TRÌNH TOÀN BỘ DỰ ÁN TÍNH ĐẾN HẾT PHIÊN NÀY
Đã xác nhận sống hoàn toàn (qua log thật, nhiều lần lặp lại ổn định)
UART1 + Modem A7677S: network attach đầy đủ (CGDCONT/CGACT/CREG/COPS/
IP/time sync), MQTT connect + publish qua broker.hivemq.com:1883.
UART2 + GPS (CASIC AT6558R): banner + NMEA sentences đều đặn (test HAL
thuần, phiên 1). Qua app thật (driver gps.c) — CHƯA re-confirm kỹ ở
phiên 3, chỉ thấy log "Power on GPS first" chạy được, GPS timeout do
không có GPS thật gắn vào bench (theo thiết kế, không phải bug).
I2C1 + BNO055 (IMU): CHIP_ID 0xA0 đúng, nhưng xem mục race condition
trên — code app thật (accel_app.c) có vấn đề timing khi wake.
SPI1 + W25Q128: JEDEC ID EF 40 18 đúng, driver thật hoạt động qua
sx_board_init().
LittleFS/sx_storage: mount OK.
I2C1 + SHT3x: init OK qua sx_board_init(), nhưng có vấn đề timing khi
wake (xem mục race condition).
Bug đã fix, có bằng chứng log xác nhận (phiên 1-2, KHÔNG cần làm lại)
Thứ tự at_term_init()/power_sim_on() (phiên test console cũ, có
thể không còn áp dụng nếu code đã chuyển hẳn app thật).
SPI1 JEDEC ID toàn 0x00 → thêm RELEASE_POWER_DOWN trước JEDEC ID.
Ăng-ten LTE lỏng/hỏng → người dùng tự thay ăng-ten mới, đã xác nhận.
BUG GỐC QUAN TRỌNG: modem_send_command() trong modem.c không
memset buffer cũ → response bị ghép lẫn giữa nhiều lệnh AT khác
nhau → CREG poll luôn timeout dù modem trả lời đúng. Fix:
memset(modem->buff, 0, MODEM_RX_BUFFER_SIZE) thêm vào
modem_send_command().
6 pattern res_success cho lệnh MQTT (CMQTTSTART/CONNECT/STOP/PUB/ SUB/DISC) thiếu dấu cách sau : so với response thực tế modem (có
dấu cách) → luôn timeout dù thành công. Fix: thêm dấu cách vào cả 6
pattern trong a7677s.c.
I2C bus lockup (1 slave kẹt giữa chừng khiến toàn bus timeout vĩnh
viễn) → người dùng TỰ VIẾT bus-recovery riêng trong sx_i2c.c (commit
9e85cc6 "fix crash"), ĐỘC LẬP với 1 bản mình từng viết thử trong
container (đã bị discard, không dùng, không commit). XÁC NHẬN LẠI
bằng đọc code thật đầu phiên sau, đừng lẫn 2 bản.
Modem start() bị gọi CHỒNG LỆNH giữa sx_sleep_manager.c's wake
sequence (_modem_wait_ready_is_done()) và sx_mqtt.c's reconnect/
recovery-ladder logic (do_error() + sx_mqtt_poll()'s power-cycle-
settle) — cả 2 độc lập gọi modem->ops->start() trong cùng 1 lần
wake, làm modem "bối rối" giữa lúc tự SIM-detect nội bộ, gây
+CME ERROR: SIM failure + phải retry/power-cycle mất nhiều phút.
ĐÃ FIX VÀ XÁC NHẬN THÀNH CÔNG bằng log thật (log phiên 3, đầu
phiên): thấy "Retrying modem start() skipped (modem owned elsewhere...)" xuất hiện đúng 3 lần thay vì "start(): modem busy".
Cách fix: thêm cơ chế callback modem_owned_elsewhere — xem chi tiết
dưới đây vì đây là fix phức tạp, cần hiểu rõ nếu phải sửa tiếp:
sx_sleep_manager.h/.c: thêm sx_sleep_manager_is_waking().
sx_mqtt.h: thêm typedef sx_mqtt_modem_owned_elsewhere_cb_t,
field modem_owned_elsewhere trong struct sx_mqtt_t, setter
sx_mqtt_set_modem_owned_elsewhere_check().
sx_mqtt.c: 2 vị trí gọi start() (trong do_error()'s recovery
ladder, và sx_mqtt_poll()'s power-cycle-settle) đều check callback
trước khi gọi start().
sx_user_mqtt.h/.c: thêm hàm forward
sx_user_mqtt_set_modem_owned_elsewhere_check() (dùng plain
uint8_t (*)(void) type, không include sx_mqtt.h trong header để
giữ tách biệt tầng, đúng style file cũ).
test_sleep.c: thêm wrapper tĩnh is_modem_owned_by_sleep_manager()
gọi sx_sleep_manager_is_waking(&s_sleep_mgr), đăng ký vào MQTT
layer trong test_sleep_init() (SAU cả sx_user_mqtt_nontls_init()
VÀ sx_sleep_manager_init(), vì cần cả s_mqtt và s_sleep_mgr đã
tồn tại).
ACCEL_APP_SAMPLE_PERIOD_MS đổi từ 100ms → 3000ms theo yêu cầu người
dùng (đọc IMU quá thường xuyên, không cần thiết cho test này). Fix ở
accel_app.h. CẢNH BÁO CHƯA GIẢI QUYẾT: ACCEL_APP_FILTER_ALPHA
(0.1f, low-pass filter constant) được tune dựa trên chu kỳ 100ms cũ —
đổi period lên 3000ms (chậm 30 lần) làm bộ lọc phản ứng khác đi đáng
kể so với thiết kế gốc. ĐÃ HỎI người dùng có muốn điều chỉnh
ACCEL_APP_FILTER_ALPHA theo cho phù hợp không — CÂU HỎI NÀY CHƯA
ĐƯỢC TRẢ LỜI (người dùng yêu cầu viết handoff thay vì trả lời). HỎI
LẠI CÂU NÀY nếu liên quan tới việc đang làm, nhưng đây là ưu tiên THẤP
hơn nhiều so với bug WAKING/race condition ở trên.
Vấn đề ĐÃ PHÁT HIỆN, CHƯA FIX (ngoài mục "việc cần làm ngay" ở trên)
External flash W25Q128 hoàn toàn KHÔNG có sleep_step nào trong
sx_sleep_manager.c (chỉ có 6 bước: GPS/modem/SPS30/pump/ZE12A/BNO055
— không có W25Q128). Người dùng đã xác nhận đây là vấn đề thật (mục
"3 vấn đề" ở đầu phiên 3, người dùng chọn xử lý "phần 1" — modem wake —
trước, NHƯNG 2 vấn đề còn lại (external flash chưa sleep, LTE 1.8V
không tắt hẳn) VẪN CHƯA XỬ LÝ, chỉ tạm hoãn, không phải đã giải quyết.
LTE không thực sự cắt nguồn 1.8V — log cho thấy modem_power_off
chỉ làm "PWRKEY pulse (no AT command involved)", KHÔNG dùng AT+CPOF.
Nghi vấn: PWRKEY pulse có thể không đủ để cắt hẳn rail 1.8V nếu board
không có GPIO cắt nguồn riêng (tương tự comment cũ trong
sx_W25Q128.c: "No power-cutoff GPIO on this board revision"). CHƯA
ĐỌC KỸ _modem_power_off_start()/a7677s_power_off_start() để xác
nhận cơ chế thật — cần đọc kỹ ở phiên sau nếu người dùng quay lại vấn
đề này.
============================================================
GHI CHÚ QUAN TRỌNG VỀ KIẾN TRÚC (để hiểu code nhanh hơn phiên sau)
main.c hiện tại chạy APP THẬT qua test_sleep.c (không phải code
test HAL thuần của phiên 1 nữa, cũng không phải test_lte_mqtt.c của
đầu phiên 2 — đã chuyển tiếp qua test_sleep.c từ giữa phiên 2). XÁC
NHẬN LẠI bằng đọc main.c/git log đầu phiên sau, đừng giả định.
Kiến trúc sleep 3 tầng: tier 1 (sx_sleep.c, generic STOP-mode +
RTC wakeup, không biết peripheral cụ thể nào) → tier 2
(sx_sleep_service.c, chạy mảng sx_sleep_step_t generic) → tier 3
(sx_sleep_manager.c, biết cụ thể GPS/modem/SPS30/pump/ZE12A/BNO055
là gì, đăng ký 6+6 step cụ thể).
board_sleep_pre_stop_hook()/board_sleep_post_wake_hook()
(sx_board.c) là plug-in riêng của board vào tier 1, xử lý UART LTE/
GPS/DUST/EXTEND (abort trước sleep). QUAN TRỌNG:
board_sleep_post_wake_hook() HIỆN TẠI HOÀN TOÀN RỖNG (không làm gì,
kể cả USB dù comment nói "chỉ USB cần khôi phục ở đây") — KHÔNG XỬ LÝ
I2C1 Ở BẤT KỲ ĐÂU trong toàn bộ chuỗi sleep/wake (không abort trước
sleep, không resume sau wake) — điều này ĐÃ ĐƯỢC KIỂM TRA và có vẻ
KHÔNG PHẢI vấn đề (I2C1 dùng PCLK1, có thể tự động sống lại đúng sau
SystemClock_Config() mà không cần code thêm — NHƯNG CHƯA CHỨNG MINH
ĐƯỢC 100%, chỉ là giả thuyết hợp lý hơn so với race-condition timing đã
tìm ra). Nếu sau khi fix race-condition ở "việc cần làm ngay" mà I2C
vẫn còn lỗi (không phải do race condition), QUAY LẠI điều tra hướng
I2C1 clock/GPIO AF có thực sự sống lại đúng sau STOP mode hay không.
modem_ops_t (modem_ops.h) là lớp trừu tượng driver-agnostic — mọi
gọi tới modem đều qua board.modem.ops->xxx(board.modem.ctx, ...),
không gọi thẳng a7677s_xxx(). Điều này giải thích vì sao bug "gọi
start() chồng lệnh" (mục 7 ở trên) xảy ra: có NHIỀU caller độc lập
(sx_sleep_manager.c, sx_mqtt.c) đều có quyền gọi qua modem_ops_t
mà không biết về nhau — bài học: MỌI lần thêm code mới gọi
board.modem.ops->start()/power_on_start()/power_off_start() ở
bất kỳ đâu, PHẢI kiểm tra xem có caller nào khác cũng đang gọi cùng
lúc không, tương tự cách đã tìm ra bug này (dùng grep -rn "ops->start("
toàn bộ SynaptiX_FDK/).
============================================================
GỢI Ý THỨ TỰ LÀM VIỆC PHIÊN SAU
Re-clone, đọc git log, xem người dùng đã tự sửa gì thêm chưa.
Đọc lại test_sleep.c thật, áp dụng fix "tạm dừng poll cảm biến lúc
WAKING" đã được xác nhận đồng ý (xem mục đầu tiên).
Hỏi lại về gps_process() có cần guard tương tự không (chưa điều
tra).
Build, test, lấy log mới — kỳ vọng không còn thấy
TEMP_HUMI read failed/ACCEL_APP linear accel read failed ngay
trong lúc đang WAKING (bước gps_wait_fix/modem_wait_ready), chỉ có
thể fail ở state PUBLISH nếu bus I2C thật sự có vấn đề khác.
Nếu I2C vẫn lỗi sau fix #2-4: điều tra sâu hơn về I2C1 clock/GPIO-AF
sau STOP mode (xem ghi chú kiến trúc ở trên).
Sau khi race-condition ổn: quay lại 2 vấn đề còn treo — external
flash chưa có sleep_step, và LTE không cắt nguồn 1.8V thật sự (đọc
kỹ _modem_power_off_start()/a7677s_power_off_start()).
Hỏi lại về ACCEL_APP_FILTER_ALPHA có cần điều chỉnh theo period mới
(3000ms) hay không — ưu tiên thấp.