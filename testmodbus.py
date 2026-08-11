from pymodbus.client import ModbusTcpClient

IP = "192.168.1.1"
PORT = 502

client = ModbusTcpClient(IP, port=PORT, timeout=3)

print(f"Connecting to {IP}:{PORT}...")

if not client.connect():
    print("Cannot connect")
    exit()

print("Connected!")

try:
    result = client.read_holding_registers(
        address=1024,
        count=10,
        device_id=1
    )

    print("Response:", result)

    if result.isError():
        print("Modbus error:", result)
    else:
        print("Registers:")

        for i, value in enumerate(result.registers):
            print(f"Register {1025 + i}: {value}")

finally:
    client.close()
    print("Connection closed")