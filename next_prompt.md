HANDOFF PROMPT — SPS30 Dust Sensor Driver Integration (Weather Station V2, WS_v1)
(Viết bởi Claude phiên trước, bàn giao cho phiên sau. Đây là bổ sung cho handoff chính
"Weather Station V2 + Modem Abstraction Layer" đã có — KHÔNG thay thế nó, chỉ tập trung
riêng vào phần SPS30 vừa làm.)

repo hiện tại: https://github.com/logan123synaptix/WS_v1.git
repo mẫu: https://github.com/logan123synaptix/WS_v0.git

QUY TẮC BẮT BUỘC — GIỐNG HANDOFF CHÍNH, NHẮC LẠI VÌ QUAN TRỌNG
1. KHÔNG tin bất kỳ mô tả "tiến độ" nào trong bản này. Luôn tự git fetch + git log
   --oneline -10 + đọc code thật (view/grep) trước khi kết luận bất cứ điều gì.
   Người dùng tự commit/push trực tiếp rất thường xuyên, không qua Claude.
2. Không sửa âm thầm. Phát hiện bug/lệch thiết kế → trình bày rõ → hỏi lại người dùng
   → chỉ code sau khi có xác nhận.
3. present_files có thể lỗi hiển thị ở UI dù file trên đĩa hợp lệ — CHỈ dùng
   present_files (không dán nội dung file vào chat) trừ khi người dùng yêu cầu khác.
   Lưu ý: người dùng đã đổi ý giữa các lượt về việc này — kiểm tra yêu cầu gần nhất
   của người dùng trong hội thoại trước khi quyết định.
4. Toàn bộ comment code bằng tiếng Anh. Trao đổi với người dùng bằng tiếng Việt.
5. Không có compiler thật trong container (thiếu STM32 HAL headers) — chỉ đọc/grep để
   rà soát, không đề nghị build từng bước nhỏ.
6. Người dùng đã nói rõ: chỉ build khi đã hoàn thiện các components/services.

BỐI CẢNH: TẠI SAO SPS30 LẠI ĐI THEO KIẾN TRÚC "NHIỀU FILE SENSIRION"
Ban đầu Claude phiên trước đề xuất gộp SPS30 thành 1 file duy nhất (sps30.c/.h) theo
đúng pattern các module khác trong dự án (gps.c/.h, bno055.c/.h...). NGƯỜI DÙNG ĐÃ TỪ
CHỐI hướng này ở giữa chừng, nói rõ "tôi thích kiến trúc của họ [Sensirion]" — tức là
giữ nguyên kiến trúc phân lớp gốc của Sensirion (SHDLC layer tách khỏi UART HAL layer
tách khỏi command layer), thay vì gộp lại. Quyết định này đã chốt, KHÔNG đề xuất gộp
lại thành 1 file nữa trừ khi người dùng chủ động đổi ý lần nữa.

Vì giữ kiến trúc Sensirion gốc, dự án hiện có RẤT NHIỀU file nhỏ trong
components/modules/sps30/, mỗi file đảm nhiệm đúng 1 lớp trách nhiệm hẹp — đây là CHỦ
Ý, không phải sai sót cần "dọn gọn".

NGUỒN THAM KHẢO
- Toàn bộ code Sensirion gốc được copy nguyên vẹn (không sửa 1 dòng nào, chỉ khác xuống
  dòng cuối file) từ repo tham khảo WS_v0 (https://github.com/logan123synaptix/WS_v0.git),
  cụ thể tại SynaptiX/board/SPS30/.
- Datasheet SPS30 đầy đủ đã có sẵn tại Documents/SPS30_dust_sensor (1).md trong repo
  WS_v1 — ĐỌC FILE NÀY trước khi đưa ra bất kỳ quyết định kỹ thuật nào về giao thức
  SHDLC, Device Status Register, hay Measurement Output Format.

============================================================
TRẠNG THÁI GIT THẬT TẠI THỜI ĐIỂM VIẾT HANDOFF NÀY (tự xác minh lại)
============================================================
Remote WS_v1 ở commit 9e20e53 ("modify sensirion_uart_hal"). Các commit liên quan SPS30,
gần nhất trước:
  9e20e53 modify sensirion_uart_hal
  aa32bdb duplicate sensirion_streaming.h/.c & sensirion_streaming_shdlc.h/.c
  3411bac need edit & modify sps30 sensirion
  ca36268 edit sps30
  e5b4623 sps30

============================================================
LỖI QUAN TRỌNG NHẤT CẦN SỬA NGAY — SAI TÊN FILE, CHẶN COMPILE
============================================================
Claude phiên trước (khi viết sensirion_uart_hal.c mới) đã tạo 2 file mới:
  - sensirion_uart_hal.c (đúng, không có vấn đề)
  - sensirion_uart_portdescriptor.h (tên Claude ĐỊNH đặt)

Nhưng khi người dùng/công cụ push code lên remote, file thứ 2 đã bị lưu SAI TÊN thành:
  SynaptiX_FDK/components/modules/sps30/sensirion_uart_descriptor.h   (THIẾU CHỮ "port")

Trong khi đó sensirion_uart_hal.h (dòng 36, copy nguyên bản Sensirion, KHÔNG sửa) có:
  #include "sensirion_uart_portdescriptor.h"

=> Nếu build đúng như trạng thái hiện tại trên remote, sẽ lỗi compile ngay ở dòng include
   này ("file not found"), vì tên file trên đĩa và tên trong #include không khớp.

VIỆC ĐẦU TIÊN CẦN LÀM: xác nhận lại bằng git fetch + ls xem tên file thật trên remote lúc
tiếp nhận handoff là gì (có thể người dùng đã tự đổi tên giữa chừng, ĐỪNG giả định vẫn
còn sai như mô tả ở đây). Nếu vẫn sai, đổi tên file thành sensirion_uart_portdescriptor.h
(hoặc sửa dòng #include trong sensirion_uart_hal.h cho khớp tên hiện có — nhưng ưu tiên
đổi tên file vì "portdescriptor" là tên gốc chuẩn Sensirion, ít gây nhầm lẫn hơn về sau).

============================================================
DANH SÁCH ĐẦY ĐỦ FILE TRONG components/modules/sps30/ VÀ VAI TRÒ TỪNG FILE
============================================================
(Tất cả đường dẫn tương đối so với SynaptiX_FDK/components/modules/sps30/)

--- COPY NGUYÊN VẸN TỪ WS_v0, KHÔNG SỬA GÌ — ĐÃ XÁC NHẬN BẰNG diff, CHỈ KHÁC NEWLINE CUỐI FILE ---

1. sensirion_common.c/.h (119/196 dòng)
   Hàm chuyển đổi byte-order thuần túy: bytes_to_uint16/32_t, bytes_to_float (IEEE754
   union trick), bytes_to_int16/32_t, và chiều ngược lại (xxx_to_bytes). Không phụ
   thuộc UART/RTOS gì cả — dùng chung cho mọi thứ cần encode/decode SHDLC payload.

2. sensirion_config.h (87 dòng)
   Macro cấu hình build chung của SDK Sensirion (bật/tắt tính năng). Chưa rà soát kỹ
   xem có macro nào cần tùy biến riêng cho STM32 không — NÊN ĐỌC LẠI file này nếu gặp
   lỗi compile lạ liên quan macro thiếu định nghĩa.

3. sensirion_shdlc.c/.h (385/278 dòng)
   API "buffer-based" gốc của Sensirion (sensirion_shdlc_tx/rx/xcv,
   sensirion_shdlc_begin_frame/add_xxx_to_frame/finish_frame/tx_frame,
   sensirion_shdlc_rx_inplace). ĐANG LÀ CODE CHẾT — sps30_uart.c hiện KHÔNG gọi bất kỳ
   hàm nào ở đây, nó dùng API streaming (xem file 4-5 dưới) thay thế. Không gây lỗi
   compile (chỉ compile ra rồi không dùng), nhưng là code thừa. Chưa hỏi người dùng có
   muốn xóa hẳn cho gọn hay giữ lại làm tài liệu tham khảo — ĐỪNG tự ý xóa, hỏi trước.

4. sensirion_streaming.c/.h (85/142 dòng)
   Định nghĩa struct sensirion_streaming_state và interface con trỏ hàm
   stream.read/stream.write mà lớp streaming_shdlc (file 5) dùng.

5. sensirion_streaming_shdlc.c/.h (206/95 dòng)
   API "streaming" của Sensirion — ĐÂY LÀ API THẬT SỰ ĐANG ĐƯỢC sps30_uart.c SỬ DỤNG:
   sensirion_shdlc_begin_stream, sensirion_shdlc_write_request,
   sensirion_shdlc_read_response. Khác bản buffer-based (file 3) ở chỗ ghi/đọc từng
   byte trực tiếp qua stream.read/write thay vì buffer toàn bộ trước.
   QUAN TRỌNG: sensirion_shdlc_read_response() (trong .c) có VÒNG LẶP CHỜ RIÊNG bên
   trong nó (biến "retries", gọi sensirion_uart_hal_sleep_usec() lặp lại) để chờ đủ
   dữ liệu tới trước khi timeout. Đây là lý do sensirion_uart_hal_rx() (file 8) được
   phép non-blocking (đọc 1 lần rồi trả về ngay) — lớp này phía trên đã tự lo việc chờ
   rồi, không cần lớp HAL bên dưới chờ hộ.

6. sps30_uart.c/.h (334/325 dòng)
   Lớp command cụ thể của SPS30 — mọi hàm public mà code nghiệp vụ cấp cao sẽ gọi:
     sps30_wake_up_sequence()          -- gọi wake_up_communication() rồi wake_up()
     sps30_wake_up_communication()     -- gửi 1 SHDLC frame với cmd=0xFF, KHÔNG đọc
                                          response (cố ý) — đây là mẹo "đánh thức UART
                                          vật lý", không phải lệnh SHDLC chuẩn theo
                                          datasheet, nhưng đóng gói thành SHDLC frame
                                          hoàn chỉnh (có checksum/stuffing) thay vì gửi
                                          1 byte thô đơn giản. Cách này VẪN HỢP LỆ vì
                                          bản thân có tín hiệu UART là đủ đánh thức
                                          receiver — module không cần hiểu nội dung.
     sps30_wake_up()                   -- lệnh SHDLC Wake-up thật, cmd=0x11 theo
                                          datasheet, có đọc response
     sps30_start_measurement(fmt)      -- cmd=0x00, tham số sps30_output_format (dùng
                                          SPS30_OUTPUT_FORMAT_FLOAT — chưa xác nhận tên
                                          hằng số chính xác trong sps30_uart.h, GREP LẠI
                                          trước khi dùng, đừng đoán tên)
     sps30_stop_measurement()          -- cmd=0x01
     sps30_read_measurement_values_uint16(...) / _float(...) -- cmd=0x03, 2 biến thể
                                          định dạng response (20 byte uint16 hay 40 byte
                                          float) — NÊN DÙNG BẢN _float, khớp
                                          SPS30_OUTPUT_FORMAT_FLOAT đã chọn khi start
     sps30_sleep()                     -- cmd=0x10
     sps30_start_fan_cleaning()        -- cmd=0x56
     sps30_read_auto_cleaning_interval() / write_...()  -- cmd=0x80 (Read/Write)
     sps30_read_product_type() / read_serial_number() / read_version()
                                        -- cmd=0xD0/thông tin thiết bị, chưa dùng tới
     sps30_read_device_status_register(clear, *reg, *reserved)  -- cmd=0xD2, ĐÂY LÀ
                                          CƠ CHẾ CHÍNH THỨC để phát hiện lỗi SPEED/
                                          LASER/FAN — xem phần "HEURISTIC SAI CẦN
                                          TRÁNH" bên dưới, quan trọng
     sps30_device_reset()               -- cmd=0xD3

--- FILE MỚI, CLAUDE PHIÊN TRƯỚC TỰ VIẾT (KHÔNG PHẢI COPY TỪ SENSIRION) ---

7. sensirion_uart_portdescriptor.h (31 dòng, XEM LẠI VẤN ĐỀ TÊN FILE Ở TRÊN)
   Thay thế bản gốc Sensirion (định nghĩa UartDescr = const char* path kiểu Linux
   "/dev/ttyUSB0", vô nghĩa trên STM32 bare-metal). Bản mới: typedef sx_uart_t *UartDescr
   — con trỏ tới instance sx_uart_t đã được sx_board.c cấu hình sẵn (ví dụ
   bsp_uart[UART_DUST]).

8. sensirion_uart_hal.c (80 dòng) — ĐÃ XÁC NHẬN NỘI DUNG ĐÚNG, KHỚP BẢN CLAUDE VIẾT
   Implement 5 hàm mà sensirion_uart_hal.h khai báo:
     sensirion_uart_hal_init(port)     -- lưu port (sx_uart_t*) vào biến static s_port
     sensirion_uart_hal_free()         -- reset s_port = NULL
     sensirion_uart_hal_tx(len, data)  -- gọi sx_uart_write() (BLOCKING, vì
                                          sx_uart_write() bên dưới gọi thẳng
                                          HAL_UART_Transmit với timeout HAL nội bộ
                                          1000ms — đã CHẤP NHẬN block ngắn này, vì SPS30
                                          chỉ gửi lệnh vài chục byte, không phải trong
                                          vòng lặp gấp)
     sensirion_uart_hal_rx(max_len, data) -- gọi sx_uart_read(..., timeout=0) — CHỈ 1
                                          lần đọc KHÔNG chờ, trả về ngay số byte đang
                                          có (có thể 0). KHÔNG được tự thêm vòng lặp
                                          chờ ở đây — lớp sensirion_streaming_shdlc.c
                                          (file 5) đã tự retry/chờ rồi, viết thêm ở đây
                                          sẽ gây double-wait hoặc hành vi khó đoán
     sensirion_uart_hal_sleep_usec(us) -- quy đổi ra ms (làm tròn LÊN, (us+999)/1000),
                                          gọi sx_delay_ms(). Lý do: dự án chỉ có
                                          sx_delay_ms (không có delay us thật). Đã xác
                                          nhận qua đọc lại code tham khảo WS_v0/board.h:
                                          NGAY CẢ bsp_delay_us() ở bản gốc WS_v0 cũng
                                          THỰC CHẤT gọi HAL_Delay(x) (delay theo x
                                          MILI giây, không phải micro giây — đây là
                                          lỗi/thiếu chính xác có sẵn trong code tham
                                          khảo gốc). Cách quy đổi hiện tại trong
                                          WS_v1 CHÍNH XÁC HƠN bản gốc WS_v0 (tỷ lệ
                                          đúng 1:1000 thay vì coi 1us = 1ms). KHÔNG
                                          CẦN SỬA GÌ THÊM Ở ĐIỂM NÀY.

============================================================
FILE ĐÃ XÓA (KHÔNG DÙNG NỮA) — KHÔNG TỰ Ý TẠO LẠI
============================================================
sps30.h (từng tồn tại 1 thời gian ngắn) — đây là bản Claude phiên trước viết theo kiến
trúc GỘP 1 FILE (tương tự gps.h), TRƯỚC KHI người dùng nói rõ muốn giữ kiến trúc
Sensirion phân lớp. Đã bị Claude xóa sau khi người dùng chốt hướng đi. ĐỪNG viết lại
file này trừ khi người dùng chủ động yêu cầu quay về kiến trúc gộp.

============================================================
HEURISTIC SAI CẦN TRÁNH — ĐÃ QUYẾT ĐỊNH, KHÔNG PORT SANG WS_v1
============================================================
Trong WS_v0/SynaptiX/apps/sps30/sps30_app.c (CHƯA copy sang WS_v1, và THEO QUYẾT ĐỊNH
ĐÃ CHỐT thì KHÔNG NÊN copy y nguyên phần này khi viết lớp nghiệp vụ cấp cao sau này) có
đoạn: nếu mc_pm10p0 == mc_pm2p5 (giá trị PM10 và PM2.5 đọc được trùng nhau) thì coi là
"dữ liệu không hợp lệ", tự ý cộng thêm 1.5 vào mc_10p0 rồi đo lại.

Đã tự đọc kỹ Documents/SPS30_dust_sensor (1).md để xác minh: ĐÂY KHÔNG PHẢI CƠ CHẾ
CHÍNH THỨC của Sensirion. Datasheet định nghĩa lỗi qua Device Status Register (bit
SPEED=fan speed sai / LASER=dòng laser bất thường / FAN=fan hỏng cơ khí, đọc qua lệnh
0xD2) và Error-Flag bit trong state byte của mọi response frame — không có chỗ nào nói
PM10==PM2.5 là dấu hiệu lỗi. Về vật lý, 2 giá trị này trùng nhau hoàn toàn có thể là kết
quả ĐÚNG (không khí rất sạch, không có hạt >2.5um). Khi viết lớp nghiệp vụ cấp cao
(business logic: chu trình wake→start→đo định kỳ→stop→sleep) cho WS_v1 sau này, KHÔNG
port heuristic này. Thay vào đó dùng sps30_read_device_status_register() định kỳ và
kiểm tra 3 bit Error thật (SPS30_STATUS_BIT_SPEED/LASER/FAN — nếu đặt tên hằng số này,
grep xác nhận tên thật trước khi dùng, đừng đoán).

============================================================
VIỆC CHƯA LÀM — CÒN LẠI ĐỂ HOÀN THIỆN SPS30
============================================================
1. SỬA LỖI TÊN FILE Ở TRÊN (ưu tiên cao nhất, chặn compile).
2. Lớp nghiệp vụ cấp cao (business logic / state machine): hiện sps30_uart.c chỉ có
   các hàm command đơn lẻ (mỗi hàm tự start+stop 1 giao dịch SHDLC hoàn chỉnh, blocking
   ngắn hạn ~20-50ms mỗi lần gọi theo datasheet). CHƯA CÓ vòng đời quản lý tổng thể:
   khi nào gọi wake_up_sequence(), khi nào start_measurement(), polling định kỳ
   read_measurement_values_float() mỗi bao lâu, khi nào kiểm tra
   read_device_status_register(), khi nào sleep(). Cần hỏi người dùng muốn đặt lớp này
   ở đâu (thêm file mới kiểu sps30_app.c riêng, hay tích hợp thẳng vào sx_board.c/app
   layer nào đó) trước khi code — ĐÂY LÀ QUYẾT ĐỊNH KIẾN TRÚC CẦN NGƯỜI DÙNG XÁC NHẬN,
   không tự ý chọn.
3. Wiring vào sx_board.c/.h: struct Board_t CHƯA có field nào cho SPS30 (đã grep xác
   nhận ở phiên trước, có thể đã đổi — TỰ GREP LẠI TRƯỚC KHI TIN). UART_DUST (UART4,
   115200 baud) đã được cấu hình sẵn trong sx_board.c's uart_config[] nhưng chưa gán
   instance driver nào vào đó.
4. sensirion_shdlc.c/.h (bản buffer-based, mục 3 ở trên) đang là code chết — hỏi người
   dùng có muốn xóa không, đừng tự ý xóa.

============================================================
VIỆC KHÔNG LIÊN QUAN SPS30 NHƯNG CŨNG ĐANG DANG DỞ TRONG CÙNG NHÁNH GIT
============================================================
- ads1115.c/.h (components/modules/ads1115/) — thư viện mới (tác giả Ahmet Batuhan
  Günaltay, GPLv3, khác hẳn bản trong WS_v0), .c GẦN NHƯ RỖNG (chỉ có #include, chưa
  implement ADS1115_Init()/ADS1115_readSingleEnded()). Có 1 lỗi nhỏ vô hại: macro
  ADS1115_DEVICE_ADDR bị #define 2 LẦN LIÊN TIẾP (dòng 44 và 47 trong ads1115.h), cùng
  giá trị nên không gây lỗi compile, nhưng nên dọn khi có dịp.
- Documents/ads1115.pdf mới được thêm — datasheet ADS1115 thật, dùng để đối chiếu khi
  implement.
- Vai trò thật của ADS1115 trong hệ thống CHƯA được người dùng xác nhận rõ (có thể liên
  quan đọc Vout analog (0-2V) của module ZE12A — xem ảnh schematic người dùng đã gửi
  trong hội thoại trước, có 3 module ZE12A (SR1/SR2/SR3) qua mux TMUX4052 dùng chung
  UART5, nhưng Vout của chúng KHÔNG thấy nối đi đâu trong ảnh schematic đó — cần hỏi
  người dùng có board/kết nối nào khác nối 3 Vout này vào ADS1115 không).
- ZE12A: chỉ có ze12a.h (47 dòng khai báo), ze12a.c HOÀN TOÀN RỖNG (0 dòng). Đã xác
  nhận qua ảnh schematic người dùng gửi: 3 module ZE12A vật lý (SR1/SR2/SR3), dùng
  chung UART5 qua IC mux TMUX4052 (U3), chọn kênh bằng 2 GPIO UART5_S0/UART5_S1 (=A0/A1
  của mux), EN nối GND cố định (luôn enable). Bảng chọn kênh đã xác nhận từ datasheet
  TI thật (Table 8-2, TMUX4052 Truth Table):
    A1=0 A0=0 -> SR1 (ZE12A TX1/RX1)
    A1=0 A0=1 -> SR2 (ZE12A TX2/RX2)
    A1=1 A0=0 -> SR3 (ZE12A TX3/RX3)
    A1=1 A0=1 -> không dùng (dư)
  CHƯA viết bất kỳ dòng code nào cho ze12a.c. Cần hỏi người dùng: loại khí cụ thể mỗi
  module SR1/SR2/SR3 đang đo (CO/SO2/NO2/O3/H2S...) trước khi code, vì có thể ảnh hưởng
  đơn vị/công thức chuyển đổi ppm theo datasheet ze12a-electrochemical-module-manual-
  v1_0.md đã có sẵn trong Documents/.
- SHT3x: driver components/modules/sht3x/sht3x.c/.h ĐÃ HOÀN THIỆN, đã đối chiếu đúng cả
  datasheet lẫn code tham khảo WS_v0, KHÔNG CẦN SỬA GÌ. Chỉ còn thiếu wiring vào
  sx_board.c/.h (chưa có instance nào trong Board_t).

============================================================
VIỆC ĐẦU TIÊN KHI TIẾP NHẬN — THEO ĐÚNG THỨ TỰ
============================================================
1. git fetch origin, git log --oneline -10, so sánh với commit 9e20e53 nêu trên — nếu
   khác, đọc lại code thật trước khi tin bất kỳ điều gì trong handoff này.
2. Kiểm tra ngay vấn đề tên file sensirion_uart_descriptor.h vs
   sensirion_uart_portdescriptor.h — xem trạng thái thật trên remote lúc này, báo cáo
   và xin xác nhận hướng sửa (đổi tên file hay sửa #include) trước khi động tay.
3. Đọc lại toàn bộ 8 nhóm file đã liệt kê ở trên bằng view/grep, đừng chỉ tin mô tả này.
4. Hỏi người dùng về hướng đi cho lớp nghiệp vụ cấp cao SPS30 (mục "VIỆC CHƯA LÀM" #2)
   trước khi code bất kỳ dòng nào mới.