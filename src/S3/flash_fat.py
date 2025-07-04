import serial.tools.list_ports
import subprocess

# List all available COM ports
ports = list(serial.tools.list_ports.comports())
print("Available COM ports:")
for i, port in enumerate(ports):
    print(f"{i}: {port.device} ({port.description})")

# Let user select port
idx = int(input("Select the COM port number: "))
selected_port = ports[idx].device

# Command template (edit the path to fatfs.bin as needed)
cmd = [
    "python", "-m", "esptool",
    "--port", selected_port,
    "--baud", "921600",
    "write_flash", "0x410000", "fatfs.bin"
]

# Print and run command
print(f"Running: {' '.join(cmd)}")
subprocess.run(cmd)
