#!/usr/bin/env python3
"""
Test nhanh module khí ZE12A (SO2/NO2/O3) qua USB-TTL, không cần board
STM32/mux — cắm thẳng USB-TTL vào module cần test (Active Upload mode
mặc định của module sẽ tự gửi 1 frame mỗi giây, không cần lệnh gì).

Dây nối (USB-TTL <-> ZE12A):
    TTL TX  -> module RX
    TTL RX  -> module TX
    TTL GND -> module GND
    Module VDD theo datasheet (không lấy từ USB-TTL nếu nó không đủ dòng/áp)

Cách dùng:
    python ze12a_test.py COM5          (Windows)
    python ze12a_test.py /dev/ttyUSB0  (Linux)

Theo datasheet ZE12A (Documents/ze12a-electrochemical-module-manual-v1_0.md):
  - Baudrate 9600, 8N1.
  - Default = Active Upload mode: module tự gửi 1 frame/giây, không cần hỏi.
  - Active Upload frame (9 byte, table trang manual):
      [0]=0xFF start, [1]=gas code, [2]=unit(0x04=ug/m3?), [3]=no_decimal,
      [4]=conc high, [5]=conc low, [6]=full_measure high, [7]=full_measure low,
      [8]=checksum
  - Checksum: bù 2 (two's complement) của tổng byte[1..6] (bỏ byte[0] start
    và chính byte[8] checksum) - đúng công thức FucCheckSum() trong manual.
  - Gas code: SO2=0x2B, NO2=0x2C, O3=0x2A (khớp ze12a.h trong firmware).

Script này KHÔNG gửi lệnh chuyển mode gì cả (không cần, module tự gửi ở
Active Upload theo mặc định) - nếu module đang bị kẹt ở Q&A mode từ lần
trước (xem cờ --force-active bên dưới), sẽ không thấy gì; dùng
--force-active để gửi lệnh chuyển về Active Upload trước khi lắng nghe.
"""

import sys
import time
import argparse

try:
    import serial
except ImportError:
    print("Thiếu pyserial. Cài bằng: pip install pyserial")
    sys.exit(1)

BAUDRATE = 9600
FRAME_LEN = 9
START_BYTE = 0xFF

GAS_NAMES = {
    0x2A: "O3",
    0x2B: "SO2",
    0x2C: "NO2",
    0x04: "CO",   # không dùng trên board này, chỉ để nhận diện nếu đọc nhầm module khác
    0x03: "H2S",  # tương tự
}

# table 6 (manual): chuyển sang Active Upload mode
CMD_SWITCH_ACTIVE = bytes([0xFF, 0x01, 0x78, 0x40, 0x00, 0x00, 0x00, 0x00, 0x47])
# table 5 (manual): chuyển sang Q&A mode (không dùng trong script này, để tham khảo)
CMD_SWITCH_QA = bytes([0xFF, 0x01, 0x78, 0x41, 0x00, 0x00, 0x00, 0x00, 0x46])
# table 7 (manual): hỏi nồng độ khí (dùng nếu module đang ở Q&A mode)
CMD_READ_QA = bytes([0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79])


def checksum(frame_without_checksum_byte):
    """FucCheckSum() trong manual: bù 2 của tổng byte[1..7] (7 byte, bỏ
    byte[0] start và byte[8] checksum). Đã verify khớp cả 2 ví dụ số liệu
    có sẵn trong datasheet (table 6 command frame và table 8 response
    frame) trước khi dùng trong script này.
    frame_without_checksum_byte phải có đúng 8 byte (byte 0..7)."""
    total = sum(frame_without_checksum_byte[1:8]) & 0xFF
    return (~total + 1) & 0xFF


def parse_frame(frame):
    """Trả về dict {gas_name, gas_code, conc, unit_raw, checksum_ok} hoặc None
    nếu không đúng định dạng."""
    if len(frame) != FRAME_LEN:
        return None
    if frame[0] != START_BYTE:
        return None

    expected_cs = checksum(frame[:8])
    checksum_ok = (expected_cs == frame[8])

    gas_code = frame[1]
    unit_raw = frame[2]
    conc = (frame[4] << 8) | frame[5]

    return {
        "gas_code": gas_code,
        "gas_name": GAS_NAMES.get(gas_code, f"unknown(0x{gas_code:02X})"),
        "unit_raw": unit_raw,
        "conc": conc,
        "checksum_ok": checksum_ok,
        "raw": frame.hex(" "),
    }


def main():
    ap = argparse.ArgumentParser(description="Test nhanh ZE12A qua USB-TTL")
    ap.add_argument("port", help="Cổng serial, vd COM5 hoặc /dev/ttyUSB0")
    ap.add_argument("--timeout", type=float, default=20.0,
                     help="Tổng thời gian lắng nghe (giây), mặc định 20s")
    ap.add_argument("--force-active", action="store_true",
                     help="Gửi lệnh chuyển về Active Upload mode trước khi lắng nghe "
                          "(dùng nếu nghi module đang kẹt ở Q&A mode)")
    args = ap.parse_args()

    try:
        ser = serial.Serial(args.port, BAUDRATE, bytesize=8, parity="N",
                             stopbits=1, timeout=0.5)
    except serial.SerialException as e:
        print(f"Không mở được cổng {args.port}: {e}")
        sys.exit(1)

    print(f"Đã mở {args.port} @ {BAUDRATE} 8N1")

    if args.force_active:
        print("Gửi lệnh chuyển sang Active Upload mode (table 6)...")
        ser.write(CMD_SWITCH_ACTIVE)
        time.sleep(0.3)
        # Module không bắt buộc phản hồi ngay cho lệnh set mode theo manual,
        # nên không chờ response ở đây - chỉ gửi rồi bắt đầu lắng nghe bình thường.

    print(f"Đang lắng nghe frame Active Upload trong {args.timeout:.0f}s "
          f"(module tự gửi 1 frame/giây theo mặc định, không cần hỏi)...\n")

    buf = bytearray()
    start = time.time()
    frame_count = 0
    ok_count = 0
    bad_checksum_count = 0
    last_byte_time = None

    while time.time() - start < args.timeout:
        chunk = ser.read(64)
        if chunk:
            last_byte_time = time.time()
            buf.extend(chunk)

            # Tìm và bóc tách frame 9 byte bắt đầu bằng 0xFF trong buffer
            while True:
                # Bỏ rác trước byte start nếu có
                idx = buf.find(bytes([START_BYTE]))
                if idx == -1:
                    buf.clear()
                    break
                if idx > 0:
                    del buf[:idx]
                if len(buf) < FRAME_LEN:
                    break

                frame = bytes(buf[:FRAME_LEN])
                info = parse_frame(frame)
                del buf[:FRAME_LEN]

                if info is None:
                    continue

                frame_count += 1
                ts = time.strftime("%H:%M:%S")
                if info["checksum_ok"]:
                    ok_count += 1
                    print(f"[{ts}] OK   gas={info['gas_name']:<12} "
                          f"conc={info['conc']:5d}  raw={info['raw']}")
                else:
                    bad_checksum_count += 1
                    print(f"[{ts}] BAD CHECKSUM  raw={info['raw']}")

    ser.close()

    print("\n" + "=" * 60)
    print(f"Tổng kết sau {args.timeout:.0f}s:")
    print(f"  Frame hợp lệ (checksum đúng) : {ok_count}")
    print(f"  Frame checksum sai           : {bad_checksum_count}")
    if last_byte_time is None:
        print("  KHÔNG NHẬN ĐƯỢC BYTE NÀO CẢ - kiểm tra dây TX/RX/GND, "
              "nguồn VDD module, hoặc baudrate/cổng COM đã đúng chưa.")
    elif frame_count == 0:
        print("  Có nhận byte nhưng không ghép được frame hợp lệ nào - "
              "có thể dây TX/RX bị đấu ngược, hoặc nhiễu tín hiệu.")
    print("=" * 60)


if __name__ == "__main__":
    main()