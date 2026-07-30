HANDOFF — HARDWARE BRING-UP TEST, WS_v1 (STM32H563RIV6) — PHIÊN 4

Viết khi sắp hết token. Đọc kỹ 3 handoff phiên trước (trong lịch sử chat)
trước khi làm gì — phiên này KẾ TIẾP trực tiếp từ phiên 3.

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
thông số. Log thật LUÔN thắng datasheet khi có xung đột.
**QUAN TRỌNG MỚI (phát hiện phiên 3): container KHÔNG có network/
credential để `git push` lên GitHub** (lỗi "could not read Username
for 'https://github.com'"). Sau khi sửa code và người dùng xác nhận
đồng ý, PHẢI nhắc người dùng cách lấy code ra khỏi container — dùng
`git diff`/patch file xuất ra /mnt/user-data/outputs/ rồi
present_files, hoặc đọc trực tiếp nội dung file đã sửa và yêu cầu
người dùng tự copy/paste, hoặc hỏi người dùng có cách nào khác đồng
bộ code không. ĐỪNG giả định push được — đã xảy ra 2 lần trong dự án
này (phiên 3 và có nguy cơ lặp lại) khiến người dùng build phải code
CŨ không có fix, tưởng nhầm là fix không hoạt động.
============================================================
TÌNH TRẠNG ĐẦU PHIÊN 4 — RẤT QUAN TRỌNG, ĐỌC KỸ
============================================================
Cuối phiên 3, đã sửa xong 2 bug lớn và ĐƯỢC NGƯỜI DÙNG XÁC NHẬN ĐÃ TEST
TRÊN BOARD THẬT, CHẠY ĐÚNG:
1. VDD_EXT/1.8V giờ tắt đúng (về 0V) sau power-off — trước đây không
   tắt vì code báo "done" giả sau 500ms, giờ đợi đúng bằng
   power_is_busy().
2. Publish sau khi wake không còn treo — trước đây bị treo vô hạn vì
   sleep_step "modem_power_off" chạy trong vòng lặp blocking không có
   ai tick động cơ state machine của modem, dẫn tới power_is_busy()
   không bao giờ về false.

NHƯNG: các fix này CHƯA CHẮC ĐÃ NẰM TRÊN GITHUB. Container không push
được lên remote (xem "QUAN TRỌNG MỚI" ở trên). Cuối phiên 3, đã commit
local (commit message "fix: sleep/wake race conditions + modem
power-off blocking hang") nhưng KHÔNG push được. Đã xuất
git diff dưới dạng patch file và trình bày handoff này để người dùng
tự đồng bộ code — CHƯA XÁC NHẬN người dùng đã áp dụng patch này vào
repo thật hay chưa. VIỆC ĐẦU TIÊN phiên 4: hỏi người dùng xem code đã
có 2 fix trên GitHub chưa (kiểm tra bằng cách đọc trực tiếp code, xem
mục "Cách xác nhận nhanh" bên dưới), nếu chưa thì phải fix lại lần nữa
(nội dung y hệt, xem "Nội dung fix cụ thể" bên dưới để không phải suy
luận lại từ đầu).

Cách xác nhận nhanh xem 2 fix trên đã có trong repo hiện tại chưa:
  grep -n "power_off_last_tick_ms" SynaptiX_FDK/app/user/sx_sleep_manager/sx_sleep_manager.h
  grep -n "s_publish_done" SynaptiX_FDK/app/user/test/test_sleep.c
Nếu cả 2 lệnh trên KHÔNG ra kết quả gì → fix chưa có trên GitHub, cần
làm lại. Nếu có → fix đã persist, tiếp tục các việc mới bên dưới.

============================================================
NỘI DUNG FIX CỤ THỂ CỦA PHIÊN 3 (để áp dụng lại nếu bị mất)
============================================================
File patch đầy đủ đã xuất ra /mnt/user-data/outputs/session3_fixes.patch
trong phiên 3 (dạng git diff, base là commit 451ebf3 "fix build") —
KIỂM TRA XEM FILE NÀY CÒN TỒN TẠI TRONG CONVERSATION KHÔNG (đã present
cho người dùng qua present_files cuối phiên 3, có thể người dùng vẫn
còn giữ). Nếu còn, đọc nó và áp dụng lại bằng `git apply` thay vì gõ
tay lại từ đầu.

Tóm tắt logic 3 chỗ sửa (nếu cần viết lại tay):

1. SynaptiX_FDK/app/user/test/test_sleep.c — guard WAKING (từ phiên 2,
   xác nhận vẫn cần thiết): trong test_sleep_poll(), bọc
   sx_temp_humi_poll()/accel_app_poll() bằng
   `if (s_state != TEST_SLEEP_STATE_WAKING) { ... }` — không đổi
   gps_process() (không cần guard, gps_process() không phát warning khi
   chưa có data, khác I2C).

2. SynaptiX_FDK/app/user/test/test_sleep.c — case TEST_SLEEP_STATE_PUBLISH
   giờ CHỜ publish thật sự xong (qua on_publish() callback) trước khi
   chuyển sang ENTER_SLEEP, thay vì gọi sx_user_mqtt_publish() (fire-
   and-forget) rồi chuyển state ngay. Thêm 3 static: s_publish_sent,
   s_publish_done, s_publish_success. on_publish() set s_publish_done=1
   + s_publish_success. State PUBLISH: nếu !s_publish_sent thì gọi
   publish + set sent=1 + return; nếu sent nhưng !s_publish_done thì
   return (đợi); nếu done nhưng !s_publish_success thì log warn + return
   (retry lần poll sau); chỉ khi done && success mới chuyển
   ENTER_SLEEP. Lý do: trước đây modem bị power-off (sleep_step
   modem_power_off) giữa lúc AT+CMQTTTOPIC/PAYLOAD/PUB sequence còn
   đang chạy dở trên UART, gây timeout + publish fail 3 lần liên tục,
   đúng vào lúc app đang ở state WAKING (ngoài dự kiến).

3. SynaptiX_FDK/app/user/sx_sleep_manager/sx_sleep_manager.c — sửa
   _modem_power_off_is_done() (BUG LỚN NHẤT, gây treo vô hạn thật sự
   trên board — đã xác nhận bằng log + đo tay 1.8V):
   - TRƯỚC: return 1 cứng sau 500ms delay trong _start(). Modem chưa
     kịp tắt thật (PWRKEY pulse + settle mất ~7.1s, xem
     A7677S_OFF_PULSE_MS=2600 + A7677S_OFF_SETTLE_MS=4500 trong
     a7677s.h), MCU đã vào STOP mode giữa chừng → 1.8V vẫn còn.
   - FIX BƯỚC 1: đổi _modem_power_off_is_done() sang check
     `!mgr->modem->ops->power_is_busy(mgr->modem->ctx)` thay vì return 1
     cứng.
   - PHÁT SINH BUG MỚI (đã tìm ra và fix trong CÙNG phiên 3): sleep_steps
     chạy trong sx_sleep_service.c's _run_steps_blocking() — vòng lặp
     BLOCKING `while (!is_done()) { sx_delay_ms(10); }`. Trong lúc này,
     KHÔNG AI gọi modem_handle_poll()/a7677s_poll() để tick động cơ
     trạng thái modem tiến triển (modem_handle_poll() bình thường chỉ
     được gọi từ sx_mqtt_poll() -> sx_user_mqtt_poll() ->
     test_sleep_poll() mỗi tick main loop — nhưng main loop này không
     chạy trong lúc blocking loop trên đang block). Kết quả:
     power_elapsed bên trong driver đứng yên mãi ở 0, power_is_busy()
     mãi mãi true → TREO VÔ HẠN THẬT (đã tái hiện trên board, bạn nói
     "treo ở đây luôn").
   - FIX BƯỚC 2 (cuối cùng, đã confirm hoạt động): thêm field
     power_off_last_tick_ms (uint32_t) vào struct sx_sleep_manager_t
     (sx_sleep_manager.h). _modem_power_off_start() lưu sx_gettick()
     vào field này. _modem_power_off_is_done() mỗi lần được gọi (kể cả
     trong blocking loop) tự tính `ts = sx_gettick() -
     power_off_last_tick_ms`, cập nhật lại power_off_last_tick_ms, rồi
     gọi modem_handle_poll(mgr->modem, ts) TRƯỚC KHI check
     power_is_busy(). Nhờ vậy modem's state machine (A7677S_PWR_OFF_
     PULSE -> OFF_SETTLE -> IDLE) vẫn tiến triển đúng dù đang trong
     blocking loop, không cần sửa gì ở tier 2 (sx_sleep_service.c —
     giữ nguyên generic, không biết gì về modem, đúng kiến trúc 3
     tầng).

============================================================
TIẾN TRÌNH TOÀN BỘ DỰ ÁN TÍNH ĐẾN HẾT PHIÊN 3 (CẬP NHẬT)
============================================================
Đã xác nhận sống hoàn toàn qua log + đo tay thật, ổn định (phiên 3)
- UART1 + Modem A7677S: network attach, MQTT connect + publish OK.
- Publish giờ ĐỢI xong (OK) mới cho vào sleep — không còn bị cắt giữa
  chừng.
- Power-off modem qua PWRKEY: xác nhận 1.8V (VDD_EXT) THỰC SỰ tắt về
  0V sau power-off (đo tay, người dùng xác nhận "đúng"), không còn
  hiện tượng "báo done giả" khiến MCU vào sleep khi modem còn sống dở.
- Wake sau sleep: không còn treo ở bước modem_power_off (bug tick
  motor blocking loop đã fix). Bắn được bản tin sau khi wake — người
  dùng xác nhận "khi wakeup đã bắn được bản tin và ko bị treo nữa".
- I2C1 + BNO055 + SHT3x: race condition lúc WAKING đã fix từ phiên 3
  (guard s_state != WAKING), chưa thấy báo lại "read failed" giả trong
  log gần nhất.
- SPI1 + W25Q128, LittleFS, GPS/CASIC AT6558R (test HAL thuần) — như
  các phiên trước, chưa có thay đổi.

Vấn đề ĐÃ PHÁT HIỆN, CHƯA FIX (từ phiên 3, còn treo lại)
1. **hard_reset() (RST pin) sau khi module đã mất nguồn hoàn toàn
   KHÔNG hoạt động đúng** — ĐÃ ĐIỀU TRA KỸ, CHƯA SỬA. Bối cảnh: sau
   khi modem báo "+CME ERROR: SIM failure" lặp lại 3 lần
   (Init sequence failed after max retries), sx_sleep_manager.c gọi
   hard_reset() (RST pin, 2500ms) tại _modem_wait_ready_is_done()'s
   90s timeout. Log cho thấy sau hard_reset(), modem tiếp tục
   TIMEOUT response: [NULL] liên tục — không hồi phục. Người dùng đo
   tay xác nhận: sau hard-reset, chân 1.8V (VDD_EXT) CŨNG MẤT — chứng
   tỏ module đã bị tắt/mất nguồn hoàn toàn, không phải chỉ reset logic
   đơn thuần như datasheet mô tả cho trường hợp chuẩn (datasheet nói
   RESET pin chỉ có tác dụng "while the module is powered on" — dòng
   1190 a7677s.md). NGƯỜI DÙNG ĐÃ CHỈ RÕ HƯỚNG FIX (cuối phiên 3,
   CHƯA THỰC HIỆN kịp vì hết token): sau hard_reset(), code phải chạy
   LẠI TOÀN BỘ chuỗi power-on tuần tự từ đầu (state
   A7677S_PWR_PULSE_HIGH -> PULSE_LOW -> WAIT_BOOT, giống hệt
   power_on_start()), KHÔNG được nhảy thẳng vào A7677S_PWR_WAIT_BOOT
   để probe AT ngay như code hiện tại đang làm (xem
   a7677s.c dòng ~527-539, case A7677S_PWR_RST_PULSE). Code hiện tại
   coi RST pulse xong là "sẵn sàng probe AT" — sai, vì module cần tín
   hiệu PWRKEY (nút bấm chủ động) để tự bật nguồn lại sau khi đã mất
   nguồn hoàn toàn, RST pin thả ra không tự làm module bật lại được.
   VIỆC CẦN LÀM ĐẦU PHIÊN 4: sửa a7677s_hard_reset() hoặc case
   A7677S_PWR_RST_PULSE trong a7677s_poll() để sau khi thả RST pin,
   KHÔNG vào WAIT_BOOT ngay mà chạy tiếp một chu trình PWRKEY đầy đủ
   (PULSE_HIGH -> PULSE_LOW -> WAIT_BOOT), y hệt power_on_start().
   CẦN ĐỌC LẠI a7677s.c thật đầu phiên trước khi sửa (có thể đã đổi
   nếu người dùng tự sửa gì thêm).

2. Log không hiện nhiệt độ/độ ẩm/accel lúc đọc thành công — ĐÃ ĐIỀU
   TRA, CHƯA SỬA (chờ xác nhận người dùng có muốn sửa không, câu hỏi
   đã hỏi cuối phiên 3 nhưng người dùng chuyển sang chủ đề khác chưa
   trả lời trực tiếp — HỎI LẠI đầu phiên 4 nếu liên quan). Nguyên nhân
   xác định rõ, không phải bug logic:
   - SynaptiX_FDK/app/user/sx_temp_humi/sx_temp_humi.c dòng ~48: dùng
     log_debug() (không phải log_info()) khi đọc T/RH thành công — nếu
     log level hiện tại lọc ở mức INFO trở lên thì dòng này bị ẩn.
   - SynaptiX_FDK/app/user/accelerometer_app/accel_app.c: HOÀN TOÀN
     KHÔNG CÓ log nào khi đọc accel thành công (chỉ log khi fail) —
     thiếu observability, không phải bug.
   - GPS: gps.c chỉ log_info khi có NMEA sentence hợp lệ từ GPS thật;
     không có module GPS thật trên bench thì im lặng hoàn toàn theo
     đúng thiết kế (đã ghi từ phiên 1-2).
   Đề xuất (CHƯA LÀM): đổi log_debug -> log_info ở sx_temp_humi.c, và
   thêm 1 dòng log_info mới trong accel_app.c khi đọc thành công — CẦN
   HỎI LẠI người dùng có đồng ý trước khi sửa.

Vấn đề ĐÃ PHÁT HIỆN TỪ PHIÊN TRƯỚC, VẪN CHƯA FIX (chưa đụng tới trong
phiên 3, vẫn còn treo)
- External flash W25Q128 hoàn toàn không có sleep_step nào trong
  sx_sleep_manager.c.
- Timestamp payload sai hoàn toàn dạng "2087-00-00T02:00:41Z" (năm
  2087, tháng/ngày 00) — thấy trong log phiên 3, CHƯA ĐIỀU TRA. Nghi
  vấn: format_timestamp() đọc RTC lỗi hoặc chưa sync xong tại thời
  điểm build payload đầu tiên (seq=1, publish đầu tiên sau boot, trước
  khi "Network time synced" có kịp áp dụng vào RTC nội bộ hay chưa?
  CẦN ĐỌC format_timestamp()/sx_ex_rtc.c để xác nhận). Ưu tiên thấp
  hơn 2 vấn đề trên nhưng dữ liệu sai lệch nghiêm trọng nếu dùng
  payload thật.
- ACCEL_APP_FILTER_ALPHA (0.1f) chưa điều chỉnh theo
  ACCEL_APP_SAMPLE_PERIOD_MS mới (3000ms, đổi từ 100ms ở phiên 2) — ưu
  tiên thấp, đã hỏi người dùng 2 lần (phiên 2, phiên 3) chưa được trả
  lời.

============================================================
GHI CHÚ KIẾN TRÚC BỔ SUNG (phát hiện mới ở phiên 3, quan trọng)
============================================================
- **Bất đối xứng tick giữa wake_steps và sleep_steps**: wake_steps
  (sx_sleep_manager.c) chạy NON-BLOCKING qua
  sx_sleep_manager_wake_process(mgr, delta_ms) được gọi từ
  test_sleep_poll() MỖI TICK của main loop — delta_ms thật, đều đặn.
  sleep_steps chạy BLOCKING qua sx_sleep_service.c's
  _run_steps_blocking() — vòng lặp riêng dùng sx_delay_ms(10), KHÔNG
  gọi test_sleep_poll()/modem_handle_poll() ở giữa. Bất kỳ sleep_step
  nào cần chờ một driver's async state machine tiến triển (không chỉ
  modem — cẩn thận nếu sau này thêm sleep_step khác cũng cần polling
  tương tự, ví dụ SPS30/BNO055 nếu chúng có state machine bất đồng bộ
  riêng) đều PHẢI tự tick driver đó bên trong is_done() của chính nó
  (theo pattern sx_gettick()-based đã áp dụng ở
  _modem_power_off_is_done()), KHÔNG được giả định driver tự tiến
  triển "ở đâu đó khác" như wake_steps làm.
- VDD_EXT (chân hay gọi là "1.8V") là OUTPUT của module A7677S (theo
  a7677s.md dòng ~1993: "Module provides three power outputs: VDD_EXT,
  VDD_AUX, and VDD_SDIO"), KHÔNG PHẢI input cấp nguồn vào module. Nó
  chỉ có điện khi module đã "powered on and initialization is
  completed" (dòng 1178). Dùng để đo/kiểm tra nhanh trạng thái
  power-on/off thật của module bằng đồng hồ đo, không cần đọc log.
- PWRKEY power-off (không phải AT+CPOF) ĐÃ ĐƯỢC XÁC NHẬN LÀ CẮT NGUỒN
  THẬT (không phải chỉ soft-shutdown giả) — khác với suy đoán sai lúc
  đầu phiên 3 (đã tự sửa sai ngay trong phiên khi người dùng chỉnh
  lại bằng phép đo tay). Đừng lặp lại nhầm lẫn này.
- RST pin (hard_reset()) THEO DATASHEET chỉ nên có tác dụng "reset
  logic" khi module đang có nguồn — nhưng TRÊN BOARD NÀY, quan sát
  thực tế cho thấy sau hard_reset(), module cũng mất nguồn luôn (1.8V
  mất) giống hệt power_off — có thể do mạch Q2 trên board giữ RESET đủ
  lâu khiến module tự shutdown thay vì chỉ reboot, HOẶC bản chất mạch
  RST bị nối/thiết kế khác thường trên board này. CHƯA XÁC ĐỊNH ĐƯỢC
  CHÍNH XÁC LÝ DO TẠI SAO (không có sơ đồ mạch RST pin trong Documents/,
  chỉ có a7677s.md là datasheet chung, không phải schematic riêng của
  WS_v1) — không cần điều tra sâu hơn về "tại sao", chỉ cần theo đúng
  hướng người dùng đã chỉ: SAU hard_reset() phải chạy lại toàn bộ chu
  trình PWRKEY power-on như thể module đang khởi động từ đầu.

============================================================
GHI CHÚ QUAN TRỌNG VỀ KIẾN TRÚC (giữ nguyên từ phiên 3, vẫn đúng)
============================================================
- main.c chạy APP THẬT qua test_sleep.c. XÁC NHẬN LẠI đầu phiên sau
  bằng đọc main.c/git log, đừng giả định.
- Kiến trúc sleep 3 tầng: tier 1 (sx_sleep.c, generic STOP-mode + RTC
  wakeup) -> tier 2 (sx_sleep_service.c, chạy mảng sx_sleep_step_t
  generic, có 2 chế độ: BLOCKING cho sleep_steps qua
  _run_steps_blocking(), NON-BLOCKING cho wake_steps qua
  sx_sleep_service_wake_process(), khác nhau hoàn toàn về cách tick —
  xem mục "GHI CHÚ KIẾN TRÚC BỔ SUNG" ở trên) -> tier 3
  (sx_sleep_manager.c, biết cụ thể GPS/modem/SPS30/pump/ZE12A/BNO055).
- modem_ops_t (modem_ops.h) là lớp trừu tượng driver-agnostic. MỌI lần
  thêm code mới gọi board.modem.ops->xxx() ở bất kỳ đâu, PHẢI kiểm tra
  caller khác + PHẢI kiểm tra ai/khi nào tick động cơ trạng thái của
  nó (poll()) — bài học kép từ phiên 2 (start() bị gọi chồng lệnh) và
  phiên 3 (is_done() không có ai tick nó trong blocking loop).
- Đã từng có file sx_sleep.c (tier 1) bị người dùng vô tình ghi đè
  toàn bộ bằng nội dung test_sleep.c (commit "build fail", phiên 3,
  đã tự khôi phục bằng commit "fix build") — nếu gặp lỗi linker lạ
  kiểu "multiple definition"/"undefined reference", nghi ngờ đầu tiên
  nên là kiểm tra xem có file nào bị ghi đè nhầm nội dung file khác
  không (dùng git log --all --oneline -- <path> để tìm bản gốc đúng
  trong lịch sử nếu cần khôi phục).

============================================================
GỢI Ý THỨ TỰ LÀM VIỆC PHIÊN 4
============================================================
1. Re-clone, đọc git log.
2. CHẠY NGAY lệnh xác nhận nhanh ở mục "TÌNH TRẠNG ĐẦU PHIÊN 4" để
   biết 2 fix của phiên 3 có persist trên GitHub hay chưa. Nếu chưa,
   ưu tiên số 1 là làm lại 2 fix đó (nội dung đã ghi chi tiết ở mục
   "NỘI DUNG FIX CỤ THỂ" — không cần suy luận lại từ đầu, nhưng vẫn
   PHẢI đọc code thật trước khi áp dụng, có thể người dùng đã tự sửa
   thêm gì).
3. Nếu 2 fix cũ đã có sẵn trên GitHub: bắt đầu luôn việc mới — sửa
   a7677s_hard_reset()/case A7677S_PWR_RST_PULSE để chạy lại đầy đủ
   chu trình PWRKEY power-on sau khi thả RST pin (xem chi tiết ở mục
   "Vấn đề ĐÃ PHÁT HIỆN, CHƯA FIX #1" bên trên).
4. Sau khi sửa xong #3, BẮT BUỘC hỏi người dùng cách đồng bộ code ra
   khỏi container TRƯỚC KHI người dùng build (nhắc nhở về việc git
   push không hoạt động trong container này) — xuất patch file qua
   present_files nếu cần, đừng để người dùng build nhầm code cũ như
   đã xảy ra 2 lần ở phiên 3.
5. Build, test, lấy log mới — kỳ vọng: sau SIM timeout/hard_reset(),
   modem phục hồi được (thấy lại "Module responsive, boot confirmed"
   thay vì TIMEOUT response: [NULL] lặp vô hạn).
6. Hỏi lại về log_debug -> log_info cho temp/humi + accel (câu hỏi
   treo từ phiên 3).
7. Nếu còn thời gian: điều tra timestamp sai "2087-00-00T..." — đọc
   format_timestamp()/sx_ex_rtc.c.
8. Ưu tiên thấp nhất: external flash chưa có sleep_step,
   ACCEL_APP_FILTER_ALPHA chưa điều chỉnh theo period mới.