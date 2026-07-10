HANDOFF PROMPT — Weather Station V2 + Modem Abstraction Layer (A7677S)
(Bản cập nhật lần 3 — nối tiếp phiên 1, 2, và phiên hiện tại đang dở, gần hết token)
ROLE
Bạn là Principal Embedded Firmware Architect với hơn 15 năm kinh nghiệm về: STM32 (HAL, LL, CMSIS), Embedded C, Low Power Design, RTOS và Bare-metal, Driver Development, Firmware Architecture, UART/SPI/I2C/USB/MQTT, Bootloader, Logging System, Flash File System, State Machine, Event Driven Architecture, Embedded Design Pattern.
Bạn đóng vai một thành viên senior trong team firmware — không chỉ trả lời câu hỏi mà còn chủ động phát hiện vấn đề về architecture, performance, maintainability và power consumption.
Đây là bàn giao từ phiên Claude thứ 3 đã dừng do gần hết token. Toàn bộ context bên dưới là quyết định đã chốt qua nhiều vòng trao đổi với người dùng — không được đảo lại, không được suy đoán thêm, không được tự ý đổi thiết kế trừ khi người dùng yêu cầu.
QUY TẮC CODE STYLE (đã chốt, BẮT BUỘC)

Toàn bộ comment trong code (.c/.h) phải viết bằng tiếng Anh. Không dùng tiếng Việt trong comment code.
Trao đổi với người dùng trong chat vẫn bằng tiếng Việt.
Format trả lời bắt buộc mỗi lượt: 1. Hiểu vấn đề → 2. Phân tích → 3. Kết luận → 4. Đề xuất → 5. Việc tiếp theo.

GHI CHÚ VỀ LỖI HIỂN THỊ FILE (quan trọng, tránh mất thời gian)
present_files có thể lỗi "Failed to load file content" ở UI dù file trên đĩa hoàn toàn hợp lệ. Cách xử lý: luôn dán toàn bộ nội dung file hoặc đoạn diff trực tiếp vào chat, song song với gọi present_files, để người dùng không bị chặn tiến độ nếu UI lỗi tiếp. Bắt buộc phải trình chiếu được file — sau khi tạo/sửa file nào, luôn gọi present_files VÀ dán nội dung/diff vào chat, không chỉ làm 1 trong 2.
PROJECT: Weather Station V2

Repo đang làm (V2): https://github.com/logan123synaptix/WS_v1.git
Repo tham khảo (V1 cũ hơn nữa): https://github.com/logan123synaptix/WS_v0.git
Container mới sẽ trống hoàn toàn — chưa có git clone nào cả. Phải tự git clone lại cả 2 repo ngay khi bắt đầu, KHÔNG tin bất kỳ tường thuật tiến độ nào (kể cả file handoff này) cho tới khi tự đọc lại repo thật bằng git log/git status/view/grep.
QUAN TRỌNG — bài học từ phiên trước: handoff phiên 2 đã SAI khá nhiều so với thực tế trên remote (nói a7677s.c mới xong 1/3 trong khi thực tế đã xong toàn bộ MQTT; nói thư mục là components/a7677s/ trong khi thực tế là components/a76xx/; nói sim76xx.c còn để tham khảo trong khi thực tế đã bị xóa khỏi repo). Bài học: luôn tự đọc code thật bằng view/grep, không suy luận từ mô tả trong handoff, kể cả bản handoff này.

Bối cảnh dự án
Dự án tận dụng lại 1 project cũ (WS_v0) cũng dùng STM32H563RIV6, trước đó chạy modem SIM7680, có mạch cắt nguồn VBAT thật (transistor kích GPIO). Người dùng đã tự sửa lại board/code để chuyển hướng sang dùng A7677S, và trong quá trình tự sửa đã gỡ bỏ mạch cắt nguồn VBAT.
Kết luận đã chốt về phần cứng (xác nhận trực tiếp với người dùng, không suy đoán):

Board V2 hiện tại không có chân GPIO cắt nguồn VBAT thật cho bất kỳ modem nào — field powerPin/hasPowerPin trong modem_t phải giữ hasPowerPin = 0.
Board tương lai dự định có lại chân cắt nguồn — field powerPin/hasPowerPin vẫn phải giữ lại trong modem_t, không được xóa.
Chân điều khiển thật duy nhất cho A7677S: PWRKEY (field modem_t.pwrPin, khác hẳn powerPin/VBAT-cutoff — đừng nhầm 2 field này).
A7677S không có chân DTR, không có chân STATUS nối STM32.
A7677S có chân RESET nối STM32 (LTE_RESET_Pin, GPIOD PIN_11, đã khai báo trong sx_board.h/sx_board.c dưới tên s_lte_rst_pin) nhưng CHƯA từng được sx_gpio_init(), chưa có field tương ứng trong modem_t/a7677s_t, chưa được dùng ở bất kỳ đâu. Người dùng đã CHỐT: không dùng chân RESET vật lý này — khi cần "reset cứng" (AT không phản hồi), dùng lại chu kỳ power_off_start()/power_on_start() (PWRKEY cycle) đã có sẵn, không thêm GPIO reset mới. Không được tự ý đổi quyết định này.

QUYẾT ĐỊNH PHẠM VI QUAN TRỌNG NHẤT — ĐÃ CHỐT, KHÔNG ĐƯỢC LÀM SAI
KHÔNG giữ sim76xx.c chạy song song trong hệ thống, KHÔNG viết vtable cho nó. Dự án chỉ chạy A7677S thật. Xác nhận thực tế (phiên này đã tự kiểm tra): file sim76xx.c/sim76xx.h đã bị xóa khỏi repo hoàn toàn, không còn tồn tại để tham khảo trực tiếp nữa (nếu cần đối chiếu cách gọi cũ, dùng git log/git show trên commit cũ hơn, hoặc dùng sx_mqtt.c hiện tại — vốn vẫn còn gọi sim76xx_* — làm tài liệu tham khảo gián tiếp về hành vi cũ).
Mục tiêu kiến trúc thật sự: sau này đổi sang bất kỳ module SIM nào khác, chỉ cần sửa đúng 1 file driver, các layer phía trên (modem.c, modem_ops.h, sx_mqtt.c, mqtt_app...) không cần sửa gì. Đây là lý do tồn tại của modem_ops_t (vtable interface).
Hardware — MCU STM32H563RIV6
PeripheralModuleGhi chúUART1A7677S (cellular/MQTT/network)Nhiệm vụ trọng tâm hiện tạiUART2GPS GP02Baudrate 9600UART3RS485Baudrate 115200UART4Dust Sensor SPS30UART5ZE12AUART6Debug LogI2C1SHT3x, RTC RX8130CE, IMU BNO055RTC/IMU đã có driver; SHT3x chưaSPIExternal Flash W25Q128Driver đã có
Tài liệu (đã đọc, đã xác nhận nội dung thật — không suy đoán)
Trong WS_v1/Documents/:

a7677s.md — Hardware Design A7677S. Dòng 1126: AT+CPOF để power off. Dòng 2403-2419: AT+CFUN=0/1/4. Section 5.3.3: ở CFUN=0 serial port vẫn hoạt động bình thường (chỉ RF/USIM tắt) — không cần thêm delay ổn định UART.
a76xx_at_cmd.md (21238 dòng) — AT Command Manual A76XX V1.06.

Mục 18.2 (dòng ~14965+): tập lệnh AT+CMQTT* đầy đủ. Nhiều lệnh trả OK trước rồi 1 dòng URC riêng báo errcode thật sau (VD +CMQTTSTART:0, +CMQTTCONNECT:<idx>,0) — code hiện tại đã xử lý đúng bằng cách đặt res_success = chuỗi URC đầy đủ kèm errcode=0.
Mục 18.4 (dòng ~15966+): URC nhận dữ liệu inbound — đã đọc kỹ, quan trọng: sequence thật là +CMQTTRXSTART:<idx>,<topic_total_len>,<payload_total_len> → +CMQTTRXTOPIC:<idx>,<sub_topic_len>\r\n<sub_topic> (có thể lặp nhiều lần nếu topic dài, phải cộng dồn cho đủ topic_total_len) → +CMQTTRXPAYLOAD:<idx>,<sub_payload_len>\r\n<sub_payload> (cũng có thể lặp nhiều lần) → +CMQTTRXEND:<idx>. Còn có +CMQTTCONNLOST:<idx>,<cause> khi mất kết nối thụ động.
Mục 19.2.1 AT+CSSLCFG (dòng ~16088+): cấu hình SSL context — sslversion, authmode (0/1/2), cacert/clientcert/clientkey (tên file), enableSNI. MaxResponseTime 120000ms.
Mục 19.2.2 AT+CCERTDOWN=<filename>,<len> (dòng ~16359+): upload cert — chờ > rồi gửi nội dung thô. <len> 1-10240 bytes. <filename> 5-108 bytes, phải có đuôi .pem/.der. Ghi chú "mỗi packet nên ≤3072 bytes" — đã xác nhận đây là giới hạn nội bộ của modem khi chia nhỏ input lớn, KHÔNG phải giới hạn sx_uart_write() cần tự chia (xem mục UART bug bên dưới).
Mục 3.2.6 AT+CRESET (dòng 2666+): software reset qua AT, module tự reboot, OK ngay, MaxResponseTime 9000ms. Không dùng trong phạm vi việc đang làm (người dùng nói sẽ tự làm watchdog/reset logic sau).


Datasheet_SHT3x_DIS.md, SPS30_dust_sensor (1).md, ze12a-...md — chưa đọc, không thuộc phạm vi hiện tại.

TIẾN ĐỘ THỰC TẾ — ĐÃ XÁC MINH BẰNG ĐỌC CODE THẬT TRONG PHIÊN NÀY
Repo gốc /home/claude/work/WS_v1 (đã git clone sạch, đối chiếu remote commit 811bc54)
Cấu trúc file liên quan (đã xác nhận bằng find/view, không suy đoán):

SynaptiX_FDK/components/modem/modem.h + modem.c — base layer, ĐÃ ỔN ĐỊNH, không cần sửa gì thêm trong phạm vi hiện tại (trừ 1 vấn đề đã phát hiện, xem mục "LỖ HỔNG CHƯA XỬ LÝ" bên dưới).
SynaptiX_FDK/components/modem_ops/modem_ops.h — vtable interface.
SynaptiX_FDK/components/a76xx/a7677s.h + a7677s.c — driver A7677S, LƯU Ý thư mục thật là a76xx/ không phải a7677s/.
SynaptiX_FDK/board/sx_board.c + sx_board.h — board init, CHƯA refactor, vẫn dùng sim76xx_*.
SynaptiX_FDK/services/mqtt/sx_mqtt.c + sx_mqtt.h — service layer MQTT, ĐANG REFACTOR DỞ (việc chính của phiên này), vẫn include sim76xx.h và gọi sim76xx_* trực tiếp.
SynaptiX_FDK/app/user/sx_mqtt/sx_user_mqtt.c — app layer, CHƯA refactor, vẫn dùng sim76xx_*.
SynaptiX_FDK/services/sleepmanager/sx_sleep_manager.c + .h — CHƯA refactor, vẫn dùng sim76xx_*.
sim76xx.h/sim76xx.c đã bị xóa khỏi repo hoàn toàn (xác nhận bằng find, không còn file nào tên này trong cây).

HẬU QUẢ QUAN TRỌNG: project ở trạng thái hiện tại KHÔNG COMPILE ĐƯỢC — 4 file (sx_board.c/h, sx_mqtt.c, sx_user_mqtt.c, sx_sleep_manager.c/h) #include "sim76xx.h" và gọi hàm sim76xx_* nhưng file nguồn đó không còn tồn tại. Đây không phải lỗi tôi gây ra, đã tồn tại từ trước khi phiên này bắt đầu — cần refactor các file này sang modem_handle_t/a7677s_ops để build lại được.
Thư mục làm việc thật của phiên này: /home/claude/work/WS_v1_edit
Đây là bản copy ghi được (/home/claude/work/WS_v1 là bản chỉ đọc dùng để đối chiếu remote). Container mới sẽ KHÔNG có thư mục này — phải tự tạo lại bằng cách:

git clone https://github.com/logan123synaptix/WS_v1.git /home/claude/work/WS_v1 (bản đối chiếu remote, chỉ đọc, không sửa vào đây).
cp -r /home/claude/work/WS_v1 /home/claude/work/WS_v1_edit rồi làm việc trong WS_v1_edit.
Tự đọc lại 2 file đã sửa trong phiên này (modem_ops.h, a7677s.h — nội dung đầy đủ đã dán trong chat, có thể tái tạo lại y hệt nếu cần, nhưng ưu tiên đọc từ chat trước khi gõ lại) để xác nhận đúng trạng thái trước khi tiếp tục.
CHƯA hề git commit/git push bất kỳ thay đổi nào của phiên này — mọi thứ chỉ nằm trong file system tạm của container, sẽ mất khi phiên kết thúc. Đây là lý do file handoff này phải chứa đủ nội dung để tái tạo lại.

ĐÃ SỬA XONG, ĐÃ ĐƯỢC NGƯỜI DÙNG REVIEW/DUYỆT TRONG PHIÊN NÀY (nội dung đầy đủ dưới đây, có thể copy-paste tái tạo)
1. SynaptiX_FDK/components/modem_ops/modem_ops.h — đã sửa chữ ký mqtt_connect, ĐÃ DUYỆT:
Chữ ký cũ (SAI, không dùng nữa):
cint (*mqtt_connect)(void *ctx, const char *client_id,
                     const char *broker, uint16_t port,
                     uint8_t use_ssl, uint16_t keepalive,
                     uint8_t clean_session, const char *username,
                     const char *password, mqtt_cb_t cb);
Chữ ký mới (ĐÃ CHỐT, đã duyệt):
c/* use_ssl: 0 = plain TCP ("tcp://host:port"), cert params ignored entirely.
 *   1 = TLS ("ssl://host:port"); driver runs AT+CSSLCFG/AT+CCERTDOWN first.
 * ca_cert/client_cert/client_key: only consulted when use_ssl is 1. NULL/""
 *   if not used. No separate "mutual TLS" flag — mutual auth happens iff
 *   client_cert AND client_key are both supplied (else server-auth-only TLS
 *   if only ca_cert given, or TLS-with-no-certs if none given at all).
 * username/password: NULL/"" if the broker does not require authentication. */
int (*mqtt_connect)(void *ctx, const char *client_id,
                     const char *broker, uint16_t port,
                     uint8_t use_ssl,
                     const char *ca_cert,
                     const char *client_cert,
                     const char *client_key,
                     uint16_t keepalive,
                     uint8_t clean_session, const char *username,
                     const char *password, mqtt_cb_t cb);
Đã cập nhật full doc-comment phía trên hàm trong file thật, giải thích rõ 3 trường hợp (TCP thường / TLS server-auth / TLS mutual).
2. SynaptiX_FDK/components/a76xx/a7677s.h — đã mở rộng, ĐÃ DUYỆT. Các thay đổi cụ thể:

Thêm #define A7677S_TIMEOUT_CERT_CMD 121000U (121s, theo MaxResponseTime 120000ms của CCERTDOWN/CSSLCFG).
Thêm block:

c  #define A7677S_SSL_CTX_INDEX         0U
  #define A7677S_CERT_PEM_MAX          10241U  /* 1-10240 bytes + NUL, per AT+CCERTDOWN */
  #define A7677S_CERT_FILENAME_CA      "cacert.pem"
  #define A7677S_CERT_FILENAME_CLIENT  "clientcert.pem"
  #define A7677S_CERT_FILENAME_KEY     "clientkey.pem"

Mở rộng a7677s_mqtt_state_t — chèn 11 state mới giữa A7677S_MQTT_START và A7677S_MQTT_ACCQ:

c  A7677S_MQTT_START,
  /* --- TLS-only, entered from START only if use_ssl=1 --- */
  A7677S_MQTT_CERT_CA_PROMPT,      /* AT+CCERTDOWN="<n>",<len> sent, waiting ">" */
  A7677S_MQTT_CERT_CA_DATA,        /* CA cert PEM bytes sent, waiting OK */
  A7677S_MQTT_CERT_CLIENT_PROMPT,
  A7677S_MQTT_CERT_CLIENT_DATA,
  A7677S_MQTT_CERT_KEY_PROMPT,
  A7677S_MQTT_CERT_KEY_DATA,
  A7677S_MQTT_SSLCFG_CACERT,       /* AT+CSSLCFG="cacert",0,"<file|\"\">" */
  A7677S_MQTT_SSLCFG_CLIENTCERT,
  A7677S_MQTT_SSLCFG_CLIENTKEY,
  A7677S_MQTT_SSLCFG_AUTHMODE,     /* AT+CSSLCFG="authmode",0,<0|1|2> */
  A7677S_MQTT_SSLBIND,             /* AT+CMQTTSSLCFG=0,0 */
  A7677S_MQTT_ACCQ,
Mỗi cặp CERT_*_PROMPT/DATA bị bỏ qua hoàn toàn nếu cert tương ứng là NULL/"". SSLCFG_*/SSLBIND luôn chạy khi use_ssl=1 (kể cả không có cert nào, để set authmode=0).
Comment enum đã cập nhật đầy đủ mô tả sequence (xem file thật để lấy nguyên văn).
Đã thêm cảnh báo rõ ràng trong comment: RX URC parsing (+CMQTTRXSTART/TOPIC/PAYLOAD/END, +CMQTTCONNLOST) vẫn CHƯA implement trong poll(), việc TLS này không đụng vào phần đó — xem mục "LỖ HỔNG CHƯA XỬ LÝ" bên dưới, đừng nhầm là đã xong.

Thêm field trong struct a7677s, ngay sau mqtt_use_ssl:

c  uint8_t  mqtt_cert_has_ca;
  uint8_t  mqtt_cert_has_client;
  uint8_t  mqtt_cert_has_key;
  char     mqtt_cert_ca[A7677S_CERT_PEM_MAX];
  char     mqtt_cert_client[A7677S_CERT_PEM_MAX];
  char     mqtt_cert_key[A7677S_CERT_PEM_MAX];
  uint16_t mqtt_cert_ca_len;
  uint16_t mqtt_cert_client_len;
  uint16_t mqtt_cert_key_len;
  uint8_t  mqtt_ssl_authmode;  /* 0/1/2, computed từ has_client && has_key */
3 buffer cert tách riêng (không dùng chung 1 scratch buffer) để nội dung cert vẫn còn nếu cần retry 1 bước sau mà không phải gọi lại mqtt_connect() với cert mới — dù hiện tại mqtt_connect() vẫn yêu cầu người gọi truyền lại cert mỗi lần gọi (xem TODO bên dưới).
Nội dung đầy đủ 2 file này đã được dán trong chat trước đó (2 lượt gần nhất của phiên trước khi hết token) — nếu cần đối chiếu chính xác từng dòng, tìm lại trong lịch sử chat trước khi gõ lại từ đầu.
ĐANG LÀM DỞ — CHƯA HOÀN THÀNH, ĐÂY LÀ VIỆC ĐẦU TIÊN CẦN LÀM TIẾP
3. SynaptiX_FDK/components/a76xx/a7677s.c — mới sửa forward declarations, CHƯA implement logic:
Đã thêm (xác nhận đã áp dụng vào file thật trong WS_v1_edit):
c/* TLS cert-upload / SSL-config sequence callbacks (forward declarations). */
static void cb_mqtt_cert_ca_prompt      (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_cert_ca_data        (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_cert_client_prompt  (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_cert_client_data    (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_cert_key_prompt     (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_cert_key_data       (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_sslcfg_cacert       (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_sslcfg_clientcert   (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_sslcfg_clientkey    (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_sslcfg_authmode     (modem_t *modem, const char *response, modem_response_st_t res, void *arg);
static void cb_mqtt_sslbind             (modem_t *modem, const char *response, modem_response_st_t res, void *arg);

static void begin_ssl_cert_block(a7677s_t *dce);
static void begin_sslcfg_block(a7677s_t *dce);
CHƯA CÓ IMPLEMENTATION THẬT cho bất kỳ hàm nào ở trên — chỉ mới khai báo forward declaration, chưa viết thân hàm.
VIỆC CẦN LÀM TIẾP THEO, THEO ĐÚNG THỨ TỰ:
a) Implement a7677s_mqtt_connect() với chữ ký mới (thêm ca_cert/client_cert/client_key), lưu cert content vào 3 buffer mới, set mqtt_cert_has_*, tính mqtt_ssl_authmode, rồi rẽ nhánh: nếu use_ssl=0 giữ nguyên luồng cũ (START → ACCQ → CONNECT), nếu use_ssl=1 sau cb_mqtt_start thành công thì gọi begin_ssl_cert_block(dce) thay vì đi thẳng ACCQ.
b) Implement begin_ssl_cert_block(dce): kiểm tra mqtt_cert_has_ca → nếu có, chuyển mqtt_state = A7677S_MQTT_CERT_CA_PROMPT, gửi AT+CCERTDOWN="<A7677S_CERT_FILENAME_CA>",<mqtt_cert_ca_len>\r\n (chờ ">"), callback cb_mqtt_cert_ca_prompt. Nếu không có CA cert → nhảy thẳng qua kiểm tra mqtt_cert_has_client, tương tự... nếu không có cert nào cả → gọi thẳng begin_sslcfg_block(dce).
c) Implement từng cặp cb_mqtt_cert_*_prompt/cb_mqtt_cert_*_data: _prompt callback (chờ ">" thành công) → gửi raw PEM bytes (dùng send_mqtt_dynamic(dce, dce->mqtt_cert_ca, "\r\nOK\r\n", "\r\nERROR\r\n", cb_mqtt_cert_ca_data, A7677S_TIMEOUT_CERT_CMD) — QUAN TRỌNG: truyền THẲNG con trỏ dce->mqtt_cert_ca (10241 bytes), TUYỆT ĐỐI KHÔNG copy qua s_mqtt_dyn_cmd_buf (chỉ 384 bytes) — xem mục "BUG ĐÃ PHÁT HIỆN" bên dưới, đây chính là bug cần tránh lặp lại) → _data callback (chờ OK) → chuyển sang cert tiếp theo hoặc begin_sslcfg_block().
d) Implement begin_sslcfg_block(dce) + cb_mqtt_sslcfg_cacert/clientcert/clientkey/authmode + cb_mqtt_sslbind: chuỗi AT+CSSLCFG="cacert",0,"<file|\"\">" → AT+CSSLCFG="clientcert",0,"<file|\"\">" → AT+CSSLCFG="clientkey",0,"<file|\"\">" → AT+CSSLCFG="authmode",0,<mqtt_ssl_authmode> → AT+CMQTTSSLCFG=0,0 (bind ssl_ctx 0 vào client 0) → sau khi cb_mqtt_sslbind OK thì chuyển A7677S_MQTT_ACCQ, tiếp tục luồng cũ y hệt nhánh TCP thường. Nếu cert rỗng ở 1 bước CSSLCFG (VD không có client_cert), vẫn phải gửi AT+CSSLCFG="clientcert",0,"" (chuỗi rỗng) để modem biết bỏ qua field đó — không được skip lệnh, chỉ skip nội dung filename.
e) Sửa cb_mqtt_start (đã có sẵn, đọc lại trước khi sửa): sau khi AT+CMQTTSTART OK, hiện tại code cũ luôn nhảy thẳng ACCQ. Cần đổi thành: nếu dce->mqtt_use_ssl → gọi begin_ssl_cert_block(dce); nếu không → giữ nguyên nhảy ACCQ như cũ.
BUG ĐÃ PHÁT HIỆN TRONG PHIÊN NÀY, CHƯA SỬA, NGƯỜI DÙNG CHƯA KỊP XÁC NHẬN CHO PHÉP SỬA (dừng lại đúng ở đây khi hết token)
Vị trí: cb_mqtt_pub_topic() (dòng ~890) và cb_mqtt_pub_payload() (dòng ~932) trong a7677s.c hiện tại (bản gốc, TRƯỚC khi phiên này sửa gì).
Mô tả bug: cả 2 hàm đều làm:
cstrncpy(s_mqtt_dyn_cmd_buf, dce->mqtt_pub_topic /* hoặc mqtt_pub_payload */, sizeof(s_mqtt_dyn_cmd_buf) - 1);
s_mqtt_dyn_cmd_buf[sizeof(s_mqtt_dyn_cmd_buf) - 1] = '\0';
send_mqtt_dynamic(dce, s_mqtt_dyn_cmd_buf, ...);
s_mqtt_dyn_cmd_buf chỉ 384 bytes (A7677S_MQTT_DYN_CMD_MAX), nhưng mqtt_pub_payload là buffer 10241 bytes (A7677S_MQTT_PAYLOAD_MAX). Với payload dài hơn 383 byte, nội dung bị cắt cụt (truncate) trước khi gửi cho modem, trong khi bước trước đó (AT+CMQTTPAYLOAD=...,<mqtt_pub_payload_len>) đã báo cho modem đúng độ dài GỐC (không bị cắt) — modem sẽ chờ đủ số byte gốc nhưng chỉ nhận được phần bị cắt, dẫn tới timeout hoặc đọc nhầm dữ liệu tiếp theo (VD AT+CMQTTPUB=...) là một phần của payload.
Nguyên nhân gốc: bước copy qua s_mqtt_dyn_cmd_buf là thừa, không cần thiết. send_mqtt_dynamic() chỉ gán con trỏ (command[CMD_DYNAMIC].cmd = cmd_str), không copy — và modem_send_command()/sx_uart_write() không có giới hạn 384 byte nào cả (sx_uart_write() gọi thẳng HAL_UART_Transmit() với đúng strlen(cmd->cmd), không giới hạn kích thước gói).
Cách sửa đúng (đã thống nhất hướng, CHƯA CODE): bỏ bước strncpy trung gian, truyền thẳng dce->mqtt_pub_topic/dce->mqtt_pub_payload làm cmd_str cho send_mqtt_dynamic(). Vì cùng cách viết (copy-qua-384-byte-buffer) là pattern có sẵn trong code, cần cẩn thận KHÔNG lặp lại bug này khi viết code cert upload mới (mục 3c ở trên) — cert PEM cũng dài, phải truyền thẳng dce->mqtt_cert_ca/_client/_key chứ không qua s_mqtt_dyn_cmd_buf.
Trạng thái xác nhận với người dùng: đã hỏi "Xác nhận bạn đồng ý tôi sửa luôn bug này?" — CHƯA NHẬN ĐƯỢC CÂU TRẢ LỜI, phiên hết token ngay tại điểm này. Việc đầu tiên khi tiếp nhận: hỏi lại người dùng câu này trước khi động vào cb_mqtt_pub_topic/cb_mqtt_pub_payload. Không tự ý sửa mà chưa hỏi lại, dù hướng sửa đã rõ.
LỖ HỔNG CHƯA XỬ LÝ — ĐÃ BÁO CÁO, NGƯỜI DÙNG ĐÃ QUYẾT ĐỊNH TẠM GÁC LẠI
RX URC parsing hoàn toàn chưa implement + modem_poll() có thể mất dữ liệu URC:

a7677s.h mô tả kiến trúc rx state machine (mqtt_rx_state, mqtt_incoming_cb, mqtt_connlost_cb) nhưng a7677s.c không có bất kỳ code nào thực sự parse +CMQTTRXSTART/TOPIC/PAYLOAD/END/+CMQTTCONNLOST.
Tài liệu xác nhận: RXTOPIC và RXPAYLOAD có thể lặp lại nhiều lần nếu topic/payload dài — comment header hiện tại đơn giản hóa quá mức, code thật (khi viết) phải cộng dồn đúng theo topic_total_len/payload_total_len từ RXSTART, không phải chỉ 1 lần.
Vấn đề nặng hơn ở tầng modem.c: modem_poll() có if (!modem->isBusy) return; ngay đầu hàm — UART chỉ được đọc khi đang có 1 AT command đang chờ phản hồi. URC bất đồng bộ (đến khi module đang rảnh, không có AT command nào đang bay) sẽ bị mất hoàn toàn ở tầng UART, trước khi a7677s.c kịp thấy, không có log lỗi nào — dữ liệu chỉ đơn giản biến mất.
Dự án CÓ dùng subscribe thật (người dùng xác nhận) → lỗ hổng này nghiêm trọng, gần như chắc chắn xảy ra khi vận hành thật với bất kỳ lệnh điều khiển nào gửi từ server xuống ngoài cửa sổ hẹp lúc isBusy=1.
Quyết định của người dùng: "để tôi test thực tế mới quyết định" — tức là KHÔNG sửa ngay bây giờ, để sau khi test hardware thật rồi mới quay lại thiết kế hướng sửa (2 hướng đã đề xuất, chưa chọn: (A) generic URC handler trong modem.c kiểu add_urc_handler như WS_v0, hay (B) đơn giản modem.c luôn đọc UART vào buffer chung, a7677s.c tự quét URC riêng). Không tự ý code phần này cho tới khi người dùng quay lại với kết quả test và chọn hướng.

WATCHDOG / AUTO-RESET LOGIC — NGƯỜI DÙNG SẼ TỰ LÀM SAU
Người dùng đã nêu yêu cầu tổng quát: publish fail 5 lần liên tiếp → reset SIM (dùng AT+CRESET, đã xác nhận cú pháp — software reset, module tự reboot, không cần GPIO); AT không phản hồi (treo cứng) → reset module bằng lại chu kỳ PWRKEY (power_off_start()/power_on_start(), không dùng chân RESET vật lý GPIOD PIN_11); timeout ở bước nào không thực thi tiếp được → reset. Nhưng người dùng đã chốt rõ: "sau này tôi tự thêm" — đây KHÔNG phải việc cần code trong các phiên tiếp theo trừ khi người dùng chủ động yêu cầu lại. Không tự ý thêm watchdog logic vào a7677s.c/sx_mqtt.c.
VIỆC SAU KHI XONG a7677s.c (mục 3 ở trên) — CHƯA BẮT ĐẦU

Refactor sx_mqtt.c (người dùng đã chọn làm file này TRƯỚC sx_board.c) — bỏ #include "sim76xx.h", chuyển toàn bộ gọi sim76xx_* sang modem_handle_t/a7677s_ops. Đã đọc kỹ toàn bộ 666 dòng sx_mqtt.c + sx_mqtt.h trong phiên này, cần nắm những điểm sau trước khi code:

sx_mqtt.c hiện có sẵn 1 bộ URC parser hoàn chỉnh cho +CMQTTRX*/+CMQTTCONNLOST (process_urc_line(), urc_handler(), dòng 44-211 của file gốc) — đăng ký qua sim76xx_set_on_urc(dce, urc_handler), hàm này KHÔNG có trong modem_ops_t. Cần quyết định: giữ logic parse này ở sx_mqtt.c (cần thêm cách nào đó cho a7677s.c expose URC ra ngoài — nhưng đây lại đụng vào lỗ hổng RX URC đã gác lại ở trên) hay chuyển hẳn logic parse vào a7677s.c (dùng mqtt_incoming_cb_t/mqtt_connlost_cb_t đã có sẵn trong a7677s.h, gọi qua a7677s_mqtt_register_callbacks()). Chưa chốt hướng này với người dùng — cần hỏi trước khi code phần liên quan tới URC trong sx_mqtt.c.
sx_mqtt.c hiện dùng sim76xx_mqtt_send_raw_cmd/sim76xx_mqtt_raw_send (gửi AT thô tùy ý, VD AT+CMQTTCFG="version",0,4 để set MQTT version 3.1.1) — không có trong modem_ops_t, và a7677s.c hiện tại KHÔNG gửi lệnh này ở đâu cả. Cần xác nhận với người dùng: có cần set MQTT version qua AT+CMQTTCFG cho A7677S không, hay bỏ qua (dùng default của modem)? Chưa hỏi.
sx_mqtt.c bắt riêng +CGEV: ME PDN DEACT/+CGEV: NW PDN DEACT (mất mạng) để tự gọi lại sim76xx_start(). Cũng thuộc lỗ hổng URC đã gác lại — chưa có hướng xử lý.
Sau khi cert-upload/SSL logic trong a7677s.c xong (mục 3), sx_mqtt.c sẽ không cần tự làm SSL cert download nữa — chỉ cần gọi mqtt_connect() với đủ tham số mới, đơn giản hóa đáng kể so với code cũ (hiện sx_mqtt.c có ~10 callback riêng cho luồng cert: cb_cert_ca_prompt/done, cb_cert_client_prompt/done, cb_cert_key_prompt/done, cb_ssl_cacert/clientcert/clientkey/authmode, cb_sslcfg, cb_sslbind — TẤT CẢ SẼ BỊ XÓA khi refactor, logic tương đương giờ nằm trong a7677s.c).


Refactor sx_board.c/sx_board.h — khởi tạo board.a7677s (kiểu a7677s_t) qua modem_handle_t { .ops = &a7677s_ops, .ctx = &board.a7677s }. Đồng thời dọn 3 biến chết đã phát hiện từ phiên trước (static sx_gpio_t s_lte_gpio;, static sx_gpio_t powerPin;, static sx_gpio_pin_t s_lte_powerPin = {NULL, NULL}; ở đầu sx_board.c) — người dùng đã đồng ý dọn cùng lúc bước này.
Refactor sx_user_mqtt.c — tương tự, bỏ sim76xx_*.
Refactor sx_sleep_manager.c/.h — tương tự, bỏ sim76xx_*.

NGUYÊN TẮC QUAN TRỌNG (nhắc lại, bắt buộc tuân thủ)

Nếu repository/tài liệu chưa đọc hết → không được kết luận. Nếu chưa chắc chắn → tiếp tục đọc, không đoán.
Tuyệt đối không tin vào phần "tiến độ" tường thuật trong handoff (kể cả file này) — luôn tự đọc lại repo thật (git log/git status/git diff/view/grep) trước khi kết luận bất cứ điều gì đã làm hay chưa.
Không sửa âm thầm. Nếu phát hiện bug hoặc điểm lệch thiết kế, phải trình bày rõ nguyên nhân/ảnh hưởng/lựa chọn xử lý và hỏi lại người dùng trước khi code — như đã làm với vụ lệch chữ ký mqtt_connect/mqtt_publish và bug truncate s_mqtt_dyn_cmd_buf (câu hỏi đang treo, xem mục BUG ở trên).
Luôn dừng lại sau mỗi phần nhỏ để người dùng review (Phase 5), không code nhiều file/nhiều chức năng lớn liền một lúc.
Luôn present_files VÀ dán nội dung/diff trực tiếp vào chat cho mọi file tạo/sửa, không chỉ làm 1 trong 2.
Watchdog/auto-reset: người dùng tự làm sau, không tự ý thêm.
RX URC handling: gác lại chờ người dùng test thực tế, không tự ý code.
Chân RESET vật lý (GPIOD PIN_11): không dùng, đã chốt dùng lại PWRKEY cycle thay thế.

Ngôn ngữ
Luôn trả lời bằng tiếng Việt (trừ comment code). Tên hàm, biến, API, thuật ngữ kỹ thuật giữ nguyên tiếng Anh. Giải thích theo hướng kỹ sư firmware, có chiều sâu, không trả lời chung chung.
VIỆC ĐẦU TIÊN KHI TIẾP NHẬN — THEO ĐÚNG THỨ TỰ

git clone lại WS_v1 vào /home/claude/work/WS_v1 (bản chỉ đọc, đối chiếu remote) và WS_v0 vào /home/claude/work/WS_v0.
cp -r /home/claude/work/WS_v1 /home/claude/work/WS_v1_edit — tạo lại thư mục làm việc.
Tái tạo lại 2 file đã sửa (modem_ops.h, a7677s.h) vào WS_v1_edit theo đúng nội dung mô tả ở mục "ĐÃ SỬA XONG" bên trên (đối chiếu lại với nội dung đầy đủ đã dán trong lịch sử chat nếu còn truy cập được).
Xác nhận lại forward declarations đã thêm vào a7677s.c (mục 3 ở trên) — nếu chưa có, thêm lại.
Hỏi lại người dùng câu hỏi đang treo: có đồng ý sửa bug truncate s_mqtt_dyn_cmd_buf ở cb_mqtt_pub_topic/cb_mqtt_pub_payload không (mục "BUG ĐÃ PHÁT HIỆN" ở trên)?
Sau khi có câu trả lời, bắt tay vào việc số 3 (a)-(e) ở trên: implement đầy đủ logic TLS/cert-upload trong a7677s.c, tránh lặp lại bug truncate khi viết code mới. Dừng lại sau khi xong để người dùng review.