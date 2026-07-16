HANDOFF PROMPT — MQTT RPC + Offline Queue (WS_v1)
(Viết bởi Claude phiên này, bàn giao cho phiên sau. Đây THAY THẾ hoàn toàn nội dung
"next_prompt.md"/handoff cũ từng nằm trong repo hoặc lịch sử chat — bản đó đã lỗi thời
ngay từ lúc viết ra. KHÔNG tin nội dung mô tả tiến độ trong bất kỳ handoff nào, kể cả
bản này — luôn tự `git fetch` + đọc code thật trước khi kết luận bất cứ điều gì.)

repo chính: https://github.com/logan123synaptix/WS_v1.git (nhánh main)
repo mẫu (tham khảo, KHÔNG port nguyên xi): https://github.com/logan123synaptix/WS_v0.git

============================================================
QUY TẮC BẮT BUỘC (không đổi so với các phiên trước)
============================================================
1. KHÔNG tin mô tả "tiến độ" trong handoff hay trong transcript chat cũ. Luôn
   `git fetch origin` + `git log --oneline -15` + đọc code thật (view/grep) trước khi
   kết luận bất cứ điều gì. Người dùng tự commit/push trực tiếp rất thường xuyên, và
   container của phiên trước có thể đã mất code chưa kịp push (đã xảy ra ít nhất 2 lần
   với chính dự án này — xem "BÀI HỌC" bên dưới).
2. Không sửa âm thầm. Phát hiện bug → trình bày rõ → hỏi lại người dùng → chỉ code
   sau khi có xác nhận, TRỪ những lỗi compile/logic rành rành (ví dụ macro sai chữ ký,
   enum không tồn tại) thì có thể sửa thẳng nhưng PHẢI báo cáo rõ đã sửa gì, ở đâu,
   vì sao, trong response — không được im lặng.
3. Toàn bộ comment code bằng tiếng Anh. Trao đổi với người dùng bằng tiếng Việt.
4. Không có compiler thật trong container (thiếu STM32 HAL headers) — chỉ đọc/grep,
   không đề nghị build. Người dùng tự build ở máy họ.
5. Khi tra cứu datasheet/tài liệu kỹ thuật, luôn đọc trực tiếp nguồn hoặc web_search
   chính thức trước khi khẳng định con số/hành vi kỹ thuật.
6. Trước khi tạo/sửa bất kỳ file nào, ĐỌC lại file thật bằng `view`.
7. Trước khi dùng bất kỳ API/macro nào (kể cả có sẵn từ lâu trong project), xác minh
   chữ ký thật bằng cách đọc định nghĩa — đã có tiền lệ 1 macro sai chữ ký nằm im
   trong code nhiều đời mà không ai gọi tới nó cả (xem "BÀI HỌC" mục 2).

============================================================
BÀI HỌC TỪ CÁC PHIÊN GẦN ĐÂY — ĐỌC KỸ TRƯỚC KHI LÀM GÌ
============================================================
1. MẤT CODE CHƯA PUSH (đã xảy ra ít nhất 2 lần với dự án này): 1 phiên viết xong
   network_config mở rộng + cli_commands, tưởng đã xong, nhưng chưa commit/push —
   toàn bộ mất khi container đóng. Một lần khác, phiên viết mqtt_rpc.c/.h + sửa build
   files nhưng QUÊN áp phần sửa app.c (include + wiring on_message/on_connected) —
   mqtt_rpc.c tồn tại, được build, nhưng KHÔNG BAO GIỜ ĐƯỢC GỌI, hoàn toàn chết.
   => BÀI HỌC: sau khi code xong bất kỳ tính năng nào, LUÔN nhắc người dùng commit +
   push NGAY, và nếu tính năng cần nhiều file phối hợp (module mới + wiring ở
   app.c/build files), phải tự kiểm tra bằng grep rằng MỌI mảnh đã thực sự nối với
   nhau (include có chưa, callback có gán chưa, có ai gọi hàm init chưa) trước khi
   báo "đã xong" — không chỉ kiểm tra file mới có tồn tại.

2. MACRO SAI CHỮ KÝ NẰM IM NHIỀU ĐỜI: file_io.h's `opendir(path)` expand ra
   `lfs_dir_open(CONFIG_LITTLEFS, path)` — thiếu hẳn tham số `lfs_dir_t *dir` mà
   `lfs_dir_open(lfs_t*, lfs_dir_t*, const char*)` cần. Bug này nằm im vì KHÔNG AI
   từng gọi macro đó (kể cả `fs_list_dir()` chính chủ trong cùng file cũng tự gọi
   thẳng `lfs_dir_open/_read/_close`, không qua macro của chính nó). Đã sửa ở phiên
   này (xem mục dưới) sau khi grep xác nhận an toàn tuyệt đối (0 caller nào khác).
   => BÀI HỌC: 1 API tồn tại lâu trong codebase KHÔNG có nghĩa là nó đúng — nếu không
   ai từng gọi nó, nó có thể chưa từng được test. Luôn đọc định nghĩa thật + verify
   chữ ký khớp cách gọi trước khi tin.

============================================================
TRẠNG THÁI GIT THẬT TẠI THỜI ĐIỂM VIẾT HANDOFF NÀY (tự xác minh lại)
============================================================
Remote WS_v1 ở commit 4dd9f12 ("add mqtt_rpc in make"), sau 50debcd ("mqtt rpc") —
2 commit này thêm module app/user/mqtt_rpc/ (mqtt_rpc.c/.h) + đăng ký vào
synaptix.mk/Makefile. ĐÚNG, đã build được (người dùng xác nhận), NHƯNG module này
CHƯA ĐƯỢC WIRING vào app.c — không có #include "mqtt_rpc.h", không có
mqtt_cfg.on_message/on_connected nào gán tới nó. Nó tồn tại, compile được, nhưng
chết hoàn toàn (không bao giờ subscribe, không bao giờ nhận RPC nào).

Phiên này đã viết bản vá đầy đủ cho việc này CỘNG THÊM offline-queue mới, gộp chung
trong 2 file sửa — nhưng CHƯA COMMIT/PUSH, chỉ nằm trong container tạm của phiên này,
SẼ MẤT khi phiên này kết thúc nếu người dùng không tự áp lại. Việc đầu tiên của phiên
sau là hỏi người dùng đã tự áp lại 2 sửa này chưa (nội dung đầy đủ liệt kê dưới đây,
có thể copy thẳng), hoặc tự áp giúp họ sau khi xác nhận.

--- Sửa 1: SynaptiX_FDK/services/filesystem/file_io.h ---
Bug thật: macro `opendir(path)` sai chữ ký, xem "BÀI HỌC" mục 2 ở trên. Sửa:

    Trước: #define opendir(path) lfs_dir_open(CONFIG_LITTLEFS, path)
    Sau:   #define opendir(dir, path) lfs_dir_open(CONFIG_LITTLEFS, dir, path)
           // dir must be a caller-owned lfs_dir_t*, e.g.:
           // lfs_dir_t d; if (opendir(&d, "/x") == LFS_ERR_OK) { ... closedir(&d); }

Đã verify bằng grep toàn project: KHÔNG có caller nào khác của opendir/readdir/
closedir trước sửa này — an toàn tuyệt đối, không ảnh hưởng code cũ.

--- Sửa 2: SynaptiX_FDK/app/app.c ---
Gồm 3 phần, tất cả trong 1 lượt sửa:

  (a) Wiring mqtt_rpc (phần bị thiếu từ commit 50debcd/4dd9f12):
      - Thêm #include "mqtt_rpc.h" và #include "sx_ex_storage.h" (thiếu từ trước,
        app.c chưa từng include header này trực tiếp dù cần cho sx_storage_write/
        read/delete/size/mkdir — network_config.h KHÔNG tự include nó, chỉ
        network_config.c mới include; app.c dùng trực tiếp các hàm này lần đầu ở
        offline-queue nên phải tự thêm include, nếu không sẽ lỗi implicit
        declaration lúc build) và #include "file_io.h" (cho opendir/readdir/
        closedir dùng ở offline-queue).
      - Trong app_init(), sau khi set mqtt_cfg.use_ssl:
            mqtt_cfg.on_message   = mqtt_rpc_on_message;
            mqtt_cfg.on_connected = mqtt_rpc_init;
        QUAN TRỌNG: dùng on_connected, KHÔNG gọi mqtt_rpc_init() trực tiếp ngay sau
        sx_user_mqtt_nontls_init()/_tls_init() — 2 hàm init đó chỉ khởi động kết nối
        bất đồng bộ (kết nối thật xảy ra dần qua sx_user_mqtt_poll() ở các tick sau),
        còn sx_user_mqtt_subscribe() (mqtt_rpc_init() gọi hàm này) là no-op im lặng
        (chỉ log_warn) nếu gọi trước khi client thực sự connected. Gọi qua
        on_connected đảm bảo subscribe đúng lúc, và tự re-subscribe mỗi lần reconnect
        sau khi mất kết nối.

  (b) Offline telemetry queue mới (module con nằm ngay trong app.c, không phải file
      riêng — 2 hàm static + 2 #define, đặt ngay trước build_telemetry_payload()):
      - #define OFFLINE_QUEUE_DIR "/queue", OFFLINE_QUEUE_MAX_FILES 20
      - sx_storage_mkdir(OFFLINE_QUEUE_DIR) gọi 1 lần trong app_init(), sau
        network_config_init().
      - offline_queue_save(payload): ghi payload ra file mới trong /queue (tên file
        telemetry_<tick_counter>.json, counter tăng dần mỗi lần gọi — KHÔNG dùng
        wall-clock vì SENDING chạy sớm trong chu kỳ, chưa chắc RTC đã sync). Nếu số
        file đã đạt OFFLINE_QUEUE_MAX_FILES, xóa file cũ nhất trước khi ghi file mới
        (quét thư mục lấy tên file đầu tiên gặp làm "oldest" — littlefs không đảm bảo
        thứ tự chính thức nhưng thực tế trả về theo thứ tự tạo).
      - offline_queue_resend_one(): quét /queue, lấy file đầu tiên tìm thấy, đọc nội
        dung qua sx_storage_size()+sx_storage_read(), publish lên
        "weatherstation/telemetry", xóa file nếu publish xong (dù publish thật ra
        không có ack đồng bộ ở tầng này — xem "VIỆC CẦN LÀM TIẾP THEO" mục 2 về giới
        hạn này). Gửi ĐÚNG 1 file mỗi lần gọi, không phải gửi hết 1 lượt — cùng tinh
        thần "one at a time" như WS_v0's push_last_telemetry_json().
      - Trong case APP_CYCLE_SENDING: nếu sx_user_mqtt_is_connected() true, gọi
        offline_queue_resend_one() TRƯỚC khi publish dữ liệu mới của chu kỳ này (để
        hàng chờ có cơ hội giảm dần mỗi chu kỳ, không chỉ khi không có gì mới); nếu
        false, gọi offline_queue_save(payload) thay vì chỉ log_warn rồi drop như
        trước đây.

  (c) Dùng lại macro opendir/readdir/closedir đã sửa ở Sửa 1 (lfs_dir_t local +
      opendir(&dir, path)/readdir(&dir, &info)/closedir(&dir)), KHÔNG gọi thẳng
      lfs_dir_open/_read/_close (bản nháp ban đầu trong phiên này có gọi thẳng để né
      bug macro, sau đó đã refactor lại dùng macro khi macro được sửa — nếu thấy code
      nào còn gọi lfs_dir_open trực tiếp trong app.c, đó là dấu hiệu bản vá KHÔNG
      được áp đầy đủ, cần đối chiếu lại).

VIỆC ĐẦU TIÊN: `git status`/`git diff` xem 2 sửa trên còn ở trạng thái uncommitted hay
đã được người dùng tự áp dụng/commit chưa. Nếu remote (`git log origin/main`) đã có
commit mới hơn 4dd9f12, đọc lại toàn bộ app.c/file_io.h bằng `view` để xem người dùng
đã tự sửa theo cách nào — CÓ THỂ KHÁC với bản vá trên, không giả định giống hệt.

============================================================
BỐI CẢNH: MQTT RPC — THIẾT KẾ (app/user/mqtt_rpc/mqtt_rpc.c/.h, đã có trên remote)
============================================================
Kênh cấu hình từ xa qua MQTT, song song với CLI qua USB CDC (app/user/shell_app/).
Ported ý tưởng từ WS_v0's app.c's RPCHandle() (method "setParams" + object "params"
chứa các cặp "-flag": value), nhưng KHÔNG dùng lớp thingsboard_client (WS_v1 hiện
KHÔNG dùng Thingsboard — xem app_init()'s comment về lý do, chưa có broker thật) —
subscribe/publish trực tiếp qua sx_user_mqtt (plain MQTT), dùng macro RPC_REQUEST_API/
RPC_RESPONSE_API có sẵn trong app_config.h (không phải macro riêng của Thingsboard,
chỉ là 2 chuỗi topic thường).

mqtt_rpc_init() (subscribe RPC_REQUEST_API) — gọi qua mqtt_cfg.on_connected, xem Sửa
2(a) ở trên.
mqtt_rpc_on_message() (parse JSON, gọi network_config_set_*(), publish phản hồi) —
gọi qua mqtt_cfg.on_message.

Flag set và đơn vị GIỐNG HỆT CLI's "settings -c" (shell_commands.c) — timing tính
bằng giây trong JSON, tự nhân 1000 thành ms khi gọi setter. Không áp dụng all-or-
nothing như CLI: field không nhận diện được bị bỏ qua và log, không abort toàn bộ
request (khác CLI, vì RPC caller là máy, "apply được phần nào báo phần đó" hữu ích
hơn all-or-nothing cho 1 script gọi tự động).

============================================================
BỐI CẢNH: OFFLINE QUEUE — GIỚI HẠN CẦN BIẾT (chưa hoàn thiện 100%)
============================================================
1. publish "thành công" hiện tại chỉ dựa vào sx_user_mqtt_is_connected() TRƯỚC khi
   gọi sx_user_mqtt_publish() — sx_user_mqtt_publish() bản thân KHÔNG trả về mã lỗi
   (kiểu void), và offline_queue_resend_one() xóa file NGAY sau khi gọi publish,
   không chờ ACK/xác nhận broker đã nhận. Nếu kết nối rớt đúng lúc giữa lúc gọi
   publish và lúc gói tin thực sự rời thiết bị, dữ liệu có thể mất mà không ai biết
   (file đã xóa, publish coi như "đã gửi" nhưng thực ra chưa tới nơi). Đây là giới
   hạn thật của kiến trúc sx_user_mqtt hiện tại (không có callback on_publish dùng
   được cho việc này dù sx_user_mqtt_cfg_t CÓ field on_publish — CHƯA kiểm tra kỹ
   liệu có thể dùng field này để làm resend logic chờ ACK thật hay không, đây là
   việc để lại cho phiên sau nếu người dùng muốn làm chặt hơn).
2. Giới hạn OFFLINE_QUEUE_MAX_FILES=20 là số tự chọn tạm thời, CHƯA hỏi ý kiến người
   dùng về con số phù hợp (phụ thuộc dung lượng flash thật, W25Q128 tổng dung lượng
   bao nhiêu, network_config.bin + timing config chiếm bao nhiêu, v.v. — CHƯA tính
   toán kỹ).
3. Tên file dùng 1 counter tăng dần trong RAM (s_offline_queue_seq), reset về 0 mỗi
   lần reset/mất điện — nếu reset xảy ra giữa lúc có file tồn đọng, file mới sau
   reset có thể trùng tên file cũ chưa gửi (ví dụ file cũ "telemetry_3.json" chưa gửi
   được, sau reset lại tạo "telemetry_3.json" mới, GHI ĐÈ mất dữ liệu cũ). CHƯA xử lý
   trường hợp này — cần hỏi người dùng có chấp nhận rủi ro nhỏ này không, hay cần đổi
   sang tên file khác (ví dụ dùng giá trị RTC nếu time_sync đã chạy xong, hoặc quét
   thư mục lấy số lớn nhất hiện có rồi +1 thay vì bắt đầu lại từ 0 mỗi lần boot).

============================================================
VIỆC CẦN LÀM TIẾP THEO — CẦN HỎI NGƯỜI DÙNG THỨ TỰ ƯU TIÊN
============================================================
1. (Nếu chưa) Áp lại/commit 2 sửa ở mục "TRẠNG THÁI GIT" phía trên — ĐÂY LÀ VIỆC BẮT
   BUỘC ĐẦU TIÊN, không thì mqtt_rpc vẫn chết và offline-queue chưa tồn tại.
2. Xem lại 3 giới hạn của offline-queue ở mục ngay trên — hỏi người dùng có cần xử lý
   ngay không, hay chấp nhận tạm thời và để dành sau.
3. Bootloader/OTA — CHƯA được yêu cầu bắt đầu, chỉ mới điều tra ở phiên trước đó (xem
   lịch sử: WS_v0 có bootloader UART-frame THẬT SỰ hoạt động ở thư mục bootloader/,
   và USB DFU qua TinyUSB CHỈ LÀ STUB rỗng chưa lập trình — khuyến nghị port hướng
   UART-frame nếu làm, KHÔNG lặp lại bug linker ORIGIN=0x8000000 trùng lặp giữa app
   và bootloader đã phát hiện ở WS_v0). WS_v1 hiện KHÔNG có gì cả ở mảng này.
4. USB MSC để sửa config file trực tiếp (thay thế nhập TLS cert dài qua CLI/RPC) —
   vẫn chỉ là comment "tương lai" trong network_config.h, chưa wiring gì.

============================================================
VIỆC ĐẦU TIÊN KHI TIẾP NHẬN — THEO ĐÚNG THỨ TỰ
============================================================
1. git fetch origin + git log --oneline -15 + git status/git diff — xác nhận trạng
   thái thật của app.c/file_io.h, so với mục "TRẠNG THÁI GIT" ở trên.
2. Nếu 2 sửa (macro opendir trong file_io.h, wiring mqtt_rpc + offline-queue trong
   app.c) chưa có trên remote lẫn chưa uncommitted trong container — hỏi người dùng
   có muốn Claude áp lại không (nội dung chính xác đã ghi đầy đủ ở trên, có thể copy
   thẳng).
3. Nếu đã có trên remote, đọc lại app.c/file_io.h bằng view để xác nhận khớp mô tả
   trên — nếu người dùng tự sửa khác đi, đọc kỹ code thật, không giả định.
4. Hỏi người dùng ưu tiên nào trong "VIỆC CẦN LÀM TIẾP THEO" (mục 2/3/4) trước khi
   code bất cứ gì mới.