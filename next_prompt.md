HANDOFF — FOTA (Firmware Over-The-Air) QUA MẠNG DI ĐỘNG, WS_v1 (STM32H563RIV6)

Viết khi vừa chốt xong thiết kế, TRƯỚC KHI viết dòng code đầu tiên.
Đây là tài liệu tổng kết kiến trúc + việc cần làm tiếp cho phiên sau
(hoặc cho chính phiên này nếu tiếp tục). Đọc kỹ trước khi động code.

============================================================
BỐI CẢNH — TẠI SAO CÓ NHÁNH NÀY
============================================================
Hệ thống hiện tại (nhánh main) đã có sẵn OTA nhưng CHỈ qua USB DFU
(local, cắm dây). Comment trong code ghi rõ đây là quyết định có chủ
đích trước đó ("deliberately NO MQTT RPC path", theo user 2026-07-18).
Người dùng giờ chủ động đổi hướng, muốn thêm FOTA thật qua mạng di
động (modem A7677S). Đã checkout nhánh `ft/fota_ws` từ `main` để làm
việc này (tại thời điểm bắt đầu, nhánh này chỉ hơn main 2 dòng ở
Core/Src/main.c, chưa có code FOTA nào).

LƯU Ý: có 1 việc TỒN ĐỌNG từ phiên trước (phiên 8) — code GPS log
persistence + dọn macro flash partition (app.c/app_config.h/sx_fs.c)
đã bị MẤT do container reset trước khi kịp push. Người dùng đã XÁC
NHẬN CHỦ ĐÍCH BỎ, không cần làm lại. Không cần nhắc lại việc này ở
các phiên sau.

============================================================
HẠ TẦNG CÓ SẴN — ĐÃ XÁC NHẬN BẰNG CÁCH ĐỌC CODE THẬT
============================================================

1) Bootloader dual-bank (SynaptiX_FDK/BOOTLOADER_WS/)
   - Partition table trong internal flash STM32H5 (2MB, sector 8KB):
       0x08000000  bootloader        56 KB (7 sectors)
       0x0800E000  partition table    8 KB (1 sector)
       0x08010000  primary app      480 KB (60 sectors)  <- app hiện chạy ở đây
       0x08088000  secondary app    480 KB (60 sectors)  <- ĐÍCH GHI FOTA
       0x08100000  factory app      480 KB (60 sectors)
       0x08178000  scratch            8 KB (1 sector)
     Nguồn: BOOTLOADER_WS/bootloader/flash_define.h (đọc trực tiếp,
     không suy từ comment).
   - BootFlashPartition_t (boot_flash.h) — struct partition table:
     các field địa chỉ/size của primary/secondary/factory/scratch,
     isNewFirmwareAvailable, isUpgradeInProgress, magic_number
     (0xDEADBEEF). KHÔNG CÓ field checksum/CRC nào cho firmware.
   - Cờ điều khiển qua TAMP backup register (sống sót qua soft reset,
     không sống sót qua mất nguồn hoàn toàn):
       BKP0R = BOOT_MAGIC_UPDATE          (vào chế độ update)
       BKP1R = BOOT_MAGIC_ROLLBACK_PREV   (rollback về secondary)
       BKP2R = BOOT_MAGIC_ROLLBACK_FACTORY (rollback về factory,
               hoặc flash-factory nếu là BOOT_MAGIC_UPDATE_FACTORY)
   - shell_commands.c đã có sẵn lệnh: ota, rollback-prev,
     rollback-factory, flash-factory (chỉ dùng qua USB DFU hiện tại).

2) Luồng OTA hiện tại (ota_trigger.c) — QUAN TRỌNG, quyết định hướng
   thiết kế FOTA:
   - ota_trigger_enter_dfu() CHỈ làm 1 việc: đọc partition table hiện
     tại (read-modify-write, không đụng field khác), set
     isUpgradeInProgress = true, verify write, rồi NVIC_SystemReset().
   - App layer KHÔNG TỰ GHI 1 BYTE FIRMWARE NÀO vào Secondary trong
     luồng hiện tại. Việc ghi thực sự do tud_dfu_download_cb() trong
     bootloader làm, nhận dữ liệu qua USB DFU protocol (có checksum ở
     tầng USB DFU sẵn).
   - Hệ quả: FOTA qua mạng KHÔNG có sẵn hàm "ghi chunk firmware vào
     Secondary từ app layer" để tái dùng — phải viết mới hoàn toàn,
     và phải tự lo checksum vì không còn USB DFU protocol lo hộ.

3) sx_flash.h (components/peripherals/flash/) — API ghi flash nội bộ
   ở app layer, ĐÃ CÓ SẴN, dùng được ngay:
     sx_flash_read(addr, buf, len)
     sx_flash_write(addr, data, len)
     sx_flash_erase(addr, len)
     sx_flash_unlock() / sx_flash_lock()

4) Modem A7677S (a7677s.c/.h, components/modules/a76xx/)
   - Hiện TẠI CHỈ CÓ hạ tầng MQTT (init, APN, publish/subscribe
     callback). ĐÃ XÁC NHẬN: 0 dòng liên quan tới HTTP trong cả
     a7677s.c và a7677s.h — phải viết mới hoàn toàn tầng AT command
     HTTP.
   - Tra cứu Documents/a76xx_at_cmd.md (AT Command Manual thật, khác
     với Documents/a7677s.md chỉ là Hardware Design guide không có
     AT command) — modem CÓ hỗ trợ đầy đủ bộ HTTP(S):
       AT+HTTPINIT / HTTPTERM / HTTPPARA / HTTPACTION / HTTPHEAD /
       HTTPREAD / HTTPDATA / HTTPPOSTFILE / HTTPREADFILE
   - AT+HTTPPARA các tham số quan trọng đã tra được:
       URL, CONNECTTO (20-120s), RECVTO (2-120s), CONTENT, ACCEPT,
       SSLCFG, USERDATA (custom header, tối đa 256 ký tự — DÙNG CHỖ
       NÀY để tự chèn "Range: bytes=start-end" vì AT+HTTPPARA KHÔNG
       có tham số Range riêng), READMODE (0/1, =1 cho phép đọc lại
       cùng vị trí nhiều lần, giới hạn response phải <1MB).
   - AT+HTTPACTION=0 (GET): max response time 120000ms, trả về
     "+HTTPACTION:<method>,<statuscode>,<datalen>" — module tự tải
     toàn bộ về buffer nội bộ của nó trước.
   - AT+HTTPREAD=<start_offset>,<byte_size>: đọc dữ liệu đã tải từ
     buffer modem ra UART theo đoạn tùy chọn — đây là cơ chế dùng để
     đọc từng chunk rồi ghi flash, KHÔNG cần giữ cả file trong RAM
     MCU.
   - Giới hạn 480KB (kích thước Secondary) nằm dưới giới hạn ~1MB
     nói trên của modem — không vướng, nhưng 480KB là trần cứng thật
     sự phải validate trước khi bắt đầu tải (dựa theo field `size`
     server báo trước khi tải).

5) mqtt_rpc.c — ĐÃ XÁC NHẬN LỖ HỔNG THIẾT KẾ QUAN TRỌNG:
   - mqtt_clean_session = 1 hiện tại → mỗi lần connect lại (sau mỗi
     lần wake), broker xóa sạch session cũ. Gửi RPC lúc thiết bị đang
     ngủ (phần lớn thời gian, modem tắt nguồn hoàn toàn 20 phút mỗi
     chu kỳ) → lệnh mất vĩnh viễn, kể cả QoS 1.
   - ĐÃ CHỌN GIẢI PHÁP (xác nhận với người dùng): dùng MQTT retained
     message trên 1 topic riêng (ví dụ
     synaptix/demo/fota_check/001), thiết bị chủ động "pull" — tự
     check topic này mỗi lần thức (trong SENDING, sau khi publish
     telemetry) thay vì chờ server "push" đúng lúc đang thức. Không
     cần đổi clean_session.

============================================================
KIẾN TRÚC FOTA ĐÃ CHỐT (xác nhận từng bước với người dùng)
============================================================

Payload retained topic (server publish, ví dụ):
    synaptix/demo/fota_check/001
    { "version": "x.y.z", "url": "https://...", "size": N,
      "crc32": "0x..." }

Luồng thiết bị (state machine, gắn vào chu kỳ wake/sleep hiện có):

  Chu kỳ N (SENDING):
    1. Publish telemetry như bình thường.
    2. Check retained topic fota_check.
    3. Nếu version mới hơn đang chạy VÀ size <= 480KB (validate
       trước khi làm gì khác):
         -> set cờ update_pending (RAM, KHÔNG persist qua sleep vì
            modem tắt nguồn hoàn toàn nên không cần bền qua power
            cycle — chỉ cần bền qua 1 chu kỳ SLEEPING/WAKE bình
            thường, RAM đủ).
       KHÔNG tải ngay trong chu kỳ này (tách "phát hiện" khỏi "hành
       động" — lỗi tải giữa chừng ở chu kỳ N sẽ không làm hỏng việc
       gửi telemetry của chính chu kỳ N).
    4. Sleep 20 phút như bình thường.

  Chu kỳ N+1 (SENDING), NẾU update_pending = true:
    5. Publish telemetry xong như bình thường (ưu tiên dữ liệu đo
       trước).
    6. Bắt đầu tải FOTA:
       a. AT+HTTPINIT, AT+HTTPPARA "URL"=<url from server>.
       b. Với mỗi range 4-8KB (kích thước chunk cụ thể do phiên code
          quyết định khi bắt tay viết, cân bằng giữa số round-trip
          AT và bộ nhớ RAM tạm dùng để giữ 1 chunk trước khi ghi
          flash):
            - AT+HTTPPARA "USERDATA" = "Range: bytes=<start>-<end>"
            - AT+HTTPACTION=0 (GET)
            - AT+HTTPREAD=<offset trong chunk này>,<size>
            - sx_flash_erase() theo sector nếu là byte đầu của
              sector đó, rồi sx_flash_write() đoạn vừa đọc vào đúng
              offset trong SECONDARY_APP_FLASH_START_ADDRESS
              (0x08088000) + running_offset.
       c. Sau khi ghi đủ `size` byte: sx_flash_read() đọc lại TOÀN
          BỘ vùng vừa ghi trong Secondary, tính CRC32, so với
          `crc32` server cung cấp.
       d. Nếu KHỚP:
            - Gọi hàm kiểu ota_trigger_enter_dfu() (viết bản mới,
              read-modify-write y hệt pattern đã có) để set
              isUpgradeInProgress = true, verify write, rồi
              NVIC_SystemReset(). Bootloader boot thẳng vào Secondary
              (không cần chờ USB DFU vì cờ đã set sẵn) — ĐIỂM NÀY
              CẦN KIỂM TRA LẠI new_bootloader_check_commands()/
              bootloader_process() xem "chỉ set isUpgradeInProgress
              không kèm BKPxR command" có thực sự khiến bootloader tự
              swap Primary<->Secondary hay cần thêm bước gì khác —
              CHƯA ĐỌC KỸ PHẦN NÀY, xem mục "việc cần làm" bên dưới.
          Nếu KHÔNG khớp:
            - Log lỗi, xoá cờ update_pending, KHÔNG đụng Primary,
              giữ nguyên hoạt động bình thường.
            - Giới hạn số lần retry (con số cụ thể chưa chốt — đề
              xuất 3 lần rồi bỏ, tránh loop vô hạn tốn pin) — CHƯA
              CHỐT VỚI NGƯỜI DÙNG, cần hỏi khi code tới phần này.
    7. Sleep 20 phút như bình thường (dù tải FOTA thành công hay
       thất bại, trừ trường hợp reset ở bước 6d).

Quyết định checksum: KHÔNG sửa BootFlashPartition_t (không đụng
bootloader). CRC32 verify hoàn toàn ở app layer, trước khi set cờ
isUpgradeInProgress. Đây là lựa chọn người dùng đã chốt rõ ràng, ưu
tiên không đụng struct dùng chung giữa 2 project build riêng biệt.

============================================================
VIỆC CẦN LÀM TIẾP (CHƯA CODE DÒNG NÀO)
============================================================

Thứ tự đề xuất (dependency tự nhiên, không bắt buộc phải đúng thứ
tự này nếu người dùng muốn đổi):

1. [CHƯA LÀM] Đọc kỹ new_bootloader_check_commands() +
   bootloader_process() trong BOOTLOADER_WS để xác nhận: chỉ set
   isUpgradeInProgress=true (không kèm BKP0R/1R/2R nào) có đủ để
   bootloader tự nhận diện + swap Primary<->Secondary khi boot lại
   không, hay cần thêm hành động khác. ĐÂY LÀ VIỆC ĐẦU TIÊN CẦN LÀM
   Ở PHIÊN TIẾP THEO — chưa đọc kỹ phần này, không được giả định.

2. [CHƯA LÀM] Viết module HTTP mới cho a7677s (ví dụ file mới
   a7677s_http.c/.h hoặc thêm hàm vào a7677s.c hiện có — quyết định
   khi bắt tay code):
     - Wrapper cho AT+HTTPINIT/TERM/PARA/ACTION/READ.
     - Hàm chèn Range header qua USERDATA.
     - Test thực tế với 1 file thật cỡ vài trăm KB TRƯỚC khi tích
       hợp vào flow FOTA đầy đủ, vì giới hạn buffer nội bộ modem cho
       multi-range liên tiếp CHƯA được test thực tế (chỉ có số liệu
       từ datasheet, có thể khác hành vi thật — theo quy tắc dự án,
       log/đo thật luôn thắng datasheet khi xung đột).

3. [CHƯA LÀM] Sửa mqtt_rpc.c: thêm subscribe + parse retained topic
   fota_check. Xác định tên topic thật (ví dụ trên chỉ là gợi ý, cần
   thống nhất với format các topic khác đang dùng trong hệ thống —
   CHƯA XEM CÁC TOPIC KHÁC ĐANG DÙNG QUY ƯỚC GÌ, nên tra cứu trước
   khi đặt tên topic mới).

4. [CHƯA LÀM] Viết file mới fota.c/.h: state machine tải theo range
   + ghi flash + CRC32 verify + gọi trigger reset. Bao gồm:
     - Validate size <= 480KB trước khi bắt đầu tải.
     - Hàm trigger reset mới (biến thể của ota_trigger_enter_dfu(),
       không gọi thẳng hàm cũ vì hàm cũ không có bước ghi flash nào
       trước đó — cần hàm riêng hoặc refactor hàm cũ để dùng chung
       phần "set cờ + verify + reset").
     - Quyết định số lần retry khi CRC32 sai (đề xuất 3, CHƯA CHỐT).

5. [CHƯA LÀM] Sửa app.c: gọi check FOTA sau WAIT_PUBLISH, trước khi
   chuyển sang SLEEPING. Thêm biến trạng thái update_pending vào
   state machine hiện có (chỗ đặt cụ thể — struct nào, tùy theo cấu
   trúc app.c thật, cần xem lại code app.c hiện tại khi bắt tay,
   KHÔNG suy đoán trước).

6. [CHƯA LÀM] Định nghĩa format server-side cho retained message
   (version, url, size, crc32) — hiện chỉ là đề xuất, CHƯA XÁC NHẬN
   với người dùng đây có phải format cuối cùng hay cần thêm/bớt
   field (ví dụ: có cần field "mandatory" để phân biệt bản bắt buộc
   update và bản optional không?).

============================================================
QUY TẮC BẮT BUỘC (kế thừa từ các phiên trước, không đổi)
============================================================
- RE-CLONE đầu phiên: git clone
  https://github.com/logan123synaptix/WS_v1.git, checkout ft/fota_ws.
- KHÔNG tin log/mô tả cũ mà không tự đọc lại code thật. Container
  reset giữa phiên — MỌI THAY ĐỔI CHƯA COMMIT/PUSH ĐÃ MẤT nếu không
  kịp đưa cho người dùng trước khi hết token.
- Không sửa code âm thầm — trình bày nghi vấn → hỏi → chỉ sửa sau
  khi có xác nhận rõ ràng.
- Comment code tiếng Anh, trao đổi tiếng Việt.
- KHÔNG có compiler thật trong container — không build được. Người
  dùng tự build + flash + gửi log qua chat.
- Board test vật lý duy nhất: STM32H563RIV6.
- Datasheet đầy đủ trong Documents/ — LUÔN tra cứu trước khi đoán
  thông số (lưu ý: Documents/a7677s.md là Hardware Design guide,
  KHÔNG có AT command; AT command manual thật là
  Documents/a76xx_at_cmd.md — dùng nhầm file sẽ không tìm thấy gì).
  Log thật/phép đo tay LUÔN thắng datasheet khi có xung đột.
- KHÔNG dùng present_files/xuất patch file — trình chiếu code trực
  tiếp trong chat bằng view/tool xem file, để người dùng tự copy.
  Container không có git credential để tự push.
- TRƯỚC KHI XOÁ/ĐỔI TÊN MACRO: luôn grep toàn bộ codebase để xác
  nhận macro có đang thực sự được dùng ở đâu không.
- KHI DEBUG PHẦN CỨNG: mỗi giả thuyết cần 1 phép đo/log cụ thể để
  xác nhận hoặc loại trừ TRƯỚC khi chuyển sang giả thuyết kế tiếp.