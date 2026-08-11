from pymodbus.client import ModbusSerialClient

client = ModbusSerialClient(
    port="COM5",
    baudrate=9600,
    bytesize=8,
    parity="N",
    stopbits=1,
    timeout=2
)

print("Connecting...")

if client.connect():
    print("Serial connected!")

    result = client.read_holding_registers(
        address=1025,
        count=1,
        device_id=1
    )

    print("Response:", result)

    if not result.isError():
        print("Register:", result.registers)
    else:
        print("Modbus error:", result)

    client.close()

else:
    print("Cannot open COM port")