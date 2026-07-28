HANDOFF — HARDWARE BRING-UP TEST, WS_v1 (STM32H563RIV6) — PHIÊN 2

Viết khi sắp hết token. Đọc kỹ handoff phiên trước (đã dán trong lịch sử
chat) trước khi làm gì — phiên này KẾ TIẾP từ đó, không lặp lại.

============================================================
QUY TẮC BẮT BUỘC (không đổi)
RE-CLONE đầu phiên: git clone https://github.com/logan123synaptix/WS_v1.git
KHÔNG tin log/mô tả cũ mà không tự đọc lại code thật. Container reset
giữa phiên.
Không sửa code âm thầm — trình bày nghi vấn → hỏi → chỉ sửa sau khi
có xác nhận. Đã áp dụng đúng suốt phiên này, tiếp tục giữ nguyên.
Comment code tiếng Anh, trao đổi tiếng Việt.
KHÔNG có compiler thật trong container — không build được. Người dùng
tự build + flash + gửi log qua chat.
Board test vật lý duy nhất: STM32H563RIV6, đã về thực tế.
Datasheet đầy đủ trong Documents/ — LUÔN tra cứu trước khi đoán
thông số (định dạng AT response thực tế THƯỜNG LỆCH so với datasheet,
xem lịch sử bug bên dưới — luôn ưu tiên log thật hơn tài liệu).
============================================================
BỐI CẢNH: ĐÃ CHUYỂN TỪ CODE TEST STANDALONE SANG APP THẬT

Phiên trước: Core/Src/main.c chạy code test thuần HAL (không qua
sx_uart/sx_board abstraction) để test từng module độc lập — xem
handoff phiên 1 (trong lịch sử chat) để biết chi tiết đầy đủ.

THAY ĐỔI QUAN TRỌNG CUỐI PHIÊN 1 → ĐẦU PHIÊN NÀY: người dùng đã tự
chuyển main.c sang chạy APP THẬT (SynaptiX_FDK/app/), cụ thể chạy
test_lte_mqtt.c (file test tích hợp LTE+MQTT dùng kiến trúc phân tầng
driver/service/app đầy đủ, KHÔNG PHẢI code test HAL thuần nữa). Commit
liên quan: aaa4e10 "test lte done". Xác nhận lại bằng git log/đọc
main.c thật đầu phiên, ĐỪNG giả định trạng thái này còn giữ nguyên.

============================================================
TIẾN TRÌNH PHIÊN NÀY — CÁC BUG ĐÃ TÌM RA VÀ FIX (CÓ BẰNG CHỨNG LOG THẬT)
Bug A — Thứ tự gọi at_term_init()/power_sim_on() sai (đã fix, thuộc

code test cũ, có thể không còn liên quan nếu đã chuyển hẳn sang app thật)
power_sim_on() gọi log_info() trước khi logger_init() chạy (nằm
trong at_term_init()) → log "Power On" bị mất. Fix: đảo thứ tự gọi.
Đây là bug ở Core/Src/main.c bản test cũ (console AT terminal qua
UART6) — CÓ THỂ ĐÃ BỊ GHI ĐÈ/KHÔNG CÒN LIÊN QUAN sau khi người dùng
chuyển sang chạy app thật, cần kiểm tra lại main.c hiện tại đầu phiên
sau.

Bug B — SPI1 W25Q128 JEDEC ID toàn 0x00 (đã fix bởi người dùng tự làm,

xác nhận qua log): thêm bước RELEASE_POWER_DOWN (0xAB) trước khi đọc
JEDEC ID. Xác nhận OK: [W25Q128] JEDEC ID: EF 40 18 (expect EF 40 18).

Bug C — Ăng-ten LTE lỏng/hỏng (đã fix bởi người dùng, PHẦN CỨNG không

phải code): trước khi đổi ăng-ten, AT+CSQ = 99,99 (không thấy sóng),
AT+CREG? = 0,0 (không đăng ký, không tìm mạng). Sau khi đổi ăng-ten
mới: AT+CSQ = 19,99 (tín hiệu khá), AT+CREG? = 0,1 (đăng ký home
network thành công), toàn bộ chuỗi CGATT/CGDCONT cũng OK. Board hiện
tại ĐANG DÙNG ăng-ten mới này, đã xác nhận vẫn gắn nguyên (người dùng
xác nhận rõ ràng khi được hỏi lại).

Bug D — BUG GỐC QUAN TRỌNG NHẤT, ĐÃ FIX, CÓ TÁC ĐỘNG LỚN:

modem_send_command() trong SynaptiX_FDK/components/modules/modem/modem.c
chỉ reset modem->buff_id = 0 (con trỏ ghi) nhưng KHÔNG XÓA nội dung cũ
trong modem->buff (mảng char[512] cố định, không bao giờ được
memset). Vì strstr(modem->buff, ...) tìm trên toàn bộ buffer (đọc tới
byte \0 đầu tiên), khi lệnh mới ghi dữ liệu NGẮN HƠN lệnh cũ, phần đuôi
buffer vẫn là rác của lệnh trước, khiến response bị "ghép lẫn" giữa
nhiều lệnh khác nhau. Xác nhận bằng bằng chứng log thật: lúc đang debug
CREG poll bị treo vô hạn, log debug tạm thời in ra
response=[AT+C1\n+CME1,"IP","m3-world"\nOK\nAT+CGA] — rõ ràng lẫn lộn
giữa response của CGDCONT cũ và echo/rác của CREG mới, KHÔNG BAO GIỜ có
+CREG: sạch.

FIX ĐÃ ÁP DỤNG (file SynaptiX_FDK/components/modules/modem/modem.c,
hàm modem_send_command()): thêm dòng
memset(modem->buff, 0, MODEM_RX_BUFFER_SIZE); ngay sau
modem->buff_id = 0;. Đây là fix ở TẦNG LÕI, ảnh hưởng MỌI lệnh AT đi
qua hàm này, không riêng CREG.

KẾT QUẢ SAU FIX (xác nhận bằng log thật): toàn bộ chuỗi network
attach (AT → CGDCONT → CGACT → CREG poll → COPS → lấy IP → sync time)
chạy hết, kết thúc bằng "Network attach complete, ready for MQTT".
Đây là tiến bộ rất lớn so với trước (trước đó CREG poll luôn timeout vô
hạn, lặp retry 1/3 mãi mãi).

Bug E — 6 pattern res_success cho lệnh MQTT bị lệch định dạng so với

response thực tế của modem (ĐÃ FIX, xác nhận MQTT connect + publish OK
sau fix):
Datasheet (Documents/a76xx_at_cmd.md) ghi URC dạng
+CMQTTSTART:0 (KHÔNG có dấu cách sau :), nhưng modem THẬT trên board
này luôn trả về CÓ dấu cách: +CMQTTSTART: 0. Code cũ hard-code pattern
theo datasheet (không dấu cách) → strstr() không bao giờ match →
timeout dù modem đã trả lời đúng thành công. Đây là CÙNG LOẠI bug với
"PWRKEY polarity" và "CSQ spacing" đã gặp ở phiên trước — luôn ưu tiên
log thật hơn datasheet khi có xung đột.

Đã sửa 6 chỗ trong SynaptiX_FDK/components/modules/a76xx/a7677s.c (tất
cả đều thêm 1 dấu cách sau :):

Dòng ~2324: CMQTTSTART: 0 (trước: CMQTTSTART:0)
Dòng ~1911: CMQTTCONNECT: 0,0
Dòng ~1974: CMQTTSTOP: 0
Dòng ~2095: CMQTTPUB: 0,0
Dòng ~2179: CMQTTSUB: 0,0
Dòng ~2350: CMQTTDISC: 0,0

Cũng cập nhật lại comment giải thích ở gần dòng 1505 (phía trên
cb_mqtt_start) để ghi chú rõ nguyên nhân + ngày phát hiện, cho phiên
sau không nghi ngờ lại từ đầu.

KẾT QUẢ SAU FIX: MQTT connected to tcp://broker.hivemq.com:1883,
publish OK liên tục nhiều lần (seq:1 đến seq:18 đều
MQTT publish OK / Publish result: OK).

============================================================
VẤN ĐỀ CHƯA GIẢI QUYẾT — ƯU TIÊN CAO NHẤT PHIÊN SAU
Bug F — CHƯA XÁC ĐỊNH ROOT CAUSE: publish thất bại 3 lần liên tiếp

(seq:19, 20, 21) rồi tự phục hồi ở seq:22, KHÔNG RÕ TẦN SUẤT XẢY RA
(câu hỏi cuối phiên gửi cho người dùng về tần suất CHƯA ĐƯỢC TRẢ LỜI —
người dùng yêu cầu viết handoff thay vì trả lời, PHẢI HỎI LẠI CÂU NÀY
đầu phiên sau trước khi làm gì tiếp).

Log lỗi thật (nguyên văn, người dùng đã xác nhận KHÔNG bị cắt/sửa khi
copy-paste — đáng tin cậy 100%):

[ERROR]MODEM : TIMEOUT response: [AT+CMQTTPUB=0,1,60,0
OK

]CMQTTPUB: 0,0
[ERROR]SX_MQTT : Publish failed (result=2)
[WARNING]USER_MQTT : Publish fail 1/3
...
[TX] seq:20
[ERROR]MODEM : TIMEOUT response: [AT+CMQTTTOPIC=0,22
]
[ERROR]A7677S : AT+CMQTTTOPIC prompt failed (res=2)
...
[TX] seq:21
[ERROR]MODEM : TIMEOUT response: [NULL]
[ERROR]A7677S : AT+CMQTTTOPIC prompt failed (res=2)
...
[ERROR]MODEM : TIMEOUT response: [
OK
]
[ERROR]A7677S : AT+CMQTTTOPIC prompt failed (res=2)
[WARNING]USER_MQTT : Publish fail 3/3
[ERROR]USER_MQTT : Max retry — reporting to sx_mqtt.c's recovery ladder
[TX] seq:22
[INFO]A7677S : MQTT publish OK  ← tự phục hồi

PHÁT HIỆN QUAN TRỌNG NHẤT: đã tra cứu Documents/a76xx_at_cmd.md
mục 18.2.12 AT+CMQTTPUB, xác nhận +CMQTTPUB:<client_index>,<err> với
err=0 LÀ THÀNH CÔNG theo đúng chuẩn. Log lỗi cho thấy response THẬT SỰ
LÀ CMQTTPUB: 0,0 (client_index=0, err=0 — TỨC LÀ PUBLISH ĐÃ THÀNH CÔNG
THẬT SỰ) — nhưng bị code coi là TIMEOUT/FAIL, vì response bị THIẾU
DẤU + Ở ĐẦU (]CMQTTPUB: 0,0 thay vì ]+CMQTTPUB: 0,0 — dấu ] là
ký tự đóng log message [...], KHÔNG PHẢI dữ liệu modem, chỉ để đánh
dấu hết chuỗi response trong log). Pattern res_success code đang tìm
là "\r\n+CMQTTPUB: 0,0\r\n" — THIẾU ĐÚNG 1 KÝ TỰ + ở đầu response
thật thì sẽ KHÔNG BAO GIỜ match, dẫn đến timeout dù dữ liệu gần như đầy
đủ và đúng.

GIẢ THUYẾT CHƯA XÁC NHẬN (cần điều tra thêm, KHÔNG được sửa mù):
mất 1 byte + ở đầu response — có thể do:
a) Lỗi UART thật (nhiễu điện/mất byte ngẫu nhiên tại đúng lúc đó) — HIẾM,
khó tái hiện bằng đọc code, cần xem tần suất xảy ra thực tế trước.
b) Race condition giữa ISR ghi sx_uart_rx_callback() và main-loop đọc
sx_uart_read()/modem_poll() — ĐÃ ĐỌC sx_uart.c, thấy dùng
cqueue (ring buffer) qua rxQueue, có mutex CHỈ KHI
SX_USE_OS == 1 — bare-metal (không RTOS) thì KHÔNG CÓ mutex bảo vệ.
CHƯA XÁC NHẬN SX_USE_OS đang bật hay tắt trong board này — ĐÂY LÀ
VIỆC ĐẦU TIÊN CẦN KIỂM TRA Ở PHIÊN SAU (grep -rn "SX_USE_OS" xem
define ở đâu, giá trị bao nhiêu).
c) Do lỗi log print bị cắt lúc paste — ĐÃ HỎI VÀ NGƯỜI DÙNG XÁC NHẬN RÕ
RÀNG "Đúng nguyên văn, không sửa gì" — LOẠI TRỪ khả năng này, KHÔNG
hỏi lại trừ khi có bằng chứng mới mạnh hơn.
d) Lỗi ở tầng cqueue (ring buffer implementation) tự nó có bug mất byte
trong điều kiện tải cao — CHƯA ĐỌC cqueue.c/cqueue.h trong phiên
này, cần đọc ở phiên sau nếu (b) không phải nguyên nhân.

CÂU HỎI CHƯA ĐƯỢC TRẢ LỜI, PHẢI HỎI LẠI ĐẦU TIÊN Ở PHIÊN SAU:
"Hiện tượng publish fail 3 lần rồi tự phục hồi (seq 19-21 fail, seq 22
OK) có hay xảy ra không, hay chỉ thấy 1 lần trong log này?" — câu trả
lời sẽ quyết định hướng đi:

Nếu HIẾM/1 lần: có thể chấp nhận được nhờ retry logic đã hoạt động
đúng (sx_user_mqtt.c's MQTT_PUBLISH_MAX_RETRY + dispatch_next()
tự phục hồi ở seq:22) — ưu tiên thấp, có thể để lại xử lý sau khi test
xong các module khác.
Nếu THƯỜNG XUYÊN/LẶP LẠI: cần điều tra sâu vào (b)/(d) ở trên trước
khi tiếp tục — đây sẽ là bug ảnh hưởng độ tin cậy truyền dữ liệu thật
sự nghiêm trọng, có thể mất dữ liệu telemetry thật khi deploy.
Lưu ý phụ (không phải bug, chỉ để tránh hiểu nhầm lại)

Hiện tượng "log [TX] seq:5 đến seq:19 in liên tục dồn cục, rồi 15
dòng Publish OK xuất hiện dồn sau đó" ĐÃ ĐƯỢC XÁC NHẬN KHÔNG PHẢI BUG —
đã đọc code sx_user_mqtt.c, xác nhận có hàng đợi (cqueue) + cờ
s_publishing chặn gửi chồng lệnh đúng cách (dispatch_next() chỉ lấy
item tiếp theo khi !s_publishing). Chỉ là hiện tượng hiển thị log dồn
cục do [TX] log ngay lúc enqueue, còn kết quả thật đến sau tuần tự
đúng thứ tự hàng đợi. KHÔNG CẦN điều tra lại vụ này.

============================================================
CÁC MODULE CHƯA TEST TRONG APP THẬT (kế thừa từ phiên 1, có thể đã test
riêng lẻ ở phiên 1 nhưng CHƯA test qua app thật/kiến trúc phân tầng)
UART2 GPS — code test HAL thuần đã OK ở phiên 1 (banner CASIC AT6558R,
NMEA sentences đều đặn), nhưng CHƯA test qua app thật/driver phân tầng
thật (SynaptiX_FDK/components/modules/gps/gps.c).
UART3 (RS485), UART4 (SPS30), UART5 (ZE12A) — hoàn toàn chưa test.
I2C1 + SHT3x, RTC RX8130CE, ADS1115 — chưa test riêng (I2C1 bus vật lý
đã xác nhận sống qua BNO055 ở phiên 1, nhưng logic/địa chỉ riêng của
từng chip này chưa test).
SPI1 + W25Q128 — OK ở tầng test HAL thuần (phiên 1). Log app thật phiên
này cũng thấy [INFO]W25Q128 : W25Q128 OK (16MB) — XÁC NHẬN driver
thật (sx_W25Q128.c) cũng hoạt động đúng qua sx_board_init(), không
chỉ code test HAL thuần. Có thể coi module này ĐÃ XONG HOÀN TOÀN.
I2C1 + BNO055 — tương tự, log app thật phiên này có [INFO]IMU : IMU init done — XÁC NHẬN driver thật cũng hoạt động qua sx_board_init().
Có thể coi ĐÃ XONG HOÀN TOÀN (dù chưa test đọc dữ liệu cảm biến thật,
chỉ mới init + CHIP_ID).
LittleFS/sx_storage — MỚI, chưa từng nhắc ở phiên 1, log phiên này
cho thấy đã có FS : mounted!/LittleFS formatted and mounted successfully/Storage init OK — CẦN XÁC NHẬN THÊM module này hoạt
động đúng đến mức nào (đọc/ghi file thật chưa test, mới chỉ mount).
============================================================
GỢI Ý THỨ TỰ LÀM VIỆC PHIÊN SAU
Re-clone, đọc lại git log, xem có commit mới nào từ người dùng
không (rất có thể có, xu hướng người dùng tự sửa/test liên tục giữa
các phiên).
HỎI LẠI câu hỏi tần suất Bug F ngay lập tức — quyết định độ ưu tiên.
Nếu cần điều tra Bug F: grep -rn "SX_USE_OS" xem có bật RTOS mutex
bảo vệ UART rxQueue hay không trước, rồi đọc cqueue.c/cqueue.h để
tìm hiểu ring buffer implementation có an toàn ISR/main-loop hay
không.
Nếu Bug F được xác nhận hiếm/chấp nhận được: chuyển sang test các
module còn lại theo đúng phương pháp đã dùng (đọc driver thật →
console AT tay nếu cần → đối chiếu datasheet → xác nhận bằng log thật
→ không tin mù).
Đặc biệt lưu ý: MỌI lần gặp lệch định dạng response giữa datasheet và
log thật (đã xảy ra 3 lần: PWRKEY polarity, CSQ spacing, CMQTT
spacing) — LUÔN ưu tiên log thật, tra cứu kỹ trước khi sửa, và kiểm
tra xem có pattern tương tự nào khác trong cùng file chưa bị phát
hiện không (dùng grep tìm mọi chỗ dùng cùng 1 kiểu lệnh, như đã làm
với 6 chỗ CMQTT).