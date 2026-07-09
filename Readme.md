Chào bạn.
Đây là dự án weather station v2 (được update dùng mcu stm32h563riv6), repo v2: "https://github.com/logan123synaptix/WS_v1.git"
Còn đây là repo mẫu v1: "https://github.com/logan123synaptix/WS_v0.git"

Ở repo mẫu dùng chip stm32h523ccu6, và dùng rtos và chưa có tối ưu năng lượng.

Ở repo mới chúng ta sẽ dùng stm32h563riv6 với có mode tối ưu năng lượng đồng thời ko dùng rtos và có các ngoại vi như sau: 6 cổng uart, 1 i2c, 1 spi, usb

- Hệ thống sẽ gồm các module (các tài liệu datasheet tôi đã convert sang mark down để bạn dễ đọc và nó nằm trong Documents):
1. a7677s: để giúp module kết nối mạng, bắn bản tin mqtt, dùng uart1
2. Cảm biến nhiệt độ, độ ẩm: SHT3x (repo mới chưa có, nhưng repo cũ đã có code mẫu nhé) dùng I2C1
3. RTC: RTC RX8130CE (chúng ta đã có code), dùng bus I2C1
4. IMU: BNO055 (chúng ta đã có code) cũng dùng I2C1
5. GPS: GP02 dùng UART2 (baud 9600)
6. 1 cái RS485 dùng UART3 (baud 115200)
7. Cảm biến bụi bặm dust sensor : SPS30 dùng uart 4
8. Cảm biến ze12a dùng uart 5
9. 1 log để debug dùng uart 6
10. 1 con external flash w25q128 dùng spi để ghi log (tôi đã code rồi)

- Ở phiên bản hiện tại tôi đang dùng con sim7680 (do tôi tận dụng 1 project trước đó dùng chip stm32h563riv6, giờ bạn có thể tái cấu trúc sim để phục vụ prj hiện tại dùng a7677s)

- trước tiên bạn hãy đọc prj để hiểu và cùng tôi xây dựng nốt architecture của prj ver sau