HANDOFF — HARDWARE BRING-UP TEST, WS_v1 (STM32H563RIV6) — PHIÊN 7

Viết khi sắp hết token. Đọc kỹ handoff phiên 5 VÀ phiên 6 (trong lịch sử
chat) trước khi làm gì — phiên này KẾ TIẾP trực tiếp từ phiên 6. Patch
file đính kèm (handoff_phien7_v4_keep_log_uart_alive.patch) chứa thay
đổi CHƯA TEST XONG HOÀN CHỈNH (đã flash, đã sửa lỗi tự-làm-mù-log,
NHƯNG CHƯA CÓ KẾT QUẢ WAKE THÀNH CÔNG XÁC NHẬN) — xem mục "VIỆC ĐANG
LÀM DỞ" bên dưới trước khi áp dụng.

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
Datasheet đầy đủ trong Documents/ — LUÔN tra cứu trước khi đoán thông
số. Log thật/phép đo tay LUÔN thắng datasheet khi có xung đột.
Nếu gặp lại lỗi "could not read Username for 'https://github.com'"
khi cố git push từ container — giới hạn môi trường đã biết, KHÔNG
PHẢI lỗi mới. Xuất patch file qua present_files và nhờ người dùng tự
đồng bộ.
CLI shell có sẵn lệnh "settings -c -sleep [seconds]" để đổi STOP-mode
sleep duration khi test mà KHÔNG cần build lại — dùng cái này để test
nhanh chu kỳ wake, không cần sửa code (xem shell_commands.c).

============================================================
TÌNH TRẠNG TỔNG QUAN ĐẦU PHIÊN 7 — ĐÃ XONG, ĐỪNG ĐỘNG VÀO
============================================================
1. Sleep/wake cycle test_sleep.c: ỔN ĐỊNH (từ phiên 3-5, không đổi).
2. app.c: publish OK lên "hanoi/air_quality/data/001", broker host đã
   đúng qua CLI settings -c -host broker.hivemq.com.
3. Bug modem_power_off hang: đã fix (người dùng tự làm, phiên 6),
   KHÔNG liên quan gì tới các việc dưới đây.
4. W25Q128 (external flash) sleep_step: ĐÃ XONG HOÀN CHỈNH, ĐÃ TEST
   TRÊN BOARD THẬT, ĐÃ PUSH LÊN GIT từ phiên 6. Đi qua
   sx_storage_sleep()/sx_storage_wake() → sx_W25Q128_sleep_on()/
   sleep_off()(&s_w25q128). Xem chi tiết đầy đủ lịch sử debug (2 vòng,
   dead field board.q128 vs static s_w25q128 thật) ở handoff phiên 6
   nếu cần — KHÔNG lặp lại ở đây vì đã ổn định, không đổi gì thêm
   trong phiên 7.

============================================================
BỐI CẢNH: ĐIỀU TRA DÒNG RÒ ~28-31mA TRONG STOP MODE (bắt đầu phiên 6,
tiếp tục xuyên suốt phiên 7)
============================================================
Baseline đo được (từ đầu, không đổi qua các phiên):
- Full-power (chạy bình thường, không sleep): 120mA
- STOP mode TRƯỚC MỌI FIX của phiên 6-7: 50-56mA
- Baseline mong muốn (reset-button, erase chip, không code gì): 25mA
=> Mục tiêu: đưa STOP mode về gần 25mA.

**ĐÃ LOẠI TRỪ (điều tra phiên 6, đối chiếu datasheet, KHÔNG cần xem
lại trừ khi có bằng chứng mới mâu thuẫn):**
1. PWR regulator mode (LOWPOWERREGULATOR_ON + STOPENTRY_WFI) — đúng.
2. RX8130CE (RTC ngoại) — IDD nA-level, không đáng kể.
3. GPS (GP-02) — dùng Shutdown mode qua chân N/F, đúng thiết kế.
4. BNO055 (IMU) — Suspend mode qua bno055_set_pwr_mode(), đúng.
5. ADS1115 — single-shot tự power-down, không cần sleep_step riêng.
6. SHT3x — idle IDD 0.2-2.0µA, không cần sleep_step riêng.
7. I2C1 peripheral clock trong STOP mode qua PCLK1 — STOP mode tự
   dừng SYSCLK/HCLK/PCLK nên I2C1 tự mất clock, không cần can thiệp
   (nhưng xem mục quan trọng bên dưới — kết luận NÀY VẪN ĐÚNG cho
   HCLK/PCLK gating, KHÔNG mâu thuẫn với phát hiện phiên 6 cuối kỳ về
   RCC clock-ENABLE BIT riêng của từng peripheral, là chuyện khác).

**GIẢ THUYẾT ĐÃ THỬ VÀ SAI (phiên 6, đầu phiên 7) — ĐỪNG LÀM LẠI:**
- v0: tắt HSE/PLL1/HSI48 trước STOP (lý luận: STOP không tự tắt
  oscillator gốc). SAI theo ablation test của người dùng — xem mục
  dưới. Đã revert hoàn toàn, không còn trong code.

**ẢNH NGHIỆM QUYẾT ĐỊNH CỦA NGƯỜI DÙNG (giữa phiên 6, ĐỪNG NGHI NGỜ
LẠI, đã đủ bằng chứng):**
- Erase full chip: 25mA (baseline sạch).
- Full MX_*_Init() (11 module: GPIO/I2C1/SPI1/USART1/2/3/ICACHE/
  LPTIM1/RTC/TIM1/UART4/5/USART6) + app chạy full (kể cả qua sleep
  path): 56.3-56.6mA.
- Comment HẾT app_init/board_init/app_process NHƯNG GIỮ NGUYÊN toàn
  bộ MX_*_Init() trong main(): VẪN 56.3mA — không đổi.
- Comment THÊM cả MX_*_Init(): về 25.4mA.
=> Kết luận chắc chắn: ~31mA rò đến từ chính việc 11 module trên được
   init ở main() và KHÔNG BAO GIỜ được tắt/disable trước STOP mode —
   không liên quan gì đến HSE/PLL/app logic/sleep logic. STOP mode
   chỉ gate SYSCLK/HCLK/PCLK ở mức core, KHÔNG tự gate từng RCC
   clock-enable bit riêng của từng peripheral (RCC_APBxENR/AHBxENR).

============================================================
CÁC VÒNG FIX ĐÃ THỬ TRONG _enter_stop() (sx_sleep.c) — LỊCH SỬ ĐẦY ĐỦ
============================================================
File: SynaptiX_FDK/components/peripherals/sleep/sx_sleep.c

**v1 (revert): __HAL_RCC_xxx_CLK_DISABLE() thô cho I2C1/SPI1/6 UART/
TIM1/LPTIM1/ICACHE, không đụng GPIO (lý do không đụng GPIO xem dưới),
không đụng RTC (wake source). Kết quả đo: 56mA → 50mA. Không đủ.
Nguyên nhân thiếu sót: CLEAR_BIT chỉ cắt clock, KHÔNG đổi GPIO pin
mode. I2C1's SDA/SCL (PB6/PB7) là GPIO_MODE_AF_OD + NOPULL, dựa vào
pull-up NGOÀI THẬT trên board (NGƯỜI DÙNG ĐÃ XÁC NHẬN CÓ PULL-UP
NGOÀI TRÊN I2C1). Cắt clock mà để pin ở AF_OD "chết nửa vời" → nếu
SDA/SCL đang ở mức LOW lúc cắt (giữa giao dịch dở, hoặc slave clock-
stretch) → pin đứng yên LOW suốt STOP trong khi pull-up ngoài cố kéo
lên HIGH → dòng rò liên tục qua điện trở pull-up.

**v2: thay CLEAR_BIT bằng HAL_I2C_DeInit(&hi2c1)/HAL_SPI_DeInit(&hspi1)/
HAL_UART_DeInit(&huart1..6). Đã tra thật HAL_I2C_MspDeInit() trong
Core/Src/i2c.c: xác nhận nó tự CLK_DISABLE VÀ tự HAL_GPIO_DeInit() đưa
pin về analog input (mode rò dòng thấp nhất theo datasheet reset-
state). Cùng pattern xác nhận cho SPI1 và cả 6 UART (CubeMX sinh sẵn
MspDeInit đúng chuẩn). Kết quả đo LẦN ĐẦU (người dùng quên chưa flash,
sau khi flash đúng bản): 24mA — GẦN ĐẠT MỤC TIÊU 25mA baseline!

**VẤN ĐỀ MỚI PHÁT SINH SAU v2: KHÔNG WAKE LẠI ĐƯỢC (hoặc treo, chưa
rõ — xem mục "VIỆC ĐANG LÀM DỞ" bên dưới, ĐÂY LÀ TRỌNG TÂM PHIÊN 7).**
Log dừng đúng tại:
  [INFO]SX_SLEEP_SVC : >>> Entering STOP mode NOW
...và không có gì thêm, kể cả sau khi chờ đủ 300s (RTC period lúc đó).
Dòng đo nhảy TỪ 24mA LÊN LẠI ~55mA sau đó (không rõ tại thời điểm
nào chính xác — CHƯA ĐO ĐƯỢC MỐC THỜI GIAN CHÍNH XÁC KHI DÒNG NHẢY
LÊN, chỉ biết là "vẫn treo y hệt, không log gì thêm" theo lời người
dùng).

**v3 (đã thử, KHÔNG rõ có giải quyết được gì không, vì bị che lấp bởi
lỗi debug ở dưới): nghi ngờ ban đầu là SX_RESUME_TICS() (HAL_ResumeTick())
đặt SAI VỊ TRÍ — nó nằm SAU toàn bộ khối MX_*_Init(), nhưng
HAL_UART_Init() (gọi bởi MX_USARTx_Init) kết thúc bằng
UART_CheckIdleState() → UART_WaitOnFlagUntilTimeout(), dùng
HAL_GetTick() để tính timeout. ĐÃ TRA THẬT trong
Drivers/STM32H5xx_HAL_Driver/Src/stm32h5xx_hal_uart.c dòng ~3433-3480,
xác nhận cơ chế timeout dùng HAL_GetTick() thật, và nếu SysTick chưa
resume, HAL_GetTick() đứng yên → về lý thuyết có thể treo vô hạn chờ
flag TEACK/REACK. Đã sửa: chuyển SX_RESUME_TICS() lên NGAY SAU
SystemClock_Config(), TRƯỚC khối MX_*_Init(). Người dùng báo "vẫn
không được" sau khi flash bản này — NHƯNG (xem v4) có khả năng cao
đây KHÔNG PHẢI vì v3 sai, mà vì cùng lúc đó UART6 (log console) VẪN
đang bị DeInit ở đầu _enter_stop(), nên KHÔNG THỂ PHÂN BIỆT được
"code treo thật" với "code chạy đúng nhưng tự tắt mất kênh log của
chính mình". v3's fix (thứ tự resume tick) CÓ THỂ ĐÚNG HOẶC KHÔNG ĐÚNG
— CHƯA CÓ CÁCH XÁC NHẬN VÌ BỊ v4's vấn đề che lấp. Đừng revert v3 vội,
nhưng cũng đừng coi nó là đã xác nhận đúng.

**v4 (VỪA SỬA XONG PHIÊN 7, CHƯA FLASH, CHƯA CÓ KẾT QUẢ — ƯU TIÊN SỐ 1
ĐẦU PHIÊN 8): phát hiện quan trọng — tra sx_board.c dòng ~18:
  static UART_HandleTypeDef *hal_uart[6] =
      {&huart1, &huart2, &huart3, &huart4, &huart5, &huart6};
      // lte, gps, rs485, dust-sensor, extend-uart, log
huart6 CHÍNH LÀ UART LOG CONSOLE (xác nhận thêm qua sx_board.c's
sx_board_init(): logger_init(LOGGER_DEBUG, log_print) dùng
board.log_uart, với uart_config[UART_LOG].pDriver = hal_uart[UART_LOG],
và UART_LOG là phần tử cuối cùng = index 5 = huart6).
v1/v2/v3 ĐỀU CÓ HAL_UART_DeInit(&huart6) trong danh sách tắt trước
STOP — nghĩa là MỌI LẦN TEST TRƯỚC ĐÓ, code tự tắt luôn kênh log của
chính nó ngay trước khi vào STOP. "Không thấy log sau khi vào STOP"
KHÔNG THỂ dùng làm bằng chứng "code bị treo" nữa kể từ v2 trở đi —
board có thể đã chạy hoàn toàn bình thường (vào STOP, wake, resume)
nhưng đơn giản là không còn kênh nào để in log ra nữa.
ĐÃ SỬA: loại bỏ HAL_UART_DeInit(&huart6) và MX_USART6_UART_Init()
khỏi luồng sleep — giữ UART6 (log) sống xuyên suốt STOP mode. Patch
đính kèm (handoff_phien7_v4_keep_log_uart_alive.patch) — ĐÃ XUẤT,
CHƯA CÓ XÁC NHẬN NGƯỜI DÙNG ĐÃ FLASH HAY CHƯA, CHƯA CÓ KẾT QUẢ ĐO.

============================================================
NỘI DUNG CODE HIỆN TẠI TRONG _enter_stop() SAU v4 (tóm tắt, xem patch
đính kèm để có diff đầy đủ, chính xác từng dòng)
============================================================
Trước STOP (thứ tự):
  1. pre_stop_hook (board/app tự quiesce thứ họ cần, không đổi)
  2. SCB->ICSR pending-systick-clear (không đổi, có từ trước)
  3. HAL_I2C_DeInit(&hi2c1)
  4. HAL_SPI_DeInit(&hspi1)
  5. HAL_UART_DeInit(&huart1) -- LTE
  6. HAL_UART_DeInit(&huart2) -- GPS
  7. HAL_UART_DeInit(&huart3) -- RS485
  8. HAL_UART_DeInit(&huart4) -- dust sensor (SPS30)
  9. HAL_UART_DeInit(&huart5) -- extend
     [huart6 KHÔNG đụng — đây là log, giữ sống]
  10. __HAL_RCC_TIM1_CLK_DISABLE()
  11. __HAL_RCC_LPTIM1_CLK_DISABLE()
  12. HAL_ICACHE_Disable()
  13. SX_SUSPEND_TICS() (HAL_SuspendTick())
  14. s_enter_stop(...) = HAL_PWR_EnterSTOPMode(..., WFI) — vào STOP

Sau wake (thứ tự, ĐÃ SỬA Ở v3 — SX_RESUME_TICS lên trước MX_*_Init):
  1. SystemClock_Config()
  2. SX_RESUME_TICS() (HAL_ResumeTick()) -- ĐÃ CHUYỂN LÊN ĐÂY (v3 fix)
  3. MX_I2C1_Init()
  4. MX_SPI1_Init()
  5. MX_USART1_UART_Init() -- LTE
  6. MX_USART2_UART_Init() -- GPS
  7. MX_USART3_UART_Init() -- RS485
  8. MX_UART4_Init() -- dust
  9. MX_UART5_Init() -- extend
     [MX_USART6_UART_Init() KHÔNG gọi lại — không cần vì huart6 không
     bị DeInit ở trên]
  10. MX_TIM1_Init()
  11. MX_LPTIM1_Init()
  12. MX_ICACHE_Init()
  13. post_wake_hook

**Deliberately KHÔNG đụng (giữ nguyên từ v1, lý do vẫn còn đúng):**
- RTC: wake source xác nhận qua HAL_RTCEx_SetWakeUpTimer_IT
  (Core/Src/rtc.c), KHÔNG được tắt.
- GPIO (mọi port, GPIOx clock): MX_GPIO_Init() (Core/Src/gpio.c) chủ
  động ghi GPIO_PIN_RESET cho GPS_CPW/GPS_RST/SPI1_CS/EN_PW_DUST/
  I2C1_RST/LTE_RST/LTE_PWR_KEY. Gọi lại MX_GPIO_Init() sau wake sẽ
  ÂM THẦM reset các thiết bị ngoài (modem, GPS...) về trạng thái boot
  bất kể app đang giữ pin ở mức nào — RỦI RO THẬT, không lý thuyết.
  Cắt GPIOx clock cũng rủi ro vì pin đang driven (SPI1_CS, LTE_PWR_KEY)
  có thể float trong STOP thay vì giữ mức. Dòng rò từ GPIO clock-enable
  bit tự thân không đáng kể so với ~31mA đã đo — không đáng risk này.

============================================================
VIỆC ĐANG LÀM DỞ — ƯU TIÊN SỐ 1 TUYỆT ĐỐI ĐẦU PHIÊN 8
============================================================
1. **Flash bản v4** (patch đính kèm handoff_phien7_v4_keep_log_uart_alive.patch)
   NẾU người dùng chưa kịp làm trong lúc chat với phiên 7 (kiểm tra kỹ
   qua hỏi han, ĐỪNG GIẢ ĐỊNH).
2. Nhờ người dùng dùng CLI "settings -c -sleep 20" (hoặc số giây ngắn)
   để test nhanh chu kỳ wake thay vì chờ 300s mỗi lần — lệnh này ĐÃ
   CÓ SẴN, không cần code gì thêm (xem shell_commands.c dòng ~213-219,
   gọi network_config_set_sleep_ms()).
3. Xem log ĐẦY ĐỦ sau khi flash bản v4: bây giờ log KHÔNG bị tắt giữa
   chừng nữa (huart6 sống xuyên suốt), nên nếu vẫn không thấy
   "<<< Woke from STOP mode" (log này có sẵn ở
   SynaptiX_FDK/services/sleep_service/sx_sleep_service.c dòng ~81,
   ngay sau sx_sleep_enter_stop() return) thì ĐÂY MỚI LÀ BẰNG CHỨNG
   THẬT về treo, đáng tin cậy để điều tra tiếp — khác với log phiên 6
   cuối kỳ (v1-v3) vốn không đáng tin vì tự tắt log.
4. NẾU v4 flash xong và board wake được bình thường + log
   "<<< Woke from STOP mode" xuất hiện + dòng tiêu thụ vẫn ~24-25mA
   (có thể nhỉnh hơn 1 chút vì giữ 1 UART sống, chấp nhận được) → COI
   NHƯ ĐÃ XONG, xác nhận lại 1-2 chu kỳ nữa cho chắc rồi COMMIT+PUSH.
5. NẾU v4 flash xong mà VẪN treo y hệt (log vẫn dừng ở "Entering STOP
   mode NOW", dù giờ log KHÔNG bị tắt nữa) → đây là bằng chứng THẬT,
   nghĩa là code THẬT SỰ treo, không phải do tự tắt log. Nghi vấn tiếp
   theo theo thứ tự ưu tiên:
   a. Có thể treo NGAY TRONG WFI, chưa bao giờ wake — kiểm tra RTC
      wakeup timer có thực sự set đúng không (log "SetWakeUpTimer
      ret=0 counter=299" cho thấy set OK, ret=0 = HAL_OK, nhưng CHƯA
      CHẮC nghĩa là interrupt sẽ thực sự bắn — có thể NVIC chưa enable
      đúng cho RTC WKUP IRQ sau khi các DeInit() phía trên chạy, dù lý
      thuyết RTC không bị đụng tới. Cần kiểm tra kỹ NVIC priority/
      enable cho RTC_WKUP_IRQn có bị ảnh hưởng gián tiếp bởi bất kỳ
      DeInit nào ở trên không — CHƯA TRA, cần làm đầu phiên 8).
   b. Có thể treo trong 1 trong các HAL_UART_DeInit()/HAL_SPI_DeInit()/
      HAL_I2C_DeInit() TRƯỚC KHI VÀO WFI (tức là chưa bao giờ thực sự
      vào STOP mode) — nếu đúng, dòng đo sẽ KHÔNG giảm xuống 24mA nữa
      mà đứng yên ở mức RUN cao hơn NGAY LẬP TỨC sau "Entering STOP
      mode NOW", KHÔNG đợi tới 300s. NGƯỜI DÙNG CẦN XÁC NHẬN: dòng có
      giảm xuống ~24mA trước rồi mới nhảy lên lại sau ~300s, hay là
      đứng yên ở mức cao NGAY LẬP TỨC không hề giảm? ĐÂY LÀ CÂU HỎI
      QUAN TRỌNG NHẤT CHƯA HỎI KỊP TRONG PHIÊN 6-7, PHẢI HỎI ĐẦU
      PHIÊN 8 TRƯỚC KHI ĐOÁN TIẾP.
   c. Nếu (b) xác nhận đúng (không hề giảm dòng, tức treo TRƯỚC WFI):
      nghi ngờ cụ thể nhất là HAL_SPI_DeInit(&hspi1) — SPI1_CS_Pin
      (PC... xem gpio.c) là GPIO thường, KHÔNG rõ trạng thái lúc
      HAL_SPI_DeInit() chạy (xem mục 6 dưới, CHƯA XÁC NHẬN). Nếu CS
      đang ở mức SELECT (thường active-low, nên mức LOW = đang chọn
      W25Q128) và HAL_SPI_DeInit() cắt SPI1 clock/pin giữa chừng một
      giao dịch SPI dở dang với W25Q128 đang chờ phản hồi — có thể
      treo ở đâu đó trong SPI HAL nếu có busy-wait nào phụ thuộc phản
      hồi phần cứng không bao giờ tới. CHƯA XÁC NHẬN, CHỈ LÀ SUY LUẬN
      — cần đọc kỹ HAL_SPI_DeInit() thật (Drivers/.../stm32h5xx_hal_spi.c)
      xem nó có busy-wait dựa vào flag phần cứng nào không, giống cách
      đã tra HAL_UART_Init()'s UART_CheckIdleState() ở phiên 6.
6. **CHƯA XÁC NHẬN (tồn đọng từ phiên 6, vẫn treo đó)**: SPI1_CS_Pin
   có chắc chắn ở mức deselect (thường = HIGH cho active-low CS) TRƯỚC
   khi HAL_SPI_DeInit(&hspi1) chạy trong _enter_stop() không? Không
   tìm thấy nơi nào set SPI1_CS_Pin bằng code ngoài MX_GPIO_Init()
   (grep cả repo, chỉ thấy trong gpio.c) — có thể CS được quản lý qua
   hardware NSS tự động bởi SPI1 peripheral (hspi1.Init.NSS), CHƯA TRA
   KỸ CẤU HÌNH NÀY. Cần xem Core/Src/spi.c's MX_SPI1_Init() phần
   hspi1.Init.NSS = ??? để biết CS là software-managed hay
   hardware-managed — đây là việc CHƯA LÀM, ưu tiên nếu nghi vấn 5c ở
   trên được xác nhận.

============================================================
VIỆC CẦN LÀM SAU KHI GIẢI QUYẾT XONG TREO/WAKE (thứ tự ưu tiên, sau
mục "VIỆC ĐANG LÀM DỞ" ở trên)
============================================================
1. Nếu dòng vẫn chưa về sát 22mA sau khi fix xong treo — kiểm tra GPIO
   nào đang driven HIGH/LOW kéo dòng tĩnh qua external component
   (SPS30, ZE12A, pump) — CHƯA đối chiếu kỹ các module này với
   datasheet, chỉ mới đối chiếu GPS/IMU/ADS1115/SHT3x/RTC/W25Q128 ở
   phiên 6. Việc tồn đọng, ưu tiên sau khi treo/wake ổn định.
2. Việc tồn đọng từ phiên 5, CHƯA ĐỘNG TỚI Ở PHIÊN 6-7, VẪN CÒN ĐÓ:
   - Bug "read failed" nghi race-condition WAKING trong app.c's
     app_process() — accel_app_poll()/sx_temp_humi_poll() gọi vô điều
     kiện không có guard theo state, khác test_sleep.c đã có guard từ
     phiên 3.
   - Muốn giảm tạm HEARTBEAT_CYCLE_INTERVAL để test nhanh heartbeat
     publish — chưa hỏi lại người dùng.
   - Timestamp payload sai "2087-00-00T...", ACCEL_APP_FILTER_ALPHA
     chưa điều chỉnh theo period mới, log_debug -> log_info cho
     temp/humi (ưu tiên thấp, xem chi tiết ở handoff phiên 3-4).
3. Race-condition nhỏ (KHÔNG gây treo, chỉ log rối, ghi nhận từ phiên
   6): modem_power_off sleep_step và sx_mqtt.c's recovery ladder có vẻ
   cùng động vào modem gần như đồng thời — ưu tiên thấp.
4. CLI "settings -c -sleep [n]" đã xác nhận tồn tại và hoạt động
   (shell_commands.c), tận dụng để test nhanh thay vì chờ 300s mỗi
   chu kỳ trong suốt các phiên tới.

============================================================
FILE PATCH ĐÍNH KÈM PHIÊN NÀY
============================================================
handoff_phien7_v4_keep_log_uart_alive.patch — áp dụng bằng
`git apply` từ thư mục gốc SAU KHI re-clone sạch. Đây là patch CỘNG
DỒN từ đầu phiên 6 (bao gồm cả W25Q128 fix đã push rồi — NẾU git log
cho thấy W25Q128 fix đã có sẵn trên main thì patch này CÓ THỂ conflict
khi apply, cần kiểm tra kỹ bằng git apply --check trước, hoặc chỉ lấy
phần đổi trong sx_sleep.c bằng tay nếu cần).