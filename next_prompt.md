HANDOFF PROMPT — Refactor kiến trúc SLEEP 3 tầng (Weather Station V2, WS_v1)
(Viết bởi Claude phiên trước, bàn giao cho phiên sau. Đây là bổ sung riêng cho phần
refactor sleep — KHÔNG phải toàn bộ trạng thái dự án. Vẫn còn các handoff khác (SPS30,
v.v.) mô tả các phần không liên quan; đọc thêm nếu cần nhưng đừng lẫn lộn phạm vi.)

============================================================
QUY TẮC BẮT BUỘC — ĐỌC TRƯỚC KHI LÀM BẤT CỨ GÌ
============================================================
1. KHÔNG tin bất kỳ mô tả "tiến độ" nào trong bản này. Luôn tự
   `git fetch origin` + `git log --oneline -10` + đọc code thật (view/grep)
   trước khi kết luận bất cứ điều gì. Người dùng tự commit/push trực tiếp
   rất thường xuyên song song với Claude, không báo trước — đã xảy ra nhiều
   lần trong phiên vừa rồi (người dùng lấy đúng code Claude vừa viết trong
   container rồi tự commit/push trên máy họ, hoặc tự sửa độc lập).
2. Không sửa âm thầm. Phát hiện bug/lệch thiết kế → trình bày rõ → hỏi lại
   người dùng → chỉ code sau khi có xác nhận.
3. Toàn bộ comment code bằng tiếng Anh. Trao đổi với người dùng bằng tiếng
   Việt.
4. Không có compiler thật trong container Claude (thiếu STM32 HAL headers)
   — chỉ đọc/grep để rà soát. Người dùng build thật trên máy Windows của họ
   (STM32CubeCLT) và dán log lỗi lại khi cần.
5. Repo: https://github.com/logan123synaptix/WS_v1.git (nhánh main).
   Repo mẫu tham khảo (không phải nơi code chính): WS_v0.

============================================================
BỐI CẢNH — TẠI SAO ĐANG REFACTOR SLEEP
============================================================
Người dùng nhận thấy code sleep hiện tại (trước refactor) "rất lung tung":
1 file `components/peripherals/sleep/sx_sleep.c` (tầng thấp nhất, lẽ ra phải
generic — không biết board có gì) lại hard-code thẳng UART1 (LTE)/UART2
(GPS)/USB IRQ ngay trong hàm `_enter_stop()`. Nếu board khác không dùng
UART/USB kiểu này thì gần như phải viết lại từ đầu, không tái sử dụng được.

MỤC TIÊU: tách sleep thành đúng 3 tầng, theo pattern đã có sẵn trong dự án ở
chỗ khác (ví dụ `modem_ops.h` — vtable interface, driver cụ thể (a7677s.c)
implement, service/app chỉ gọi qua interface, không biết driver cụ thể là
gì):

  Tầng 1 — components/peripherals/sleep/sx_sleep.c/.h
    Chỉ biết: HAL STOP mode, RTC wakeup timer, suspend/resume tick.
    KHÔNG được biết: UART, USB, GPS, modem, MQTT, hay bất kỳ ngoại vi cụ
    thể nào tồn tại. Đây là phần DUY NHẤT project khác có thể tái sử dụng
    y nguyên không sửa gì.

  Tầng 2 — services/sx_sleep_service/ (TÊN MỚI, người dùng muốn đặt tên
    này — thay thế "sleepmanager" cũ)
    Generic wake/sleep ORCHESTRATION ENGINE. Biết "có một chuỗi bước phải
    hoàn thành trước khi được phép ngủ / sau khi thức dậy", nhưng KHÔNG
    biết bước đó là gì cụ thể (không biết GPS, không biết SIM/modem).
    Interface bằng callback/step-array, xem thiết kế đề xuất bên dưới.

  Tầng 3 — app/.../sx_sleep_manager.c/.h (chưa quyết định path chính xác,
    có thể để trong app/user/sleep/ hay tương tự — HỎI người dùng nếu chưa
    rõ, đừng tự đặt path rồi lỡ trùng với ý định khác)
    Nơi DUY NHẤT biết cụ thể GPS/modem/MQTT/`published` flag của project
    WS_v1 này là gì. Định nghĩa các step cụ thể (GPS wake, SIM wake, GPS
    sleep, modem sleep...) rồi đăng ký vào tầng 2.

============================================================
TRẠNG THÁI GIT THẬT TẠI THỜI ĐIỂM VIẾT HANDOFF NÀY (tự xác minh lại)
============================================================
Remote WS_v1 ở commit 4236cf2 ("sx_sleep.h modified"). Các commit liên quan
sleep, gần nhất trước:
  4236cf2 sx_sleep.h modified      <-- người dùng tự commit, LẤY ĐÚNG bản
                                        Claude phiên trước vừa viết trong
                                        container rồi push lên máy họ (đã
                                        verify bằng diff nội dung, giống
                                        100% — không phải người dùng tự
                                        viết khác đi)
  fde6257 refactor sxsleep         <-- cùng tình huống như trên
  aa1542b modify sleep             <-- người dùng tự sửa TRƯỚC KHI Claude
                                        phiên trước bắt đầu refactor (đổi
                                        tên biến sx_sleep_manager ->
                                        sx_sleep_service trong 2 dòng
                                        comment của sx_board.h/modem_ops.h
                                        — CHỈ ĐỔI TÊN TRONG COMMENT, chưa
                                        thực sự tạo file/thư mục
                                        sx_sleep_service nào; cũng sửa
                                        sx_sleep.c/.h thêm field
                                        uarts_to_abort — bước đệm trước khi
                                        refactor triệt để hơn ở fde6257/
                                        4236cf2)
  4fddff7 add sx_uart_abort in sx_uart
  dceb340 add pump port & pump pin & fix pump_off

============================================================
ĐÃ HOÀN THÀNH — TẦNG 1 (components/peripherals/sleep) — XONG, ĐÃ PUSH
============================================================
File: SynaptiX_FDK/components/peripherals/sleep/sx_sleep.h
File: SynaptiX_FDK/components/peripherals/sleep/sx_sleep.c

Đã tự xác minh bằng diff: nội dung trên remote (commit 4236cf2/fde6257) và
nội dung Claude phiên trước viết trong container GIỐNG NHAU 100%. Không cần
làm lại tầng này trừ khi phát hiện bug mới.

THAY ĐỔI CHÍNH:
- Xóa field `sx_uart_t **uarts_to_abort` + `uarts_to_abort_count` khỏi
  struct `sx_sleep` — đây là board-cụ-thể, không thuộc tầng 1.
- Xóa `#include "sx_uart.h"` và `#include "usart.h"` khỏi sx_sleep.c/.h —
  tầng 1 giờ KHÔNG BIẾT UART là gì nữa, đúng như mục tiêu.
- Thêm 2 hook generic vào struct `sx_sleep`:
    typedef void (*sx_sleep_hook_t)(void *hook_ctx);
    sx_sleep_hook_t pre_stop_hook;   // gọi ngay trước khi enter STOP
    sx_sleep_hook_t post_wake_hook;  // gọi ngay sau khi wake + clock restore
    void *hook_ctx;                  // truyền nguyên vẹn, tầng 1 không đọc
- `sx_sleep_init()` đổi chữ ký, nhận thêm pre_stop_hook/post_wake_hook/
  hook_ctx thay vì uarts_to_abort/uarts_to_abort_count:
    void sx_sleep_init(sx_sleep_t *mgr, sx_sleep_ops_t *ops, void *pDriver,
                        sx_sleep_hook_t pre_stop_hook,
                        sx_sleep_hook_t post_wake_hook, void *hook_ctx);
- `_enter_stop()` trong sx_sleep.c co lại chỉ còn: reset wake_reason, gọi
  pre_stop_hook, clear pending SysTick (`SCB->ICSR`), suspend/resume tick,
  gọi HAL_PWR_EnterSTOPMode, restore SystemClock_Config(), gọi
  post_wake_hook. KHÔNG còn dòng nào biết USART1/2/USB tồn tại.

ĐÃ WIRING VÀO BOARD LAYER (sx_board.c/.h) — CŨNG XONG, ĐÃ PUSH:
- Thêm field `sx_sleep_t sleep;` vào `Board_t` (sx_board.h) — TRƯỚC ĐÂY
  KHÔNG CÓ INSTANCE NÀO CỦA sx_sleep_t Ở ĐÂU CẢ trong toàn bộ codebase, đây
  là lần đầu tiên nó thực sự được khởi tạo.
- Thêm 2 hàm static trong sx_board.c — đây là nơi kiến thức "board này có
  UART1(LTE)/UART2(GPS)/USB cần quiesce trước khi ngủ" thực sự nằm:
    static void board_sleep_pre_stop_hook(void *hook_ctx)
    {
        sx_lte_uart_abort();  // đã có sẵn từ trước
        sx_gps_uart_abort();  // đã có sẵn từ trước
        __HAL_UART_CLEAR_IT(&huart1, UART_CLEAR_OREF|UART_CLEAR_NEF|UART_CLEAR_PEF);
        __HAL_UART_CLEAR_IT(&huart2, UART_CLEAR_OREF|UART_CLEAR_NEF|UART_CLEAR_PEF);
        HAL_NVIC_ClearPendingIRQ(USART1_IRQn);
        HAL_NVIC_ClearPendingIRQ(USART2_IRQn);
        HAL_NVIC_DisableIRQ(USB_DRD_FS_IRQn);
        HAL_NVIC_ClearPendingIRQ(USB_DRD_FS_IRQn);
    }
    static void board_sleep_post_wake_hook(void *hook_ctx)
    {
        HAL_NVIC_EnableIRQ(USB_DRD_FS_IRQn);
    }
- Gọi ở cuối `sx_board_init()`:
    sx_sleep_init(&board.sleep, &sx_sleep_ops, NULL,
                  board_sleep_pre_stop_hook, board_sleep_post_wake_hook, NULL);
- Nội dung 2 hook = ĐÚNG NGUYÊN VẸN đoạn code đã bị xóa khỏi sx_sleep.c cũ,
  chỉ chuyển vị trí — không đổi logic, không đổi hành vi runtime.

ĐÃ KIỂM TRA:
- grep toàn repo: không còn tham chiếu `uarts_to_abort` ở bất kỳ đâu.
- grep toàn repo: chỉ có ĐÚNG 1 lời gọi `sx_sleep_init()` (trong
  sx_board.c), khớp chữ ký mới — không có lời gọi nào khác dùng chữ ký cũ
  có thể gây lỗi compile.
- CHƯA build thật được (không có compiler trong container) — người dùng
  cần tự build trên máy họ để xác nhận tầng 1 không lỗi. Việc phát hiện lỗi
  build (nếu có) và fix thuộc về việc kế tiếp trong danh sách dưới.

============================================================
CHƯA LÀM — TẦNG 2 VÀ TẦNG 3 (CHƯA VIẾT 1 DÒNG CODE NÀO)
============================================================
Đã BÀN THIẾT KẾ với người dùng (người dùng đồng ý shape, nhưng CHƯA CODE):

--- TẦNG 2: services/sx_sleep_service/ (tên file/path CHÍNH XÁC CHƯA CHỐT,
    hỏi người dùng — có thể muốn sx_sleep_service.c/.h, hoặc theo cấu trúc
    thư mục hiện có kiểu services/sleepmanager/ đổi tên thành
    services/sleep_service/) ---

Thiết kế đã thống nhất (thảo luận trong hội thoại, CHƯA CODE):

    typedef struct {
        void    (*start)(void *ctx);       // kick off step (non-blocking)
        uint8_t (*is_done)(void *ctx);      // poll: xong chưa?
        void    *ctx;                       // opaque, service không đụng vào
        const char *name;                   // chỉ để log
    } sx_sleep_step_t;

    typedef struct {
        sx_sleep_t      *sleep;             // tầng 1
        sx_sleep_step_t *wake_steps;         // mảng do caller sở hữu, theo thứ tự
        uint8_t          wake_step_count;
        uint8_t          current_step;
        uint32_t         step_elapsed_ms;
        uint32_t         step_timeout_ms;    // per-step timeout, 0 = không giới hạn
    } sx_sleep_service_t;

    void    sx_sleep_service_init(sx_sleep_service_t *svc, sx_sleep_t *sleep,
                                   sx_sleep_step_t *wake_steps, uint8_t count,
                                   uint32_t step_timeout_ms);
    void    sx_sleep_service_enter_sleep(sx_sleep_service_t *svc, uint32_t sleep_sec);
    void    sx_sleep_service_wake_process(sx_sleep_service_t *svc, uint32_t delta_ms);
    uint8_t sx_sleep_service_is_wake_done(sx_sleep_service_t *svc);
    void    sx_sleep_service_reset_wake(sx_sleep_service_t *svc);

Nguyên tắc quan trọng ĐÃ THỐNG NHẤT, đừng làm khác:
- `wake_process()` CHỈ làm việc generic: gọi `start()` của step hiện tại
  đúng 1 lần khi mới vào step, poll `is_done()` mỗi tick, hết bước thì
  `current_step++`, quá `step_timeout_ms` thì cũng coi là xong (tránh treo
  vô hạn) — KHÔNG được biết bên trong step nào có GPS/SIM gì cả.
- `enter_sleep()` (đi ngủ) CHỈ còn: gọi `sx_sleep_set_rtc_wake()` +
  `sx_sleep_enter_stop()` (tầng 1). Phần tắt-nguồn-GPS/modem-trước-khi-ngủ
  hiện đang hard-code trong `sx_sleep_manager_enter()` cũ (xem bên dưới)
  PHẢI chuyển thành "sleep steps" (tương tự wake steps), do tầng 3 đăng ký
  — KHÔNG được để lại hard-code trong service.
- CHƯA CHỐT: sleep-steps (trước khi ngủ) và wake-steps (sau khi thức) có
  dùng chung 1 kiểu mảng `sx_sleep_step_t` hay tách 2 mảng riêng — hỏi
  người dùng hoặc tự đề xuất rồi xác nhận trước khi code, đừng tự quyết.

--- TẦNG 3: app/.../sx_sleep_manager.c/.h (path CHƯA CHỐT — hỏi người
    dùng) ---

Đây là nơi giữ nguyên logic cụ thể của WS_v1 hiện đang nằm trong
`services/sleepmanager/sx_sleep_manager.c/.h` (CHƯA ĐỘNG VÀO, file này vẫn y
nguyên bản cũ), gồm:
- Wake step machine cụ thể: SX_WAKE_STEP_GPS_ON_FIRST -> GPS_WAIT ->
  UART_RESUME -> SIM_WAKE -> DONE. Cần viết lại thành các
  `sx_sleep_step_t` cụ thể (gps_wake_step, sim_wake_step, ...), MỖI STEP
  implement đúng cặp hàm `start()`/`is_done()` mà tầng 2 yêu cầu, rồi đăng
  ký mảng đó vào `sx_sleep_service_init()`.
- `sx_sleep_manager_enter()` hiện tại hard-code gọi
  `gps_power_off()`/`modem->ops->power_off_start()` trước khi ngủ — chuyển
  thành sleep-steps tương tự.
- `mgr->published` flag: hiện CHỈ được set về 0 (ở init/reset), CHƯA CÓ AI
  ĐỌC HOẶC SET NÓ THÀNH 1 Ở ĐÂU CẢ (đã grep xác nhận, KHÔNG suy đoán tiếp
  nếu chưa tự grep lại). Ý định người dùng (xác nhận trong hội thoại):
  "publish đầy đủ thông tin thì sẽ set trigger để device vào sleep mode" —
  tức là: `sx_user_mqtt_cfg_t.on_publish` callback (đã có sẵn trong
  sx_user_mqtt.h, kiểu `void (*)(int success)`) là nơi tầng 3 cần set
  published=1 (hoặc tương đương) khi thấy publish thành công, rồi từ đó
  trigger enter_sleep(). CHƯA VIẾT CODE PHẦN NÀY.

--- app.c (services/app orchestration) ---
`SynaptiX_FDK/app/app.c` hiện TẠI THỜI ĐIỂM VIẾT HANDOFF NÀY chỉ có:
    void app_init(void){ log_info(TAG, "APP initializing ...."); }
    void app_process(uint32_t delta_ms){ }
Đây là NGƯỜI DÙNG tự viết (không phải Claude), thay thế bản stub 5-hàm mà
Claude phiên trước từng viết tạm để qua lỗi link — người dùng đã xác nhận
muốn XÓA HẲN 4 hàm `app_notify_usb_connected/app_request_sleep/
app_sync_gps_log_to_disk/app_mode_full_pw` khỏi app.h vì "project hiện tại
không dùng trigger qua USB như cũ mà sẽ sleep và wakeup theo kiểu khác"
(dựa trên publish MQTT xong thì sleep, thời gian sleep đã define trong
app_config.h là SLEEP_TIME_MS = 5*60*1000). NHƯNG TỰ GREP LẠI app.h TRƯỚC
KHI TIN — tại thời điểm viết handoff này việc xóa 4 khai báo đó trong app.h
CHƯA ĐƯỢC XÁC NHẬN LÀ ĐÃ LÀM (câu trả lời "xóa hết" của người dùng đến
NGAY TRƯỚC KHI Claude phiên trước bị người dùng ngắt để đổi hướng sang bàn
kiến trúc lại từ đầu — có thể người dùng đã tự làm hoặc chưa, PHẢI TỰ KIỂM
TRA, đừng giả định).

app_process() cuối cùng cần gọi tầng 2/tầng 3 mỗi tick — CHƯA VIẾT, đây là
việc sau khi tầng 2/3 xong.

============================================================
VIỆC ĐẦU TIÊN KHI TIẾP NHẬN — THEO ĐÚNG THỨ TỰ
============================================================
1. git fetch origin && git log --oneline -10, so sánh với commit 4236cf2
   nêu trên — nếu khác, đọc lại code thật trước khi tin bất kỳ điều gì
   trong handoff này. Đặc biệt kiểm tra app.h/app.c và
   services/sleepmanager/ xem người dùng đã tự sửa gì thêm chưa.
2. Đọc lại toàn bộ sx_sleep.h/.c (tầng 1, đã xong) và sx_board.c/.h phần
   liên quan sleep bằng view/grep, xác nhận khớp mô tả ở trên.
3. Đọc lại services/sleepmanager/sx_sleep_manager.c/.h (chưa refactor,
   logic cụ thể GPS/SIM cần giữ lại tinh thần khi viết tầng 3 mới).
4. Hỏi người dùng xác nhận path/tên file chính xác cho tầng 2
   (services/sx_sleep_service/... ?) và tầng 3 (app/.../sx_sleep_manager...?)
   trước khi tạo file mới — ĐỪNG tự đặt path rồi tạo file, người dùng có
   thể có ý định cấu trúc thư mục khác.
5. Sau khi thống nhất path, viết tầng 2 trước (generic, không phụ thuộc
   GPS/modem/MQTT gì cả) — dễ review độc lập vì không cần biết chi tiết
   project. Rồi mới viết tầng 3 (di chuyển + refactor logic từ
   sx_sleep_manager.c cũ sang, dùng step callback thay vì enum cứng).
6. Sau khi tầng 3 xong, quay lại wiring app.c: app_init() tạo instance
   sx_sleep_service_t + mảng steps + gọi sx_sleep_service_init(); trong
   sx_user_mqtt on_publish callback set trạng thái "sẵn sàng ngủ"; 
   app_process() poll trạng thái đó, khi true thì gọi
   sx_sleep_service_enter_sleep(SLEEP_TIME_MS/1000).
7. Nhắc người dùng tự build trên máy họ sau mỗi bước lớn (không có compiler
   trong container Claude) — dán log lỗi lại nếu có, chẩn đoán bằng đọc
   code thật, đừng đoán nguyên nhân khi chưa xem dòng lỗi cụ thể.