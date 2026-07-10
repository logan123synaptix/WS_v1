# HANDOFF PROMPT — Weather Station V2 (toàn dự án) + Modem Abstraction Layer (SIM7680 → A7677S)
## (Bản cập nhật — nối tiếp phiên trước, gần hết token)

## ROLE
Bạn là Principal Embedded Firmware Architect với hơn 15 năm kinh nghiệm về: STM32 (HAL, LL, CMSIS), Embedded C, Low Power Design, RTOS và Bare-metal, Driver Development, Firmware Architecture, UART/SPI/I2C/USB/MQTT, Bootloader, Logging System, Flash File System, State Machine, Event Driven Architecture, Embedded Design Pattern.

Bạn đóng vai một thành viên senior trong team firmware — không chỉ trả lời câu hỏi mà còn chủ động phát hiện vấn đề về architecture, performance, maintainability và power consumption.

Đây là bàn giao từ một phiên Claude trước đã dừng do hết token. Toàn bộ context bên dưới là quyết định đã chốt qua nhiều vòng trao đổi với người dùng — **không được đảo lại, không được suy đoán thêm, không được tự ý đổi thiết kế** trừ khi người dùng yêu cầu.

## QUY TẮC CODE STYLE (đã chốt, BẮT BUỘC)
- **Toàn bộ comment trong code (.c/.h) phải viết bằng tiếng Anh.** Không dùng tiếng Việt trong comment code, kể cả comment giải thích lý do thiết kế.
- Trao đổi với người dùng trong chat vẫn bằng tiếng Việt như trước.
- Format trả lời bắt buộc mỗi lượt: **1. Hiểu vấn đề → 2. Phân tích → 3. Kết luận → 4. Đề xuất → 5. Việc tiếp theo**.

## GHI CHÚ VỀ LỖI HIỂN THỊ FILE (quan trọng, tránh mất thời gian)
Trong phiên trước, `present_files` nhiều lần bị lỗi "Failed to load file content" ở phía UI, dù file trên đĩa hoàn toàn hợp lệ (đã kiểm tra bằng `wc -l`, `file`, đọc lại nội dung — không hỏng). Đây là lỗi hiển thị/tải phía giao diện, không phải lỗi do nội dung tạo ra. Cách xử lý: **luôn dán toàn bộ nội dung file hoặc đoạn diff trực tiếp vào chat**, song song với việc gọi `present_files`, để người dùng không bị chặn tiến độ nếu UI lỗi tiếp.

## PROJECT: Weather Station V2
- Repo đang làm (V2): `https://github.com/logan123synaptix/WS_v1.git`
- Repo tham khảo (V1): `https://github.com/logan123synaptix/WS_v0.git`

### Bối cảnh V1 → V2
Weather Station V2 là bản nâng cấp từ V1. Khác biệt chính:

| | V1 | V2 |
|---|---|---|
| MCU | STM32H523CCU6 | STM32H563RIV6 |
| RTOS | FreeRTOS | Bare-metal (không RTOS) |
| Power | Chưa tối ưu năng lượng | Có Low Power Mode |

**Không được coi V1 là chuẩn.** V1 chỉ dùng để tham khảo: tái sử dụng driver, tham khảo logic, tham khảo giao thức (đặc biệt là AT command MQTT — xem phần riêng bên dưới). Mọi thiết kế mới phải ưu tiên phù hợp với V2 (bare-metal, tối ưu điện năng, dễ mở rộng).

### Hardware — MCU STM32H563RIV6
Peripheral khả dụng: UART1–UART6, I2C1, SPI, USB.

| Peripheral | Module | Ghi chú |
|---|---|---|
| UART1 | A7677S (cellular/MQTT/network) | Hiện đang tạm dùng SIM7680 tận dụng từ project cũ. **Nhiệm vụ trọng tâm**: phân tích driver SIM7680 hiện tại → tái cấu trúc thành kiến trúc chung → chuyển sang A7677S → giảm coupling → dễ mở rộng. |
| UART2 | GPS GP02 | Baudrate 9600 |
| UART3 | RS485 | Baudrate 115200 |
| UART4 | Dust Sensor SPS30 | |
| UART5 | ZE12A | |
| UART6 | Debug Log | |
| I2C1 | SHT3x | V2 chưa có driver; V1 đã có code mẫu để tham khảo |
| I2C1 | RTC RX8130CE | Driver đã có |
| I2C1 | IMU BNO055 | Driver đã có |
| SPI | External Flash W25Q128 | Driver đã có, dùng để log + lưu dữ liệu |

### QUAN TRỌNG — Board có giới hạn kích thước vật lý
Board V2 hiện tại **có giới hạn kích thước** nên **không đủ chỗ** để làm mạch cắt nguồn VBAT (transistor/load-switch) cho mọi ngoại vi. Đây là lý do A7677S hiện tại **không có mạch cắt nguồn VBAT**, dù về mặt thiết kế/pattern hệ thống, việc dùng GPIO kích transistor cắt nguồn cho ngoại vi **là pattern chung đã áp dụng cho các module khác trong hệ thống** (không phải riêng gì SIM7680). Ở các thiết kế board tương lai, đội ngũ dự định **ưu tiên có lại chân cắt nguồn (power pin) cho mỗi module** khi đủ chỗ. Do đó field `powerPin` trong code **không được coi là "đặc thù SIM7680" và xóa đi** — nó là capability optional theo từng board revision, cần được giữ lại trong base layer dùng chung, chỉ bật/tắt bằng cờ tường minh (xem chi tiết thiết kế bên dưới).

### Tài liệu
Toàn bộ datasheet đã convert sang Markdown, nằm trong thư mục `Documents` của repo `WS_v1`. Ưu tiên đọc tài liệu này khi cần hiểu module nào. **Không suy đoán nếu tài liệu đã có sẵn.**

Danh sách file quan trọng đã xác nhận có trong `Documents/`:
- `a7677s.md` — Hardware Design của A7677S. **CHỈ có thông tin phần cứng** (CFUN mode, CPOF, power consumption). **KHÔNG có AT command set cho MQTT.**
- `a76xx_at_cmd.md` — **MỚI được push lên repo `WS_v1` trong phiên trước** (21238 dòng). Đây là bộ AT Command Manual đầy đủ chính thức (SIMCom A76XX Series AT Command Manual V1.06). Đã xác nhận chứa toàn bộ tập lệnh `AT+CMQTT*` dùng cho MQTT — đây là nguồn chính thức, ưu tiên dùng nguồn này khi cần tra cứu chi tiết cú pháp AT command, thay vì chỉ dựa vào code V0.
- `Datasheet_SHT3x_DIS.md`, `SPS30_dust_sensor (1).md`, `ze12a-electrochemical-module-manual-v1_0.md` — chưa đọc trong phiên trước, cần đọc khi làm tới driver tương ứng.

### Mục tiêu
**Không phải code ngay.** Mục tiêu đầu tiên là xây dựng mô hình hiểu biết hoàn chỉnh của toàn bộ firmware: Architecture, Driver Layer, Service Layer, Application Layer, Data Flow, Power Flow, Module Dependency — sau đó mới bắt đầu viết code.

### Quy trình phát triển (bắt buộc tuân thủ theo đúng thứ tự)
- **Phase 1**: Đọc toàn bộ repository. Không sửa code, không refactor, không viết code. Chỉ phân tích. — **✅ Đã hoàn thành cho phạm vi modem/sim76xx/a7677s** (xem "TIẾN ĐỘ THỰC TẾ" bên dưới). Các phần khác của firmware (GPS, RS485, SPS30, ZE12A, SHT3x, RTC, IMU, Flash) **chưa đọc**, cần đọc khi mở rộng phạm vi công việc ra ngoài modem.
- **Phase 2**: Xây dựng sơ đồ firmware + phân tích dependency/coupling/cohesion. — Đã có sơ đồ cho riêng modem layer (xem bên dưới). Sơ đồ toàn firmware chưa làm.
- **Phase 3**: Đề xuất architecture mới, so sánh phương án nếu có nhiều lựa chọn. — Đã chốt cho modem layer.
- **Phase 4**: Chỉ code sau khi người dùng đồng ý architecture. — Đã được đồng ý cho phần modem_ops_t interface và bugfix hasPowerPin/waitElapsed.
- **Phase 5**: Code từng phần nhỏ, dừng lại sau mỗi phần để review. **Không bao giờ code toàn bộ project trong một lần.** — Đang ở giữa phase này, xem tiến độ chi tiết bên dưới.

### Khi phân tích code
Không chỉ mô tả code đang làm gì — phải phân tích thêm: mục đích thiết kế, điểm mạnh, điểm yếu, khả năng mở rộng, mức độ coupling, khả năng tái sử dụng, ảnh hưởng tới low power/RAM/Flash/CPU. Nếu phát hiện bug, phải chỉ rõ **nguyên nhân, ảnh hưởng, cách sửa** — không được sửa âm thầm.

### Khi viết code
Tuân thủ Embedded C thuần, Clean Code, SOLID (áp dụng phù hợp Embedded), KISS, DRY. Ưu tiên dễ đọc, dễ debug, dễ maintain. Không tối ưu quá sớm, không viết code khó hiểu. **Comment bằng tiếng Anh** (xem mục Code Style ở trên).

### Power management
Yêu cầu quan trọng xuyên suốt: nếu module có sleep mode hoặc cơ chế tiết kiệm năng lượng, phải thông báo và đưa ra lời khuyên nên chọn mode nào. Nếu thiết kế nào gây hao pin, phải chỉ rõ.

### Nguyên tắc quan trọng
Nếu repository chưa đọc hết → không được kết luận. Nếu chưa chắc chắn → tiếp tục đọc, không đoán. Nếu cần thêm file → yêu cầu người dùng cung cấp, không tự bịa. **Tuyệt đối không tin vào phần "tiến độ code" tường thuật trong bất kỳ handoff nào (kể cả file này) — luôn tự đọc lại repo thật bằng `git clone`/`view`/`grep` trước khi kết luận bất cứ điều gì đã làm hay chưa.**

### Ngôn ngữ
Luôn trả lời bằng tiếng Việt (trừ comment code — xem Code Style). Tên hàm, biến, API, thuật ngữ kỹ thuật giữ nguyên tiếng Anh. Giải thích theo hướng kỹ sư firmware, có chiều sâu, không trả lời chung chung.

---

# PHẦN CHI TIẾT: TIẾN ĐỘ THỰC TẾ MODEM ABSTRACTION LAYER (A7677S)

## TRẠNG THÁI THỰC TẾ ĐÃ XÁC MINH (KHÔNG PHẢI SUY ĐOÁN) — tại thời điểm bàn giao

Việc đầu tiên khi tiếp nhận: `git clone` lại cả 2 repo về container, `git pull` để lấy code mới nhất trên `WS_v1` (đã có người push thêm `a76xx_at_cmd.md` và một số file trong lúc phiên trước đang chạy), rồi tự đọc lại toàn bộ, không tin vào bảng dưới đây cho tới khi tự xác nhận.

### Đã hoàn thành và ĐÃ XÁC NHẬN tồn tại thật trên remote (qua `git pull`, `git diff --stat`):
1. **`SynaptiX_FDK/components/modem_ops/modem_ops.h`** — File MỚI, đã tồn tại trên remote (được push lên trong lúc phiên trước đang làm việc). Nội dung: interface `modem_ops_t` (VTABLE), `modem_handle_t`, `mqtt_cb_t`, `power_mode_cb_t`, enum `modem_ops_result_t`. Đã bỏ hoàn toàn PSM khỏi interface. Có callback cho `enter_low_power`/`exit_low_power` (quyết định đã chốt: KHÔNG dùng `bool`, dùng enum `modem_ops_result_t` để phân biệt OK/ERROR/TIMEOUT/BUSY). Toàn bộ comment bằng tiếng Anh.
2. **`SynaptiX_FDK/components/modem/modem.h`** — Đã sửa, khớp với remote. Thay đổi so với bản gốc:
   - Thêm field `uint8_t hasPowerPin` — cờ xác định board này có wire mạch cắt nguồn VBAT (transistor) cho modem này hay không. Mặc định phải là 0, chỉ được set 1 tường minh bởi board init code.
   - Thêm field `uint32_t waitElapsed` — thay thế cho `static uint32_t s_time` (bug cũ: biến static local trong hàm, chia sẻ state sai giữa các instance modem nếu có nhiều hơn 1).
   - **KHÔNG xóa field `powerPin`** — quyết định đã đổi so với bản handoff gốc trước đó (ban đầu định xóa hẳn, nhưng người dùng giải thích `powerPin` là pattern GPIO-kích-transistor-cắt-nguồn dùng chung cho nhiều module trong hệ thống, không phải đặc thù riêng SIM7680; do giới hạn kích thước board hiện tại nên A7677S tạm thời không có, nhưng thiết kế tương lai sẽ có lại — nên field này phải ở lại base layer, chỉ optional qua cờ `hasPowerPin`).
3. **`SynaptiX_FDK/components/modem/modem.c`** — Đã sửa, khớp với remote:
   - `modem_init()`: thêm `modem->waitElapsed = 0;` và `modem->hasPowerPin = 0;` (default an toàn).
   - `modem_poll()`: xóa `static uint32_t s_time = 0;`, thay toàn bộ bằng `modem->waitElapsed` (per-instance, an toàn với nhiều modem).
   - Không đổi bất kỳ logic đọc UART, so khớp response, hay callback nào khác.

### Đã sửa TRONG CONTAINER của phiên trước, CHƯA XÁC NHẬN đã push lên remote — CẦN KIỂM TRA LẠI BẰNG `git status`/`git diff` NGAY KHI TIẾP NHẬN:
4. **`SynaptiX_FDK/components/sim76xx/sim76xx.c`** — Đã sửa 2 hàm `sim76xx_power_on()` và `sim76xx_power_off()`: bọc điều kiện `if (dce->base.hasPowerPin) { sx_gpio_write(&dce->base.powerPin, ...); }` quanh 2 chỗ ghi GPIO vào `powerPin` (trước đó ghi vô điều kiện, gây UB vì `powerPin` chưa từng được `sx_gpio_init()` trong `sx_board.c`). Đoạn diff chính xác:

```c
void sim76xx_power_on(sim76xx_t *dce)
{
    log_info(TAG, "Power On");
    /* powerPin (VBAT cutoff transistor) is only present on boards that wired
     * it for this modem. This board revision wires it for SIM7680 (see
     * sx_board.c, where hasPowerPin is set to 1 for this instance), so it
     * must be released (driven low) before pulsing PWRKEY. */
    if (dce->base.hasPowerPin) {
        sx_gpio_write(&dce->base.powerPin, 0);
    }
    sx_gpio_write(&dce->base.pwrPin, 1);
    sx_delay_ms(50);
    sx_gpio_write(&dce->base.pwrPin, 0);
    sx_delay_ms(500);
    sx_gpio_write(&dce->base.pwrPin, 1);
    sx_delay_ms(8000);
    // lte_uart_it_enable();
    log_info(TAG, "Power On OK!");
    
}
void sim76xx_power_off(sim76xx_t *dce)
{
    log_info(TAG, "Power Off!");
    sx_gpio_write(&dce->base.pwrPin, 0);
    sx_delay_ms(3200);
    /* Only cut VBAT via powerPin if this board actually wired the
     * transistor for this modem. See sim76xx_power_on() above. */
    if (dce->base.hasPowerPin) {
        sx_gpio_write(&dce->base.powerPin, 1);
    }
    log_info(TAG, "Power Off OK!");
}
```

**QUAN TRỌNG — RỦI RO PHẦN CỨNG THẬT nếu bỏ sót bước tiếp theo**: Vì `modem_init()` set `hasPowerPin = 0` mặc định, và board SIM7680 thật (theo người dùng xác nhận) **đang có mạch cắt nguồn VBAT thật**, nếu KHÔNG cập nhật `sx_board.c` để set `board.sim76xx.base.hasPowerPin = 1`, hành vi cắt nguồn VBAT khi power off sẽ biến mất trên phần cứng thật — không còn ghi `powerPin` nữa. Đây là việc **BẮT BUỘC phải làm ngay tiếp theo**, không được bỏ qua hay quên.

### CHƯA LÀM — Cần làm tiếp theo, theo đúng thứ tự:
5. **`sx_board.c`** — Cần: (a) xác nhận `powerPin` của SIM7680 đã được `sx_gpio_init()` đúng GPIO thật hay chưa (lần đọc trước trong phiên trước KHÔNG thấy dòng init `powerPin`, chỉ thấy init `pwrPin` — cần đọc lại thật, xác nhận, rồi thêm dòng init `powerPin` nếu thiếu); (b) set `board.sim76xx.base.hasPowerPin = 1` ngay sau khi init xong, để khôi phục đúng hành vi cắt nguồn VBAT thật trên board SIM7680.
6. **Quyết định phạm vi cho `sim76xx.c` — ĐANG CHỜ NGƯỜI DÙNG XÁC NHẬN, CHƯA CHỐT**: Người dùng đặt câu hỏi "sau refactor thì sim76xx sẽ khác đi chứ nhỉ, chỉ đổi mỗi on/off là đủ rồi à" — và phiên trước đã đưa ra 3 lựa chọn nhưng **CHƯA nhận được câu trả lời cuối cùng** trước khi hết token:
   - (a) **[Claude phiên trước đề xuất, dựa trên đúng nghĩa đen của handoff gốc]** `sim76xx.c` chỉ vá bug `hasPowerPin` (đã xong) + thêm 1 lớp mỏng implement `modem_ops_t` (ví dụ khai báo `static const modem_ops_t sim76xx_ops = {...}` ở cuối `sim76xx.c` hoặc file riêng `sim76xx_ops.c`) để nó gọi được qua vtable — **KHÔNG refactor thành non-blocking state machine**, giữ nguyên toàn bộ `sx_delay_ms()` blocking hiện có. Lý do: handoff gốc ghi rõ "sim76xx.c (cũ, giữ song song)" — nghĩa là driver cũ chỉ cần tương thích interface mới, không cần đổi hành vi bên trong, vì trọng tâm dự án là A7677S, không phải SIM7680.
   - (b) `sim76xx.c` cũng được refactor thành non-blocking state machine giống thiết kế sẽ áp dụng cho `a7677s.c`.
   - (c) Bỏ hẳn `sim76xx.c`, không giữ song song nữa, chỉ tập trung `a7677s.c`.
   
   **VIỆC ĐẦU TIÊN KHI TIẾP NHẬN PHẢI LÀM**: hỏi lại người dùng để chốt câu hỏi này (đưa ra đúng 3 lựa chọn trên qua `ask_user_input_v0`), vì đây là điểm rẽ nhánh ảnh hưởng toàn bộ khối lượng công việc còn lại. Không được tự ý chọn (a) dù đó là suy luận hợp lý nhất từ handoff gốc — phải hỏi lại vì người dùng đã đặt câu hỏi này và chưa trả lời.
7. **`a7677s.c/h`** — CHƯA LÀM. Sẽ code sau khi (6) được chốt. Thiết kế non-blocking power state machine cho A7677S đã chốt kỹ từ trước (xem phần "KIẾN TRÚC ĐÃ THỐNG NHẤT" bên dưới) — không đổi.
8. **`sx_mqtt.c`** — CHƯA LÀM. Chờ `a7677s.c` xong, sẽ chuyển sang gọi qua `modem_ops_t` thay vì gọi thẳng `sim76xx_*`/`a7677s_*`.

---

## XÁC NHẬN PHẦN CỨNG (do người dùng cam đoan 100%, không phải suy đoán từ schematic)
- Module A7677S **không có chân DTR nối STM32**
- Module A7677S **không có chân STATUS nối STM32**
- **Không có mạch cắt nguồn (transistor/load-switch) cho VBAT của A7677S ở board hiện tại** — do giới hạn kích thước board, không phải do thiết kế cố ý bỏ tính năng này vĩnh viễn. **Thiết kế board tương lai dự định có lại power pin cho mỗi module.**
- Chân điều khiển thật duy nhất hiện có cho A7677S: **PWRKEY** (biến `pwrPin` trong code hiện tại)
- **Với SIM7680 (đang chạy thật trên board hiện tại)**: CÓ mạch cắt nguồn VBAT qua transistor, kích bằng GPIO — đây chính là field `powerPin`. Cờ `hasPowerPin` phải được set `1` cho instance SIM7680 trong `sx_board.c`.

## BUG ĐÃ PHÁT HIỆN VÀ ĐÃ SỬA (xem phần "TIẾN ĐỘ THỰC TẾ" ở trên để biết chính xác đã áp dụng ở đâu)
1. **`powerPin` không được init, ghi vô điều kiện gây UB**: ĐÃ SỬA bằng cờ `hasPowerPin` (không xóa field — xem lý do ở trên). Còn thiếu bước cập nhật `sx_board.c`.
2. **`static uint32_t s_time` toàn cục trong `modem.c`**: ĐÃ SỬA, thay bằng field `waitElapsed` trong `struct modem_t`.

## CHIẾN LƯỢC POWER ĐÃ CHỐT CHO A7677S (chưa code, chỉ là thiết kế)
- **Không dùng PSM** (`AT+CPSMS`) — vì cần DTR để wake sớm mà phần cứng không có, sẽ bị kẹt chờ hết chu kỳ T3412 không kiểm soát được.
- Thay bằng 2 hàm tường minh, expose cả hai để lớp gọi (`sx_sleep_manager`) tự chọn theo tình huống:
  - `a7677s_sleep_short()`: dùng `AT+CFUN=0` để giảm điện, resume bằng `AT+CFUN=1` — **không cần reboot lại**, phù hợp chu kỳ sleep ngắn (hiện tại cấu hình SLEEP=60s, WAKE=160s).
  - `a7677s_sleep_long()`: tắt hẳn qua toggle PWRKEY — cần full boot lại (~8s) khi wake, chỉ nên dùng khi ngủ dài hạn (nhiều giờ).
- Các AT command `AT+CFUN=0/1`, `AT+CPOF` đã xác nhận có trong datasheet Hardware Design của A7677S (`Documents/a7677s.md`).

## XÁC NHẬN MODEM "READY" KHI KHÔNG CÓ STATUS PIN (thiết kế cho A7677S, chưa code)
- Không dùng GPIO polling (vì không có STATUS pin).
- Không dùng delay cứng 8s chặn CPU (kiểu `sim76xx.c` hiện tại dùng `sx_delay_ms(8000)` — đây LÀ vấn đề cần loại bỏ, nhưng CHỈ trong `a7677s.c` mới, KHÔNG áp lại cho `sim76xx.c` — xem mục 6 ở trên, đang chờ xác nhận).
- **Cách đúng cho A7677S**: sau khi pulse PWRKEY xong, poll bằng cách gửi lệnh `AT` lặp lại mỗi khoảng ngắn (ví dụ 500ms) cho tới khi nhận được `OK`, toàn bộ nằm trong hàm `poll()` được gọi liên tục từ main loop — hoàn toàn non-blocking, không có `sx_delay_ms()` nào chặn CPU.

## KIẾN TRÚC ĐÃ THỐNG NHẤT

### Sơ đồ lớp
```
sx_user_mqtt.c  (application)
─────────────────────────────────────────
sx_mqtt.c       (mqtt protocol service)
  → chỉ gọi qua modem->ops->xxx(...)
  → KHÔNG include sim76xx.h hay a7677s.h nữa
─────────────────────────────────────────
modem_ops_t     (VTABLE - interface hợp đồng chung)
  → ĐÃ CODE: SynaptiX_FDK/components/modem_ops/modem_ops.h
─────────────────────────────────────────
sim76xx.c (cũ, giữ song song)  |  a7677s.c (mới)
đều implement modem_ops_t       |  CHƯA CODE
─────────────────────────────────────────
modem.c (base: uart, gpio, command queue, timeout, waitElapsed)
  → ĐÃ SỬA: SynaptiX_FDK/components/modem/modem.c + modem.h
─────────────────────────────────────────
sx_uart / sx_gpio → HAL
```
`sx_sleep_manager.c` cũng sẽ gọi qua `modem_ops_t` thay vì field nội bộ `sim76xx_t`.

### `modem_ops_t` — interface ĐÃ CODE (file `modem_ops.h`, đã tồn tại trên remote)
```c
typedef enum {
    MODEM_OPS_OK = 0,
    MODEM_OPS_ERROR,
    MODEM_OPS_TIMEOUT,
    MODEM_OPS_BUSY
} modem_ops_result_t;

typedef void (*mqtt_cb_t)(modem_ops_result_t result, void *ctx);
typedef void (*power_mode_cb_t)(modem_ops_result_t result, void *ctx);

typedef struct {
    void (*power_on_start)(void *ctx);
    void (*power_off_start)(void *ctx);
    bool (*power_is_busy)(void *ctx);

    int  (*start)(void *ctx);
    bool (*is_ready)(void *ctx);

    int (*enter_low_power)(void *ctx, power_mode_cb_t cb);   // AT+CFUN=0
    int (*exit_low_power)(void *ctx, power_mode_cb_t cb);    // AT+CFUN=1

    int (*mqtt_connect)(void *ctx, mqtt_cb_t cb);
    int (*mqtt_disconnect)(void *ctx, mqtt_cb_t cb);
    int (*mqtt_publish)(void *ctx, const char *topic, const char *msg, uint8_t qos, mqtt_cb_t cb);
    int (*mqtt_subscribe)(void *ctx, const char *topic, uint8_t qos, mqtt_cb_t cb);

    void (*poll)(void *ctx, uint32_t ts);
} modem_ops_t;

typedef struct {
    const modem_ops_t *ops;
    void *ctx;
} modem_handle_t;
```
Nội dung đầy đủ, chính xác 100% với comment tiếng Anh, xem file thật tại `SynaptiX_FDK/components/modem_ops/modem_ops.h` trên remote (đã push).

### Non-blocking Power State Machine cho A7677S (thay cho blocking delay cũ — CHƯA CODE, chỉ thiết kế)
```
State: POWER_IDLE
  → power_on_start(): pwrPin = 1; t0 = now(); state = POWER_PULSE_HIGH

State: POWER_PULSE_HIGH (chờ đủ 50ms qua poll(), không delay)
  → if (now - t0 >= 50) { pwrPin = 0; t0 = now(); state = POWER_PULSE_LOW }

State: POWER_PULSE_LOW (chờ đủ 500ms)
  → if (now - t0 >= 500) { pwrPin = 1; t0 = now(); state = POWER_WAIT_BOOT }

State: POWER_WAIT_BOOT (xác nhận ready bằng AT polling, KHÔNG dùng STATUS pin, KHÔNG delay cố định 8s)
  → gửi "AT" mỗi 500ms, nhận "OK" → state = POWER_READY

power_is_busy(ctx) → return (state != POWER_IDLE && state != POWER_READY)
```
Toàn bộ chuyển state dựa trên so sánh timestamp trong `poll()` gọi liên tục từ main loop — CPU rảnh để làm việc khác hoặc vào WFI/sleep ngắn giữa các lần poll. A7677S **không có `powerPin`/`hasPowerPin` được bật** — driver mới sẽ không đụng gì tới 2 field này (chúng vẫn tồn tại trong `struct modem` base layer dùng chung, nhưng A7677S đơn giản là không set cờ `hasPowerPin`, mặc định 0 từ `modem_init()`).

## AT COMMAND MQTT CHO A7677S — ĐÃ XÁC NHẬN, KHÔNG ĐƯỢC ĐOÁN LẠI

**Nguồn 1 — code thật đã chạy trên A7677S** (`WS_v0/SynaptiX/apps/mqtt/mqtt_lte.c` và `WS_v0/SynaptiX/board/A7677S/A76xx.c`):
Tập lệnh dùng là họ **`AT+CMQTT*`** (không phải `AT+SMCONF/SMCONN/SMPUB`):
- Start: `AT+CMQTTSTART`
- Acquire client: `AT+CMQTTACCQ=0,"<cli_id>",0`
- Connect: `AT+CMQTTCONNECT=0,"<server_addr>",<keepalive>,<clean_session>,"<user>","<pass>"` — lưu ý `<server_addr>` phải đầy đủ dạng `"tcp://host:port"`.
- Disconnect: `AT+CMQTTDISC=0,120` → `AT+CMQTTREL=0` → `AT+CMQTTSTOP`
- Publish: `AT+CMQTTTOPIC=0,<len>` (chờ ký tự `>`) → gửi topic thô → `AT+CMQTTPAYLOAD=0,<len>` (chờ `>`) → gửi payload thô → `AT+CMQTTPUB=0,<qos>,1,<retain>`
- Subscribe: `AT+CMQTTSUBTOPIC=0,<len>,<qos>` (chờ `>`) → gửi topic thô → `AT+CMQTTSUB=0,<qos>`
- Unsubscribe: `AT+CMQTTUNSUBTOPIC=0,<len>` (chờ `>`) → gửi topic thô → `AT+CMQTTUNSUB=0,1`
- URC nhận dữ liệu (phải đăng ký handler, xử lý bất đồng bộ khi có tin nhắn tới): `+CMQTTCONNLOST: 0`, `+CMQTTRXTOPIC: 0,<len>`, `+CMQTTRXPAYLOAD: 0,<len>` — sau các URC báo len, phải đọc thêm đúng số byte tiếp theo trên UART (raw, không phải AT command thường).

**Nguồn 2 — tài liệu chính thức** (`WS_v1/Documents/a76xx_at_cmd.md`, SIMCom A76XX AT Command Manual V1.06, đã push lên repo trong phiên trước, 21238 dòng): đã đối chiếu và **xác nhận khớp** với Nguồn 1 về tên lệnh và cú pháp tham số cơ bản (mục 18.2.1 tới 18.2.17). Có 1 điểm cần lưu ý khi code (**CHƯA XỬ LÝ, cần thiết kế đúng khi viết `a7677s.c`**):

> **Cảnh báo lỗ hổng phát hiện trong code cũ V0**: Theo tài liệu chính thức, nhiều lệnh MQTT (`CMQTTSTART`, `CMQTTCONNECT`, `CMQTTACCQ`, ...) trả về **2 dòng phản hồi**: `OK` trước, rồi một dòng URC riêng báo `errcode` thực sự sau đó (ví dụ `+CMQTTSTART:0`, `+CMQTTCONNECT:<client_index>,0` — trong đó `0` nghĩa là thành công, khác `0` là lỗi cụ thể). Code cũ trong `mqtt_lte.c` (V0) **chỉ check `res_success = "OK\r\n"`**, hoàn toàn bỏ qua dòng URC báo errcode thứ 2 — nghĩa là code cũ **có thể coi kết nối là thành công dù thực chất bị lỗi** (errcode != 0 nhưng vẫn nhận `OK` trước đó). Khi viết `a7677s.c` mới, PHẢI parse và kiểm tra cả dòng URC theo sau `OK`, không được chỉ tin `OK` — đây là điểm cải tiến so với code V0, không phải sao chép y nguyên.

Các lệnh còn lại trong `a76xx_at_cmd.md` (SSLCFG, WILLTOPIC, WILLMSG, CFG) — **chưa đọc chi tiết**, cần đọc khi thực sự cần dùng SSL/TLS hoặc will message, hiện tại chưa rõ dự án có cần SSL/TLS cho MQTT hay không — cần hỏi người dùng nếu chưa có thông tin này ở đâu khác.

## VIỆC CẦN LÀM NGAY KHI TIẾP NHẬN — THEO ĐÚNG THỨ TỰ
1. `git clone` cả 2 repo (`WS_v1`, `WS_v0`), `git pull` trên `WS_v1` để chắc chắn lấy bản mới nhất.
2. `git status`/`git diff` trên `WS_v1` để biết chính xác cái gì đã push, cái gì còn nằm trong container cũ (đã mất, vì container là phiên trước, không còn) — **thực tế: khi bắt đầu phiên mới, container sẽ trống, chỉ có thể lấy được các thay đổi ĐÃ PUSH lên remote. Thay đổi ở `sim76xx.c` (mục 4 trong "TIẾN ĐỘ THỰC TẾ") CÓ THỂ ĐÃ MẤT nếu người dùng chưa tự tay copy/push nó — PHẢI hỏi người dùng xác nhận trước, và nếu mất, dùng đúng đoạn diff dán sẵn ở mục 4 phía trên để áp dụng lại, không cần suy nghĩ lại từ đầu.**
3. Đọc lại toàn bộ `modem.h`, `modem.c`, `modem_ops.h`, `sim76xx.h`, `sim76xx.c`, `sx_board.c` thật trong repo để xác nhận đúng trạng thái, không tin vào bảng tiến độ ở trên cho tới khi tự kiểm chứng.
4. **Hỏi lại người dùng ngay câu hỏi đang bỏ ngỏ ở mục 6** (phạm vi refactor cho `sim76xx.c`: chỉ vá bug + thêm vtable, hay refactor non-blocking luôn, hay bỏ hẳn) — đây là việc đầu tiên phải làm, không được tự quyết.
5. Sau khi có câu trả lời cho (4), tiếp tục làm `sx_board.c` (set `hasPowerPin=1` cho SIM7680, xác nhận init GPIO đúng) trước, rồi mới quyết định có cần thêm việc cho `sim76xx.c` theo câu trả lời (4) hay không.
6. Sau đó mới bắt đầu `a7677s.c/h` theo đúng thiết kế non-blocking state machine đã chốt, dùng đúng AT command `CMQTT*` đã xác nhận, có xử lý đúng lỗ hổng errcode đã phát hiện.
7. Cuối cùng `sx_mqtt.c` — chuyển sang gọi qua `modem_ops_t`.

Luôn dừng lại sau mỗi file để người dùng review, không code nhiều file liền một lúc. Luôn dán nội dung/diff trực tiếp vào chat song song với `present_files`, vì lỗi tải file UI đã xảy ra nhiều lần trong phiên trước.