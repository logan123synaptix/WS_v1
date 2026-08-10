HANDOFF — WS_v1 (nhánh main) — TIẾN ĐỘ PHIÊN NÀY
============================================================
Ngày: 2026-08-10
Nhánh làm việc: main (KHÔNG phải ft/fota_ws — nhánh đó có FOTA,
main thì không, xem mục "FOTA" bên dưới)

============================================================
TRẠNG THÁI GIT — QUAN TRỌNG, ĐỌC TRƯỚC
============================================================
5 file đang có thay đổi CHƯA COMMIT trên máy làm việc (chưa push):

    Core/Src/main.c
    SynaptiX_FDK/app/app.c
    SynaptiX_FDK/app/user/sx_sleep_manager/sx_sleep_manager.c
    SynaptiX_FDK/services/sleep_service/sx_sleep_service.c
    SynaptiX_FDK/services/sleep_service/sx_sleep_service.h

Đây là code IWDG watchdog (xem mục 2 bên dưới) — ĐÃ VIẾT XONG,
ĐÃ KIỂM TRA CÚ PHÁP (ngoặc cân bằng, include, không sót tham
chiếu), NHƯNG CHƯA BUILD THẬT (không có toolchain ARM trong môi
trường làm việc) VÀ CHƯA TỪNG CHẠY TRÊN BOARD THẬT. Người dùng cần
tự build + flash + test trước khi commit/push.

============================================================
ĐÃ HOÀN THÀNH VÀ XÁC NHẬN QUA HARDWARE THẬT (ổn định)
============================================================

1. ZE12A — mux ghép cặp kênh (0,1)/(2,3) — ĐÃ TÌM RA NGUYÊN NHÂN
--------------------------------------------------------------
Đã xác nhận bằng đo trực tiếp: chân MCU UART5_S0_Pin (GPIOA Pin 7,
điều khiển bit A0 của mux TMUX4052) kẹt cứng ở 0V, không lên HIGH
được dù channel lẻ (1,3) yêu cầu HIGH. Đây là lỗi PHẦN CỨNG (chân
MCU hỏng hoặc dây/mối hàn), KHÔNG PHẢI lỗi logic firmware — công
thức chọn kênh trong ze12a_select_mux_channel() (channel & 0x01 ->
S0, channel & 0x02 -> S1) đã verify đúng qua code.

Việc còn lại thuộc về người dùng, không phải code: xác định S0
kẹt LOW là do chân MCU hỏng hay do dây/mối hàn — cần đo thêm tại
chân IC trực tiếp (không phải test point) để phân biệt.

Công cụ đã cung cấp, đã dùng để chẩn đoán, có thể giữ lại hoặc gỡ
tùy người dùng:
  - SynaptiX_FDK/app/user/test/test_mux_select.c/.h — hàm
    select_mux_test(channel) độc lập, tự ghi GPIO S0/S1 để đo bằng
    đồng hồ vạn năng, không phụ thuộc gas_sensor_init().
  - ze12a.c có thêm log chẩn đoán DIAG (s_last_write_channel[]) ghi
    lại mux channel nào ghi đè vào slot nào — hữu ích nếu lỗi mux
    khác tái xuất hiện sau này, có thể gỡ nếu người dùng thấy log
    quá ồn.

2. Heartbeat không bao giờ publish — ĐÃ FIX, ĐÃ QUA REVIEW LOGIC
--------------------------------------------------------------
Nguyên nhân gốc: send_heartbeat_if_due() (app.c) dùng HAL_GetTick()
để đo thời gian trôi qua, nhưng HAL_GetTick() (SysTick) bị
HAL_SuspendTick() đóng băng suốt STOP mode. Với sleep_ms=1800s và
phần thức mỗi lap chỉ vài chục giây, hiệu số tick giữa 2 lần
SENDING gần như luôn nhỏ hơn heartbeat_ms, khiến điều kiện "đã đủ
giờ chưa" gần như vĩnh viễn sai — heartbeat không bao giờ publish.

Đã sửa: chuyển sang dùng epoch giây đọc từ RTC ngoài (rx8130ce,
không bị SysTick ảnh hưởng) — hàm mới get_rtc_epoch_utc() +
sửa send_heartbeat_if_due() trong app.c. Đã present file, đã giải
thích cho người dùng. TRẠNG THÁI: đã nằm trong code hiện tại trên
main (không phải 1 trong 5 file "chưa commit" ở trên — đây đã
được người dùng tự commit/push từ trước, đã pull về xác nhận).

============================================================
★★★ VIỆC QUAN TRỌNG NHẤT CHƯA LÀM — TRỌNG TÂM CẦN LÀM TIẾP ★★★
============================================================

YÊU CẦU CỦA NGƯỜI DÙNG (đã xác nhận rõ ràng, nguyên văn ý):
  - Cứ 15 PHÚT thì publish 1 lần HEARTBEAT (topic heartbeat)
  - Cứ 30 PHÚT thì publish 1 lần DATA/TELEMETRY (topic chính, đầy
    đủ cảm biến — pump/GPS/SPS30/ZE12A)
  - heartbeat_ms và sleep_ms đều đã runtime-configurable qua
    shell/RPC từ trước (network_config_set_heartbeat_ms(), đã có
    sẵn, không cần làm gì thêm ở tầng đó)

TRẠNG THÁI THỰC TẾ HIỆN TẠI TRÊN MAIN: CHƯA LÀM ĐƯỢC ĐIỀU NÀY.

Bản fix ở mục 2 (RTC epoch) chỉ sửa được lỗi "heartbeat không bao
giờ publish" — nhưng heartbeat vẫn CHỈ publish tại state
APP_CYCLE_SENDING, tức là **CHỈ có cơ hội publish 1 lần MỖI LAP
sleep_ms (30 phút), CÙNG LÚC với data**. Với cấu hình hiện tại
(heartbeat_ms=900s=15p < sleep_ms=1800s=30p), thực tế đo được là
heartbeat VẪN publish đúng mỗi 30 phút (bám theo sleep_ms), KHÔNG
PHẢI mỗi 15 phút như người dùng muốn. Con số heartbeat_ms=900s
hiện tại không tạo ra khác biệt hành vi nào so với heartbeat_ms=
1800s hay bất kỳ giá trị nào <= sleep_ms.

ĐÃ TỪNG THIẾT KẾ + VIẾT DỞ 1 GIẢI PHÁP CHO VIỆC NÀY TRONG PHIÊN
NÀY (state machine APP_MODE_HB_ONLY, app_hb_only_process(), chia
sleep_ms thành các đoạn heartbeat_ms, dậy riêng mỗi 15p chỉ bật
modem+IMU pub heartbeat rồi ngủ lại, không đụng GPS/bơm/SPS30) —
NHƯNG SAU ĐÓ NGƯỜI DÙNG YÊU CẦU BỎ ĐI (để pull sạch code watchdog
"add watchdog" mới trên remote, tránh xung đột merge) VÀ CHƯA VIẾT
LẠI. Code đó KHÔNG CÒN TỒN TẠI trong working tree hiện tại (đã bị
discard qua git checkout --, không có trong stash).

THIẾT KẾ CŨ (tham khảo lại nếu viết lại theo hướng này) — ĐÃ
NGHIÊN CỨU KỸ, CÓ THỂ DÙNG LẠI TRỰC TIẾP, KHÔNG CẦN NGHIÊN CỨU LẠI
TỪ ĐẦU:
  1. Chia sx_sleep_manager_enter_sleep(sleep_ms/1000) (hiện đang
     ngủ 1 mạch 1800s) thành nhiều đoạn ngủ ngắn hơn, mỗi đoạn dài
     heartbeat_ms (900s), lặp lại cho tới khi tổng đủ sleep_ms.
  2. Đoạn ngủ ĐẦU TIÊN trong 1 lap: chạy sx_sleep_manager_enter_
     sleep() ĐẦY ĐỦ như hiện tại (7 sleep_steps: tắt GPS/modem/
     SPS30/bơm/ZE12A/accel, rồi STOP mode) — vì lúc đó GPS/bơm/
     SPS30 vẫn đang bật từ ON_PUMP/SENSING/SENDING.
  3. Các đoạn ngủ TIẾP THEO trong cùng lap: dùng tier-1 STOP mode
     TRẦN (sx_sleep_set_rtc_wake() + sx_sleep_enter_stop() gọi trực
     tiếp, KHÔNG chạy lại 7 sleep_steps) — vì GPS/SPS30/bơm/ZE12A
     đã được parked từ đoạn đầu, không cần tắt lại.
  4. Sau mỗi đoạn ngủ ngắn thức dậy: kiểm tra đã đủ sleep_ms chưa.
     - Nếu ĐỦ rồi -> chạy full wake sequence hiện có (GPS wait_fix,
       modem, MQTT...) như bình thường, không đổi gì.
     - Nếu CHƯA đủ -> chạy 1 "mini wake sequence" riêng: CHỈ bật
       modem (resume UART qua sx_board_uart_resume_it(), comm_reset(),
       power_on_start(), rồi start() đúng lúc !power_is_busy()) +
       accel (accel_app_wake_step_start(), rẻ, đồng bộ) — KHÔNG
       đụng GPS/bơm/SPS30. Đợi modem is_ready() + MQTT connect
       (sx_user_mqtt_is_connected()), gọi send_heartbeat_if_due(),
       đợi publish xong (sx_user_mqtt_is_publishing()==0), rồi tắt
       modem (power_off_start(), comm_reset()) + accel suspend, quay
       lại ngủ đoạn kế tiếp.
  5. Cần 1 flag kiểu "đã chạy đủ 7 sleep_steps trong lap này chưa"
     để biết đoạn ngủ hiện tại là đoạn đầu (full sleep_steps) hay
     đoạn sau (bare STOP) — reset flag về false ở đầu mỗi lap mới
     (APP_CYCLE_ON_PUMP, first tick).
  6. Payload heartbeat GIỮ NGUYÊN như build_heartbeat_payload() đã
     có (deviceID, timestamp, signalStrength, operator, motionState)
     — không cần thêm/bớt field nào, người dùng đã xác nhận "chỉ cần
     pub những thứ như heartbeat hiện tại thôi".
  7. Cần mở rộng is_modem_owned_by_sleep_manager() (app.c) để bao
     gồm cả trạng thái đang chạy mini wake sequence, tránh
     sx_mqtt.c's recovery ladder gọi start() chồng lấn — đã có tiền
     lệ y hệt (bug đã fix 2026-08-01 giữa sleep_manager và
     recovery ladder), lý luận giống hệt, chỉ thêm 1 điều kiện OR.

ĐIỂM CẦN CẨN THẬN KHI VIẾT LẠI (rút kinh nghiệm từ lần viết dở
trước, để không lặp lại sai sót):
  - state machine mini-wake PHẢI resume UART + comm_reset() TRƯỚC
    power_on_start(), không phải sau power_off — đây là lỗi tôi đã
    tự phát hiện và sửa giữa chừng lần viết trước, xem kỹ đúng thứ
    tự trong sx_sleep_manager.c's _modem_power_on_start()/
    _modem_wait_ready_is_done() làm mẫu.
  - start() modem chỉ được gửi ĐÚNG 1 LẦN, tại đúng lúc
    !power_is_busy() lần đầu, không phải mỗi tick — cần 1 flag
    kiểu s_hb_start_sent, reset về 0 mỗi khi bắt đầu chu kỳ mini-
    wake mới.
  - Cẩn thận trường hợp chunk_ms tính ra 0 (do sleep_ms không chia
    hết cho heartbeat_ms) — không được gọi sleep(0/1000), cần fallback
    hợp lý (đã có hướng xử lý ở thiết kế cũ, xem lại nếu cần).
  - accel_app_wake_step_start()/sleep_step_start() gọi lặp đi lặp
    lại nhiều lần trong 1 lap (1 lần bởi mini-wake, 1 lần nữa bởi
    full sleep_steps ở đoạn cuối lap) — CHƯA XÁC NHẬN có an toàn
    tuyệt đối không, cần kiểm tra kỹ accel_app.c hoặc hỏi người
    dùng test thử trước khi tin tưởng hoàn toàn.

============================================================
2A. WATCHDOG (IWDG) — VIẾT XONG, CHƯA BUILD/TEST, CHƯA COMMIT
============================================================
Người dùng yêu cầu: watchdog ~30s, CHỈ hoạt động lúc full-power và
wakeup, "tắt" lúc sleep. Do giới hạn phần cứng IWDG (không thể tắt
bằng software, LSI vẫn chạy xuyên STOP mode trừ khi có option byte
đặc biệt), đã dùng WWDG ban đầu (không khả thi, timeout tối đa chỉ
~1s) rồi chuyển sang IWDG (khả thi, đạt đúng 30s).

Cấu hình CubeMX đã set (đã pull, đã xác nhận đúng số):
  Prescaler = IWDG_PRESCALER_256
  Reload    = 3750
  Window    = 4095 (không giới hạn, refresh lúc nào cũng được)
  -> timeout = 3750 x (256/32000) = 30.000s chính xác (LSI danh
     định 32kHz, dao động thực tế có thể ±5-10% theo datasheet)

Cơ chế "đóng băng lúc sleep" dùng FLASH OPTION BYTE (không phải
runtime register) IWDG_STOP=FREEZE — counter tự đóng băng phần
cứng khi vào STOP mode, không đếm tiếp, bất kể sleep_ms dài bao
nhiêu. Option byte này set 1 lần, lưu vĩnh viễn trong flash, sống
sót qua power-cycle/reflash bình thường.

5 file đã sửa (xem mục TRẠNG THÁI GIT ở đầu handoff):
  1. Core/Src/main.c — hàm ensure_iwdg_frozen_in_stop_option_byte(),
     gọi ngay sau HAL_Init(), TRƯỚC SystemClock_Config() và TRƯỚC
     MX_IWDG_Init(). Tự kiểm tra option byte hiện tại, nếu đã đúng
     thì no-op (mọi lần boot sau lần đầu), nếu chưa đúng thì ghi +
     gọi HAL_FLASH_OB_Launch() (hàm này TỰ RESET MCU NGAY LẬP TỨC
     để nạp lại option byte mới — LẦN BOOT ĐẦU TIÊN trên board
     chưa từng set sẽ có thêm 1 lần reset phụ, đây LÀ HÀNH VI BÌNH
     THƯỜNG, không phải lỗi, cần báo trước cho người dùng khi họ
     test lần đầu để không hoảng).
  2. SynaptiX_FDK/app/app.c — thêm #include "iwdg.h". Refresh
     HAL_IWDG_Refresh(&hiwdg) ở ĐẦU app_process(), CHỈ khi
     s_app_mode == APP_MODE_FULL_POWER hoặc APP_MODE_WAKEUP. Thêm 1
     lần refresh cuối cùng NGAY TRƯỚC lời gọi
     sx_sleep_manager_enter_sleep() (để "nạp đầy" 30s countdown
     trước khi vào STOP).
  3. SynaptiX_FDK/services/sleep_service/sx_sleep_service.h — thêm
     field pre_stop_refresh (function pointer void(void), NULLable,
     không phá vỡ caller khác) vào sx_sleep_service_t. Thêm tham số
     cùng tên vào prototype sx_sleep_service_init().
  4. SynaptiX_FDK/services/sleep_service/sx_sleep_service.c — sửa
     sx_sleep_service_init() nhận + lưu tham số pre_stop_refresh.
     Gọi callback này (nếu khác NULL) trong
     sx_sleep_service_enter_sleep(), NGAY SAU khi 7 sleep_steps chạy
     xong, NGAY TRƯỚC sx_sleep_enter_stop() — đây là điểm refresh
     "sát nhất" trước khi counter bị đóng băng, phòng trường hợp 7
     sleep_steps mất nhiều thời gian hơn dự kiến trong tương lai.
  5. SynaptiX_FDK/app/user/sx_sleep_manager/sx_sleep_manager.c —
     thêm #include "iwdg.h" (đây là tier 3, project-specific, được
     phép biết IWDG là gì — tier 2 sx_sleep_service.c KHÔNG được
     phép biết, giữ đúng ranh giới kiến trúc 3 tầng sẵn có của dự
     án). Viết hàm static _iwdg_refresh(void) gọi
     HAL_IWDG_Refresh(&hiwdg), truyền vào sx_sleep_service_init()
     làm tham số pre_stop_refresh cuối cùng.

Đã kiểm tra: ngoặc cân bằng cả 5 file (script Python đếm { }, không
phải build thật), không sót tham chiếu biến/hàm cũ, thứ tự gọi
HAL_Init() trước ensure_iwdg_frozen... đúng, include đầy đủ
(HAL_IWDG_MODULE_ENABLED và HAL_FLASH_MODULE_ENABLED đã bật sẵn
trong stm32h5xx_hal_conf.h, không cần sửa thêm).

CHƯA KIỂM TRA (cần làm ở phiên sau hoặc người dùng tự test):
  - Build thật bằng toolchain ARM (không có trong môi trường làm
    việc của phiên này).
  - Chạy thật trên board — đặc biệt xác nhận: (a) lần boot đầu có
    reset phụ 1 lần như dự kiến rồi hoạt động bình thường từ lần
    2 trở đi; (b) board ngủ được xuyên suốt 1800s không bị IWDG
    reset giữa chừng; (c) nếu cố tình làm treo code ở
    FULL_POWER/WAKEUP quá 30s, IWDG có thực sự reset board (xác
    nhận bảo vệ thật hoạt động, không chỉ lý thuyết).

============================================================
FOTA — GHI CHÚ, KHÔNG PHẢI VIỆC CẦN LÀM
============================================================
Nhánh main HIỆN TẠI KHÔNG CÓ FOTA. Có 1 commit cũ (c9352dc "build
fail") từng gọi fota_is_pending()/fota_download()/fota_init()/
fota_on_message() + #include "fota.h" nhưng fota.c/fota.h CHƯA
TỪNG tồn tại trong repo trên nhánh này -> code không build được.
Đã gỡ sạch cả 4 điểm gọi + include khỏi app.c ở 1 thời điểm trong
phiên này, NHƯNG SAU ĐÓ người dùng yêu cầu discard để pull code
watchdog mới -> bản gỡ FOTA đó KHÔNG CÒN trong working tree hiện
tại. Cần kiểm tra lại: app.c hiện tại (sau các lần pull mới nhất)
CÓ CÒN gọi fota_* hay không trước khi build — nếu commit "add
watchdog"/"add peripheral iwdg" không đụng gì tới đoạn FOTA cũ, rất
có thể vẫn còn tồn tại y nguyên, cần gỡ lại theo đúng cách đã làm
trước đó (xem lịch sử chat phiên này nếu cần đối chiếu chi tiết
từng đoạn đã xóa). FOTA thật (nếu cần) nằm ở nhánh ft/fota_ws,
KHÔNG được trộn vào main cho tới khi fota.c/fota.h thực sự được
thêm vào.

============================================================
QUY TẮC BẮT BUỘC (kế thừa, không đổi)
============================================================
- Nhánh làm việc: main (không phải ft/fota_ws).
- RE-CLONE/PULL đầu phiên: git pull origin main ngay khi bắt đầu,
  luôn kiểm tra git status trước để biết có thay đổi local chưa
  commit không (phiên này liên tục gặp tình huống này, đã xử lý
  bằng cách hỏi người dùng discard hay giữ).
- KHÔNG tin log/mô tả cũ mà không tự đọc lại code thật.
- KHÔNG có compiler ARM thật trong container — không build được.
  Người dùng tự build + flash + gửi log qua chat. Chỉ kiểm tra được
  cú pháp cơ bản (ngoặc cân bằng) bằng script, KHÔNG thay thế cho
  build thật.
- Board test vật lý duy nhất: STM32H563RIV6.
- Log thật/phép đo tay LUÔN thắng datasheet khi có xung đột.
- KHÔNG khẳng định chắc chắn hơn những gì bằng chứng thật sự cho
  thấy.