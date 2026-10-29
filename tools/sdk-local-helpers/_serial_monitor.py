import sys
import time

import serial

PORT = 'COM5'
BAUD = 115200
TOTAL = 18.0

ser = serial.Serial(PORT, BAUD, timeout=0.2)

# Hard reset (same as esptool --after hard_reset).
ser.setDTR(False)
ser.setRTS(True)
time.sleep(0.15)
ser.setRTS(False)

start = time.time()
sent_status = False
sent_start = False
buf = b''

while time.time() - start < TOTAL:
    elapsed = time.time() - start
    if not sent_status and elapsed >= 5.0:
        ser.write(b'cam status\r\n')
        sent_status = True
    if not sent_start and elapsed >= 6.5:
        ser.write(b'cam start\r\n')
        sent_start = True

    data = ser.read(4096)
    if data:
        buf += data
    else:
        time.sleep(0.01)

sys.stdout.buffer.write(buf)
