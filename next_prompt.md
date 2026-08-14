HANDOFF — WS_v1 (nhánh ft/heartbeat) — TIẾN ĐỘ PHIÊN NÀY
============================================================
Ngày: 2026-08-13
Nhánh làm việc: ft/heartbeat

repo: https://github.com/logan123synaptix/WS_v1.git
clone và pull ft/heartbeat trước

============================================================
TRẠNG THÁI GIT — QUAN TRỌNG, ĐỌC TRƯỚC
============================================================
File đang có thay đổi CHƯA COMMIT trên máy làm việc (chưa push),
KHÔNG BUILD THẬT (không có toolchain ARM trong môi trường làm việc),
CHƯA CHẠY TRÊN BOARD:

    Makefile
    SynaptiX_FDK/app/app.c
    SynaptiX_FDK/components/modules/imu/bno055.c
    SynaptiX_FDK/components/modules/imu/bno055.h
    SynaptiX_FDK/synaptix.mk

Untracked (file mới, chưa git add):
    SynaptiX_FDK/app/user/imu_velocity/           (imu_velocity.c/.h)
    SynaptiX_FDK/components/third_party/embfilt/  (vendored MA + median
                                                     filter, MIT license,
                                                     xem README.md trong
                                                     thư mục đó)

Chỉ kiểm tra được ngoặc cân bằng (script Python đếm { } ( )) qua mọi
file trên — KHÔNG THAY THẾ CHO BUILD THẬT.

============================================================
ĐÃ FIX, ĐÃ XÁC NHẬN QUA HARDWARE THẬT (phiên trước + phiên này)
============================================================

Các bug sau đã fix và CONFIRM trên board thật (kế thừa từ phiên
2026-08-12, không đổi gì thêm phiên này — xem lại nếu cần chi tiết):
  - HB_ONLY modem PWRKEY cooldown (HB_ONLY_MODEM_COOLDOWN_MS=15000)
  - sensorStatus SO2/NO2/O3 sai (gas_last_full_wake_ok[] snapshot,
    field-name mismatch giữa .h/.c đã fix, giờ build được)
  - ZE12A cold-boot không đọc được khí (gọi gas_sensor_switch_to_
    active_mode() 2 lớp: sx_board.c ngay sau init + app.c's
    APP_CYCLE_ON_PUMP's first tick — CONFIRMED bởi người dùng, hết
    lỗi sau power-cycle thật)

============================================================
FIX PHIÊN NÀY — operator: null trong heartbeat
============================================================
Nguyên nhân: AT+COPS? có thể trả "+COPS: 0,0,\"\",7" (tên operator
rỗng THẬT từ modem, không phải bug parse) ngay sau CREG registered —
race condition mạng thật, chỉ đọc 1 lần mỗi lần network attach,
không retry.

Fix: network_config_get_carrier_name() (network_config.c/.h) — bảng
map tĩnh APN -> tên nhà mạng VN (m3-world->VinaPhone, v-internet->
Viettel, m-wap->MobiFone, đã tra cứu web xác nhận đúng APN từng nhà
mạng). app.c's build_heartbeat_payload() ưu tiên đọc bảng map này
trước (dựa network_config_get()->apn, đọc từ flash, ổn định suốt
session), chỉ fallback về sx_user_mqtt_get_operator() (đọc modem)
nếu APN không có trong bảng.

TRẠNG THÁI: code đã fix, đã push, NHƯNG người dùng báo log thật
(chạy liên tục >12h không reboot) vẫn cho ra "operator": null XEN
KẼ với "VinaPhone" trong CÙNG 1 session — điều này KHÔNG khớp logic
(network_config_get()->apn là giá trị tĩnh, load 1 lần lúc boot,
không đổi giữa các lap). ĐÃ THÊM 1 dòng debug log tạm trong app.c:

    log_info(TAG, "[DEBUG OPERATOR] apn=[%s] carrier_lookup=[%s] cops_operator=[%s]", ...);

CHƯA XÁC ĐỊNH ĐƯỢC NGUYÊN NHÂN THẬT của việc null/VinaPhone xen kẽ.
Cần người dùng build+flash+chạy vài lap (cả HB_ONLY và full-wake),
gửi lại đoạn log [DEBUG OPERATOR] để xác định — có thể là race
condition đọc s_cfg.apn, hoặc network_config_get_carrier_name() bị
gọi trước network_config_init() hoàn tất ở 1 số lap nào đó, hoặc
nguyên nhân khác chưa nghĩ tới. KHÔNG ĐOÁN THÊM khi chưa có log này.
Nhớ xóa dòng debug log này sau khi xác định xong nguyên nhân.

============================================================
★★★ TÍNH NĂNG MỚI ĐANG PHÁT TRIỂN — TRỌNG TÂM PHIÊN NÀY ★★★
Pump-on-speed-threshold (dùng lỗ thông khí tự nhiên khi xe > 20km/h)
============================================================

Ý tưởng gốc (người dùng): trạm có lỗ thông khí tự nhiên — xe di
chuyển đủ nhanh thì khí tự lùa vào, không cần bật bơm (tiết kiệm
điện/mòn bơm). Chỉ bật bơm khi xe dừng/đi chậm (< threshold km/h,
default 20, cần config được qua shell) — ví dụ trong hầm, đèn đỏ.

NGUỒN TỐC ĐỘ: GPS speed (gps->speed, đã có sẵn trong gps.c, đơn vị
knots) là nguồn duy nhất cho số km/h chính xác — nhưng GPS hay mất
fix đúng lúc cần nhất (hầm). Người dùng yêu cầu: khi mất GPS fix,
dùng IMU (BNO055) để tự ước lượng vận tốc, calib theo GPS làm tham
chiếu khi có fix.

--- ĐÃ KIỂM TRA, KHÔNG PHẢI GIẢ ĐỊNH ---
- accel_app_is_movement_detected() (accel_app.c) hiện tại CHỈ phát
  hiện rung động/thay đổi gia tốc (magnitude, low-pass filter, so
  độ lệch với threshold) — KHÔNG PHẢI tốc độ km/h thật, không dùng
  được trực tiếp cho ngưỡng cụ thể.
- BNO055 datasheet (Documents/bno055.md) TỰ CẢNH BÁO: "linear
  acceleration signal typically cannot be integrated to recover
  velocity... error typically becomes larger than the signal within
  less than 1 second if other sensor sources are not used to
  compensate". Cũng cảnh báo riêng: fusion algorithm không thiết kế
  cho xe cộ (cornering/braking mạnh kéo dài có thể làm sai lệch
  gravity vector). Đây là giới hạn VẬT LÝ của MEMS + tích phân số,
  không phải thiếu sót code — đã giải thích rõ với người dùng.
- gps.c CHƯA parse course/heading (minmea hỗ trợ field này nhưng
  code hiện tại bỏ qua) — cần thêm nếu làm tiếp Stage B's forward-
  axis (xem dưới).
- Không có EN_PW/GPIO enable riêng cho ZE12A (đã biết từ phiên
  trước) — không liên quan tính năng này, nhắc lại cho đủ ngữ cảnh.

--- KIẾN TRÚC: GPS-Referenced IMU Velocity Estimator, 3 giai đoạn ---
(xem doc-comment đầy đủ, rất chi tiết, ở đầu file
SynaptiX_FDK/app/user/imu_velocity/imu_velocity.h — ĐỌC FILE ĐÓ
TRƯỚC KHI SỬA GÌ, comment ở đó là nguồn thông tin đầy đủ nhất)

  STAGE A — Bias calibration: TRỪ giá trị offset tĩnh của accel khi
  đứng yên (nếu không trừ, tích phân sẽ tạo vận tốc "ảo" ngay cả lúc
  đứng im). ĐÃ CODE XONG, TEST ĐƯỢC NGAY LÚC ĐỨNG YÊN (không cần xe,
  không cần GPS).

  STAGE A2 — Temperature-compensated bias (thêm giữa phiên, theo
  yêu cầu người dùng "phải có cách nào đó giảm sai số"): bias KHÔNG
  phải hằng số cố định — trôi theo nhiệt độ (well-known trong MEMS
  accelerometer literature, đã tra cứu web, trích dẫn cụ thể trong
  code: PMC8124870, guidenav.com 2025). Giờ bias(T) = intercept +
  slope * T thay vì 1 số cố định. Cơ chế: mỗi lần đứng yên, sample
  được tự phân vào 1 trong 2 "cluster" nhiệt độ (lạnh nhất/nóng nhất
  đã thấy, ngưỡng tách biệt tối thiểu
  IMU_VELOCITY_TEMP_CLUSTER_MIN_SEPARATION_C=10°C) — khi đủ 2
  cluster tách biệt, tự tính slope. ĐÃ CODE XONG, TEST ĐƯỢC NGAY LÚC
  ĐỨNG YÊN (cần để board qua vài lần đứng yên ở nhiệt độ khác nhau
  để thấy slope != 0, ví dụ mới bật máy vs sau khi chạy lâu ấm lên).

  STAGE A3 — Re-calib mỗi lần dừng (ZUPT + refresh bias): mỗi khi
  xe dừng hẳn, KHÔNG CHỈ reset velocity=0 (ZUPT chuẩn INS) mà còn
  tiếp tục feed sample mới vào Stage A2's fit — bias "học" liên tục
  suốt vòng đời vận hành, không chỉ 1 lần lúc boot. ĐÃ TÍCH HỢP
  trong app.c (xem dưới).

  STAGE B — Trục tham chiếu: phần "trục xuống" (dùng
  bno055_get_gravity()) TEST ĐƯỢC ĐỨNG YÊN, ĐÃ CODE XONG. Phần
  "trục tiến" (forward axis, hướng xe chạy) KHÔNG THỂ xác định lúc
  đứng yên — cần xe di chuyển thật + so với GPS course. CHƯA CODE
  (cần thêm course vào gps.c trước). Hiện tại dùng giá trị giả định
  IMU_VELOCITY_ASSUMED_FORWARD_AXIS (mặc định trục X), có log rõ
  "ASSUMED, not measured" để không ai nhầm là đã calib thật.

  STAGE C — Scale factor (2 tầng, theo yêu cầu người dùng "tầng 1
  nhiệt độ, tầng 2 hàm theo GPS"): sau khi trừ bias theo nhiệt độ
  (tầng 1) và tích phân ra vận tốc thô, NHÂN thêm scale_factor để
  bù sai số hệ thống còn sót (scale error cảm biến, sai số chiếu
  trục...). imu_velocity_scale_calib_update(state, gps_speed_kph)
  — CHỈ CÓ KHUNG, chưa chạy được, người dùng xác nhận CHƯA CÓ điều
  kiện chạy xe thật (chỉ test đứng yên/trong phòng). Hiện tại
  scale_factor là 1 hằng số nhân duy nhất (running average đơn
  giản, alpha=0.05) — ĐANG DỞ DANG CÂU HỎI: người dùng được hỏi có
  muốn scale_factor phức tạp hơn (đổi theo dải tốc độ thay vì 1 số
  cố định) hay giữ đơn giản — CHƯA CÓ CÂU TRẢ LỜI, session bị ngắt
  giữa chừng ngay lúc chờ người dùng chọn. HỎI LẠI CÂU NÀY TRƯỚC
  KHI SỬA STAGE C.

--- GIỚI HẠN THẬT, ĐÃ GIẢI THÍCH RÕ VỚI NGƯỜI DÙNG, KHÔNG PHẢI CHE GIẤU ---
Ngay cả với cả 3 tầng calib (A + A2 + A3 + B + C) hoàn chỉnh, drift
KHÔNG BAO GIỜ bị loại bỏ hoàn toàn — đây là giới hạn vật lý của MEMS
+ tích phân số, không phải thiếu sót thuật toán (đã trích dẫn
nguồn: "Even with strong hardware design, precise calibration, and
real-time compensation, small residual drift will always remain").
ZUPT (reset về 0 mỗi lần dừng hẳn) là cơ chế chính giữ sai số trong
tầm kiểm soát — phù hợp cho use-case cụ thể này (hầm/dừng ngắn vài
chục giây tới vài phút giữa các lần ZUPT), KHÔNG phù hợp cho dead-
reckoning dài hạn không có GPS. Người dùng đã được thông báo rõ,
đồng ý hướng đi này ("phải có 1 cách nào đó... + zupt").

--- ĐÃ TÍCH HỢP VÀO app.c, CHƯA TÍCH HỢP VÀO LOGIC BƠM ---
- imu_velocity_state_t s_imu_velocity (biến static mới)
- imu_velocity_init() gọi trong app_init() cạnh accel_app_init()
- Mỗi tick trong vòng lặp chính: nếu
  accel_app_is_movement_detected()==false liên tục
  >=IMU_VELOCITY_STATIONARY_CONFIRM_MS (3000ms, ngưỡng riêng của
  module này, nghiêm ngặt hơn accel_app's per-tick flag) thì gọi
  imu_velocity_bias_calib_sample() + imu_velocity_axis_calib_sample()
- CHƯA gọi imu_velocity_zero_velocity_update() ở đâu cả (cần thêm)
- CHƯA gọi imu_velocity_poll() ở đâu cả trong vòng lặp chính lúc xe
  đang di chuyển — cần thêm khi bắt đầu tích hợp vào logic bơm thật
- CHƯA có logic thay đổi hành vi bơm (APP_CYCLE_ON_PUMP) dựa theo
  tốc độ — toàn bộ phần "khi nào bật/tắt bơm dựa threshold" CHƯA
  LÀM, mới chỉ có phần đo vận tốc (nền tảng)

============================================================
BUILD SYSTEM — ĐÃ CẬP NHẬT
============================================================
- Makefile: thêm -I cho app/user/imu_velocity và
  components/third_party/embfilt
- synaptix.mk: thêm imu_velocity.c, ma_filt.c, median_filt.c vào
  danh sách source (chú ý: sx_pump.c trước đây là dòng CUỐI của
  COMPONENT_FILES, không có \ cuối dòng — đã thêm \ vào và nối thêm
  2 dòng mới, kiểm tra lại nếu thêm file component mới sau này)

============================================================
CÔNG CỤ MỚI/THAY ĐỔI PHIÊN NÀY
============================================================
- bno055_get_temperature() — hàm mới trong driver
  (SynaptiX_FDK/components/modules/imu/bno055.c/.h), đọc register
  TEMP (0x34), 1 byte signed, 1 LSB = 1°C, không cần scale. Trước
  đây driver hoàn toàn chưa expose hàm này dù chip hỗ trợ.
- SynaptiX_FDK/components/third_party/embfilt/ — vendored 2 file từ
  https://github.com/huunghiaspkt/embfilt (MIT), theo yêu cầu người
  dùng tham khảo bộ lọc có sẵn thay vì tự viết lại: ma_filt.*
  (moving average, circular buffer) và median_filt.* (loại outlier).
  Repo gốc còn có EMA/Kalman/IIR filter khác chưa vendor — lấy thêm
  nếu giai đoạn sau cần (ví dụ Stage C có thể cần EMA thay vì running
  average tay hiện tại nếu người dùng muốn tunable cutoff frequency).

============================================================
QUY TẮC BẮT BUỘC (kế thừa, không đổi)
============================================================
- Nhánh làm việc: ft/calib_vel.
- RE-CLONE/PULL đầu phiên: git pull origin ft/calib_vel ngay khi
  bắt đầu, luôn kiểm tra git status trước để biết có thay đổi local
  chưa commit không.
- KHÔNG tin log/mô tả cũ mà không tự đọc lại code thật.
- KHÔNG có compiler ARM thật trong container — không build được.
  Người dùng tự build + flash + gửi log qua chat. Chỉ kiểm tra được
  cú pháp cơ bản (ngoặc cân bằng) bằng script, KHÔNG thay thế cho
  build thật.
- Board test vật lý duy nhất: STM32H563RIV6.
- Log thật/phép đo tay LUÔN thắng datasheet/giả thuyết khi có xung
  đột.
- KHÔNG khẳng định chắc chắn hơn những gì bằng chứng thật sự cho
  thấy. Bug operator:null phiên này là ví dụ điển hình — code fix
  ĐÚNG VỀ MẶT LOGIC nhưng log thật cho thấy vẫn còn vấn đề chưa hiểu
  hết, KHÔNG được báo "đã xong" cho tới khi có log [DEBUG OPERATOR]
  xác nhận.
- Với tính năng imu_velocity: đây là công nghệ có giới hạn vật lý
  thật (đã giải thích, đã dẫn nguồn khoa học cụ thể trong code) —
  KHÔNG được hứa hẹn "chính xác tuyệt đối không cần GPS" với người
  dùng, luôn nói rõ đây là giảm drift + ZUPT, không phải loại bỏ
  hoàn toàn.