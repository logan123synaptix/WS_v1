import serial
import time

# Thay COM3 bằng cổng COM của bạn
PORT = "COM3"
BAUDRATE = 115200

try:
    ser = serial.Serial(
        port=PORT,
        baudrate=BAUDRATE,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=1
    )

    print(f"Opened {PORT} @ {BAUDRATE} baud")

    while True:
        msg = b"Hello\r\n"
        ser.write(msg)
        ser.flush()

        print(f"TX: {msg.decode().strip()}")

        time.sleep(2)

except KeyboardInterrupt:
    print("\nStopped by user.")

except serial.SerialException as e:
    print(f"Serial error: {e}")

finally:
    if 'ser' in locals() and ser.is_open:
        ser.close()
        print("Serial port closed.")