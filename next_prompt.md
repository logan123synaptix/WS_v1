# HANDOFF — HARDWARE BRING-UP TEST, WS_v1 (STM32H563RIV6)

Viết khi sắp hết token. Mục tiêu: test từng module phần cứng độc lập
bằng code test standalone thuần HAL trong `Core/Src/main.c` (KHÔNG phải
firmware thật `SynaptiX_FDK/app/`), để tách biệt lỗi phần cứng khỏi lỗi
logic app trước khi chạy code app đầy đủ.

============================================================
QUY TẮC BẮT BUỘC (giống các handoff trước)
============================================================
1. KHÔNG tin mô tả "đã test"/"OK" mà không tự đọc lại code + log thật.
   Container reset giữa phiên — RE-CLONE đầu phiên:
   `git clone https://github.com/logan123synaptix/WS_v1.git`
2. Không sửa code âm thầm. Trình bày nghi vấn → hỏi người dùng → chỉ sửa
   sau khi có xác nhận.
3. Comment code tiếng Anh. Trao đổi với người dùng tiếng Việt.
4. KHÔNG có compiler thật trong container (không build được — thiếu
   startup files/linker script/HAL đầy đủ). Có thể cài
   `gcc-arm-none-eabi` qua `apt-get` để chạy `objdump`/`nm`/`size` trên
   file `.elf` người dùng upload, KHÔNG build từ đầu được. Người dùng tự
   build + flash trên máy công ty và gửi log lại qua chat.
5. Board test vật lý duy nhất: STM32H563RIV6, đã về thực tế (không còn
   dùng repo TF làm board thử tạm nữa).
6. Datasheet đầy đủ nằm trong `Documents/` (đã xác nhận CÓ THẬT trong
   repo): `a7677s.md`, `a76xx_at_cmd.md` (537KB, tập lệnh AT), `bno055.md`,
   `gps_gp02_aithinker.md`, `Datasheet_SHT3x_DIS.md`,
   `SPS30_dust_sensor (1).md`, `ze12a-electrochemical-module-manual-v1_0.md`,
   `ads1115.pdf`. LUÔN đọc tài liệu ở đây trước khi đoán thông số phần
   cứng (baudrate, timing, register address, polarity chân...).

============================================================
BỐI CẢNH: TẠI SAO CÓ CODE TEST STANDALONE TRONG main.c
============================================================
Firmware thật (`SynaptiX_FDK/app/app.c`, kiến trúc phân tầng driver/
service/app đầy đủ — xem prompt hệ thống/handoff kiến trúc trước đó) MỚI
VIẾT, CHƯA TỪNG CHẠY TRÊN BOARD THẬT. Thay vì debug cả app phức tạp cùng
lúc, người dùng quyết định: viết code test TỐI GIẢN, THUẦN HAL (không qua
`sx_uart`/`sx_i2c`/`sx_spi`/`Board_t` abstraction layer), trực tiếp trong
`Core/Src/main.c`, để xác nhận TỪNG MODULE PHẦN CỨNG sống trước — nếu sau
này chạy code app thật mà fail, sẽ biết chắc là do LOGIC APP SAI, không
phải do phần cứng/board.

`main.c` hiện tại có `sx_board_init()`/`app_init()`/`app_process()` bị
COMMENT OUT — đây là chủ đích, không phải quên. KHÔNG bỏ comment các dòng
này trừ khi người dùng yêu cầu chuyển sang chạy app thật.

============================================================
TRẠNG THÁI TEST — TÍNH ĐẾN HẾT PHIÊN NÀY
============================================================

## ĐÃ XÁC NHẬN SỐNG (qua log thật từ board, không suy đoán)

### 1. UART1 + Modem A7677S — OK, ổn định
- PWRKEY: PD12. Code hiện tại (đã người dùng tự chỉnh và xác nhận hoạt
  động, KHÔNG theo đúng datasheet nhưng THỰC TẾ CHẠY ĐƯỢC — xem mục cảnh
  báo bên dưới): kéo SET (50ms) rồi RESET (không delay gì thêm, dòng
  `HAL_Delay(8050)` đã bị người dùng COMMENT OUT).
- Lệnh test hiện tại: `CMD_AT_TEST = "AT\r\n"` (chỉ gửi `AT` đơn giản,
  KHÔNG PHẢI `AT+CGSN` — người dùng đã đổi lại, xem mục lịch sử debug).
- Spam mỗi 2000ms (`AT_SPAM_INTERVAL_MS`), vô thời hạn.
- Log xác nhận: `[SIM TX] AT` → `[SIM RX] AT` → `[SIM RX] OK` lặp lại ổn
  định qua rất nhiều chu kỳ.
- Trước đó đã từng test thành công lấy IMEI qua `AT+CGSN`:
  `861385071580230` (15 chữ số, đúng định dạng) — xác nhận chắc chắn
  UART1 hoạt động 2 chiều hoàn hảo, không giới hạn ở lệnh `AT` đơn giản.
- Đã quan sát URC `*ATREADY: 1` (module báo sẵn sàng) và
  `+CPIN: SIM REMOVED` (đúng thực tế — board CHƯA cắm SIM card vật lý,
  chỉ test UART/IMEI, không cần SIM).

### 2. UART2 + GPS (chip thật: CASIC AT6558R, không phải u-blox) — OK
- Chân bật nguồn: `GPS_CPW_Pin` (PC2, N/F Shutdown Control, active-HIGH-
  to-enable) VÀ `GPS_RST_Pin` (PC3, external reset).
- **PHÁT HIỆN QUAN TRỌNG (đã xác nhận qua đọc code driver thật
  `SynaptiX_FDK/components/modules/gps/gps.c`)**: `GPS_RST_Pin` bị
  CubeMX-generated startup code kéo LOW mặc định trước khi bất kỳ code
  app nào chạy — PHẢI tự kéo lại HIGH, nếu không GPS bị giữ reset vĩnh
  viễn, không bao giờ gửi NMEA dù UART hoàn toàn đúng. Code test
  (`power_on_gps()`) đã xử lý đúng: kéo cả CPW và RST lên HIGH, đợi 1s.
- Log xác nhận: banner khởi động chuẩn của chip CASIC AT6558R
  (`$GPTXT,...MA=CASIC*27`, `IC=AT6558R-5N-32-1C580901`,
  `SW=URANUS5,V5.3.0.0`), sau đó bắt đầu gửi câu `$GNGGA`/`$GNGLL` đều
  đặn (chưa có fix vệ tinh — `0` satellite, các trường toạ độ rỗng — ĐÚNG
  HÀNH VI BÌNH THƯỜNG lúc mới bật, cold-start cần 30s-vài phút, đặc biệt
  nếu test trong nhà/gần cửa sổ).
- GPS RX được gom thành từng dòng NMEA hoàn chỉnh để log (không phải hex
  từng byte) — dùng `gps_line_buf`/`gps_line_flush()`, cùng pattern với
  SIM.

### 3. I2C1 + BNO055 (IMU) — OK
- Chân reset: `PB8` (`I2C1_RESET_Pin`), active-LOW (kéo LOW 10ms rồi thả
  HIGH, đợi 650ms — đúng `DELAY_RESET_MS` trong `bno055.c` thật).
- Địa chỉ I2C: `0x29 << 1` (`BNO055_I2C_ADDR_DEFAULT`).
- Đọc thanh ghi `CHIP_ID` (0x00) qua `HAL_I2C_Mem_Read` — **kết quả: `0xA0`,
  ĐÚNG KHỚP giá trị chuẩn**. Xác nhận chip BNO055 sống, giao tiếp I2C1
  hoạt động đúng.
- Lưu ý: chân `PB8` theo comment cũ trong `sx_board.c` ghi "dùng chung với
  RTC" nhưng code driver thật xác nhận CHỈ IMU dùng chân reset vật lý
  này, RTC tự reset qua lệnh I2C riêng — không có xung đột.

## CHƯA XÁC NHẬN / FAIL — CẦN ĐIỀU TRA TIẾP Ở PHIÊN SAU

### 4. SPI1 + W25Q128 (External Flash) — FAIL, CHƯA GIẢI QUYẾT
Log thật:
```
[W25Q128] JEDEC ID: 00 00 00 (expect EF 40 18)
[W25Q128] FAIL - JEDEC ID khong khop, kiem tra day SPI/CS/nguon
```
Nhận toàn `0x00` — đây là dấu hiệu KINH ĐIỂN của 1 trong các nguyên nhân
sau (CHƯA xác định được cái nào đúng, cần điều tra ở phiên sau, KHÔNG
được đoán mù mà phải loại trừ từng bước có bằng chứng):

a) **CS pin sai hoặc không toggle đúng** — code test dùng
   `SPI1_CS_GPIO_Port`/`SPI1_CS_Pin` (định nghĩa trong `Core/Inc/main.h`:
   `PC12`), khớp với `sx_board.h`'s `SPI_CS_Port`/`SPI_CS_Pin` (cũng PC12)
   — 2 định nghĩa NHẤT QUÁN nên khả năng sai tên chân thấp, nhưng CHƯA
   loại trừ khả năng schematic thật khác với `.ioc`/`main.h` (nhầm lẫn
   giữa các bản mạch revision khác nhau) — cần người dùng xác nhận bằng
   đo trực tiếp (multimeter/oscilloscope) chân CS có toggle khi code chạy
   không.
b) **SPI1 chưa init đúng mode/tốc độ/CPOL-CPHA** — CHƯA kiểm tra
   `MX_SPI1_Init()` (`Core/Src/spi.c`) xem cấu hình clock polarity/phase
   có đúng W25Q128 yêu cầu không (thường Mode 0: CPOL=0, CPHA=0). Đây là
   nghi vấn ưu tiên cao nhất vì toàn 0x00 thường có nghĩa SPI clock/mode
   sai khiến chip không hiểu lệnh, KHÔNG PHẢI dây đứt hẳn (dây đứt hẳn
   thường ra 0xFF do pull-up, hoặc dữ liệu ngẫu nhiên/nhiễu, không phải
   toàn 0x00 sạch như vậy).
c) **Nguồn cấp cho chip flash chưa lên** — theo comment trong
   `sx_W25Q128.c`: "No power-cutoff GPIO on this board revision — the
   chip's 3.3V is wired directly" — nghĩa là KHÔNG có GPIO điều khiển
   nguồn (khác SIM/GPS có GPIO enable riêng), nên nếu nguồn không lên thì
   là lỗi mạch/hàn, không phải thiếu bước "bật nguồn" trong code như
   SIM/GPS.
d) **Thiếu bước "wake from power-down" trước khi đọc JEDEC ID** — code
   driver thật (`sx_W25Q128_init()`) làm đúng 2 bước: (1) gửi lệnh
   `RELEASE_POWER_DOWN` (0xAB) TRƯỚC, đợi 1ms, rồi MỚI (2) gửi
   `JEDEC_ID` (0x9F). Code test hiện tại **CHỈ làm bước (2), THIẾU HẲN
   bước (1)** — đây là khác biệt cụ thể, có thể là nguyên nhân, CẦN THỬ
   TRƯỚC TIÊN ở phiên sau vì đây là thay đổi rẻ nhất, rõ ràng nhất, và
   khớp đúng với code driver thật đã verify đúng ở nơi khác.
e) Dây SPI (MOSI/MISO/SCK) đứt/hàn lỗi — cần đo trực tiếp nếu (b) và (d)
   không giải quyết được.

**VIỆC ĐẦU TIÊN PHIÊN SAU NÊN LÀM**: thêm bước gửi
`W25Q128_CMD_RELEASE_POWER_DOWN (0xAB)` + `HAL_Delay(1)` TRƯỚC khi gửi
JEDEC ID trong `test_flash_w25q128()` (`Core/Src/main.c`), giống hệt thứ
tự trong `sx_W25Q128_init()` thật
(`SynaptiX_FDK/components/modules/external_flash/sx_W25Q128.c`, dòng
97-105), rồi test lại trước khi nghi ngờ phần cứng.

============================================================
LỊCH SỬ DEBUG CHI TIẾT — CÁC BUG ĐÃ GẶP VÀ FIX (để tránh lặp lại)
============================================================

## Bug 1 — Lỗi cú pháp nghiêm trọng (đã fix từ lâu)
Code ban đầu người dùng tự viết có:
```c
HAL_UART_Receive_IT(&huart1, &uart_byte_sim, 1){
  /*Code here*/
}
```
Đây SAI CÚ PHÁP HOÀN TOÀN — `HAL_UART_Receive_IT` là hàm HAL có sẵn để
"arm" nhận ngắt 1 lần, KHÔNG PHẢI nơi viết thân callback. Callback thật
tên cố định `HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)`. Đã sửa
đúng ở các phiên trước.

## Bug 2 — Mất byte do dùng single-byte-buffer thay vì ring buffer (đã fix)
`uint8_t uart_byte_sim` + `uint8_t sim_rx_flag` (1 biến, không phải
queue) khiến ISR ghi đè byte mới lên byte cũ trước khi main loop kịp đọc,
ở baudrate 115200 (~87us/byte). Người dùng tự xác nhận bằng oscilloscope:
"gửi AT đi và dùng oscillo để đo thì thấy có xung trả về đấy" — chứng
minh phần cứng OK, lỗi nằm ở tầng phần mềm. ĐÃ SỬA bằng ring buffer 256
byte (`ring_buf_t`, `ring_push()`/`ring_pop()`) cho cả SIM và GPS — ISR
chỉ đẩy byte vào buffer rồi return ngay, main loop rút cạn buffer ở tốc
độ riêng, không bao giờ mất byte.

## Bug 3 — PWRKEY polarity: datasheet nói 1 đằng, code chạy được 1 nẻo
Datasheet A7677S (`Documents/a7677s.md`, mục 3.2.1 + Table 14): PWRKEY
active-LOW, pull-up nội bộ, cần kéo LOW tối thiểu 50ms rồi thả HIGH. Code
ĐÃ TỪNG được sửa đúng theo hướng này (`GPIO_PIN_RESET` để kéo LOW,
`GPIO_PIN_SET` để thả HIGH), nhưng ở 1 phiên sau, NGƯỜI DÙNG TỰ ĐỔI LẠI
NGƯỢC (dòng code thật hiện tại: `GPIO_PIN_SET` rồi comment "kéo LOW",
`GPIO_PIN_RESET` rồi comment "thả về HIGH" — comment và code KHÔNG khớp
nhau, đây là tàn dư từ việc đảo ngược) — và NGƯỜI DÙNG XÁC NHẬN RÕ RÀNG:
"giữ nguyên như hiện tại vì thấy module vẫn trả lời được (có thể polarity
thực tế khác datasheet)". KHÔNG ĐƯỢC tự ý sửa lại theo datasheet nữa —
đây là quyết định có chủ đích của người dùng dựa trên bằng chứng thực tế
đo được, không phải sai sót. Nếu nghi ngờ lại vấn đề này trong tương lai,
PHẢI hỏi lại rõ ràng trước khi đổi, vì đã hỏi và có câu trả lời dứt khoát
1 lần rồi.

## Bug 4 — CMD_AT_TEST thiếu \r\n (đã fix, rồi người dùng tự đổi lại đơn giản)
Từng có bug `CMD_AT_TEST = "AT+CGMM=?"` KHÔNG có `\r\n` — sẽ khiến module
không bao giờ xử lý được lệnh. Model AI đã sửa thêm `\r\n` + đổi sang
`AT+CGSN` (lấy IMEI theo đúng yêu cầu ban đầu). Ở phiên sau đó, NGƯỜI
DÙNG covered lại code trên nền mới (`e8c01b3`), đơn giản hoá về
`CMD_AT_TEST = "AT\r\n"` (chỉ test AT cơ bản, không phải IMEI nữa) — đã
CÓ `\r\n` đúng, không còn bug thiếu ký tự kết thúc dòng. Đây có vẻ là lựa
chọn có chủ đích để tối giản test liên tục (đổi lại `AT+CGSN` nếu cần lấy
IMEI thêm lần nữa, đơn giản chỉ cần đổi `#define`).

## Bug 5 (tiềm ẩn/chưa xảy ra) — `sim_line_buf`/`gps_line_buf` là dòng chỉ
chứa "\r" sẽ vẫn in ra 1 dòng log gần như trống
Ví dụ log SIM RX thường có: dòng `AT` (echo), dòng trống (chỉ có `\r`),
dòng `OK`. Đây LÀ HÀNH VI ĐÚNG (module tự thêm `\r\n` phụ giữa echo và
kết quả theo chuẩn AT command ATE1 echo mode), KHÔNG PHẢI bug, chỉ ghi
chú lại để phiên sau không nhầm tưởng là lỗi.

============================================================
CẤU TRÚC CODE TEST HIỆN TẠI (Core/Src/main.c, USER CODE BEGIN 0)
============================================================
- `ring_buf_t` + `ring_push()`/`ring_pop()`: ring buffer 256 byte dùng
  chung cho SIM và GPS RX, ISR-safe.
- `sim_line_buf`/`sim_line_flush()`, `gps_line_buf`/`gps_line_flush()`:
  gom byte RX thành từng dòng hoàn chỉnh (kết thúc bởi `\n`) rồi log 1
  lần — dễ đọc hơn hex từng byte.
- `power_on_sim()`: bật nguồn A7677S qua PD12 (xem Bug 3 ở trên về
  polarity).
- `power_on_gps()`: bật nguồn + thả reset GPS qua PC2 (CPW) + PC3 (RST).
- `test_flash_w25q128()`: đọc JEDEC ID qua SPI1, KHÔNG có bước
  release-power-down trước — XEM MỤC 4 CẦN SỬA Ở TRÊN.
- `test_imu_bno055()`: reset qua PB8 rồi đọc CHIP_ID qua I2C1 — ĐÃ OK,
  không cần sửa gì.
- `uart_test_init()`: gọi `logger_init()` + arm RX interrupt ban đầu cho
  UART1/UART2.
- `uart_test_poll()`: gọi mỗi tick trong `while(1)` — rút cạn ring
  buffer, gom dòng, log, và spam `AT` mỗi 2000ms.
- `HAL_UART_RxCpltCallback()` (USER CODE BEGIN 4, cuối file): đẩy byte
  vào ring buffer tương ứng rồi re-arm ngay.

`main()`: gọi theo thứ tự `uart_test_init()` → `power_on_sim()` →
`power_on_gps()` → `test_flash_w25q128()` → `test_imu_bno055()`, rồi vào
`while(1)` chỉ gọi `uart_test_poll()` (SIM/GPS test là liên tục/vô hạn,
Flash/IMU chỉ test 1 lần lúc boot).

**CẢNH BÁO QUAN TRỌNG CHO PHIÊN SAU**: local container (lúc viết handoff
này) đang có thay đổi CHƯA COMMIT/PUSH lên GitHub (thêm
`test_flash_w25q128()`/`test_imu_bno055()`). Nếu người dùng đã tự
commit/push trước khi phiên sau bắt đầu, RE-PULL và đọc lại `git log`/
`git diff` để xác nhận chính xác trạng thái, ĐỪNG giả định code y hệt mô
tả trong handoff này — luôn đối chiếu bằng cách đọc code thật, đúng quy
tắc bắt buộc mục 1.

============================================================
CÁC MODULE CHƯA TEST (còn lại theo yêu cầu tổng của dự án)
============================================================
- UART3 (RS485) — chưa test.
- UART4 (SPS30, dust sensor) — chưa test.
- UART5 (ZE12A, gas sensor) — chưa test.
- I2C1 + SHT3x (nhiệt độ/độ ẩm) — chưa test riêng (dùng chung bus I2C1
  với BNO055 đã OK, nên bus I2C1 vật lý coi như đã xác nhận sống, nhưng
  CHƯA test riêng địa chỉ/logic của SHT3x).
- I2C1 + RTC RX8130CE — chưa test riêng.
- I2C1 + ADS1115 (ADC đo rail nguồn) — chưa test riêng. Lưu ý từ phiên
  phân tích kiến trúc trước: giá trị điện trở shunt R16 (AIN1, current-
  sense) CHƯA được người dùng xác nhận, nghi ngờ nhầm với R9=0R — nếu
  test ADS1115, đừng tin số đo dòng điện cho tới khi giải quyết nghi vấn
  này.
- USB — KHÔNG áp dụng nữa, board thật không có cổng USB (đã bỏ hoàn toàn
  ở phiên refactor trước, xem handoff kiến trúc riêng nếu cần chi tiết).

============================================================
GỢI Ý THỨ TỰ LÀM VIỆC PHIÊN SAU
============================================================
1. Re-clone repo, đọc `git log`, so sánh với mục "CẢNH BÁO" ở trên.
2. Đọc lại `Core/Src/main.c` thật để xác nhận trạng thái (đừng tin mô tả
   này mù quáng).
3. Thử sửa `test_flash_w25q128()` thêm bước `RELEASE_POWER_DOWN` trước
   JEDEC ID (xem mục 4d) — đây là nghi vấn ưu tiên cao nhất, rẻ nhất để
   thử trước.
4. Nếu vẫn fail, đọc `Core/Src/spi.c`'s `MX_SPI1_Init()` kiểm tra
   CPOL/CPHA/tốc độ clock có đúng chuẩn SPI Mode 0 mà W25Q128 yêu cầu
   không.
5. Nếu vẫn fail, đề nghị người dùng đo trực tiếp bằng oscilloscope (giống
   cách đã làm rất hiệu quả với SIM trước đây) để xác nhận CS/CLK/MOSI có
   tín hiệu thật không, tách biệt lỗi phần mềm khỏi phần cứng.
6. Sau khi W25Q128 xong, tiếp tục các module còn lại (RS485, SPS30,
   ZE12A, SHT3x, RTC, ADS1115) theo cùng phương pháp: đọc driver thật
   trước → viết code test thuần HAL tối giản → chỉ đọc/test an toàn
   (không ghi/xoá) trước khi thử ghi.