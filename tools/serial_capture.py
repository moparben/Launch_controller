import serial
import time
import sys

PORT = 'COM6'
BAUD = 115200
DURATION = 15  # seconds
OUTFILE = 'serial_capture.txt'

try:
    ser = serial.Serial(PORT, BAUD, timeout=1)
except Exception as e:
    print('Error opening serial port:', e, file=sys.stderr)
    sys.exit(2)

end = time.time() + DURATION
with open(OUTFILE, 'wb') as f:
    while time.time() < end:
        n = ser.in_waiting
        if n:
            b = ser.read(n)
            f.write(b)
        else:
            time.sleep(0.05)
ser.close()
print('Capture complete:', OUTFILE)
