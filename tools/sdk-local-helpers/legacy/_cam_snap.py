import sys
import time

import serial

PORT = 'COM5'
BAUD = 115200
TIMEOUT = 30.0
MAX_TRIES = 5


def read_one_jpeg(ser, timeout):
    ser.write(b'cam snap\r\n')
    buf = b''
    n = None
    start = time.time()
    while time.time() - start < timeout:
        chunk = ser.read(4096)
        if not chunk:
            continue
        buf += chunk
        if n is None:
            i = buf.find(b'JPEG ')
            if i != -1:
                j = buf.find(b'\r\n', i)
                if j != -1:
                    try:
                        n = int(buf[i + 5:j].strip())
                    except ValueError:
                        return None
                    buf = buf[j + 2:]
        if n is not None and len(buf) >= n:
            return buf[:n]
    return None


ser = serial.Serial(PORT, BAUD, timeout=0.5)
# Give a freshly-flashed device time to boot and finish camera early_init.
time.sleep(7.0)

# Make sure the stream is running (idempotent).
ser.reset_input_buffer()
ser.write(b'cam start\r\n')
time.sleep(1.5)

for attempt in range(1, MAX_TRIES + 1):
    ser.reset_input_buffer()
    jpg = read_one_jpeg(ser, TIMEOUT)
    if jpg is not None and jpg[:2] == b'\xff\xd8' and jpg[-2:] == b'\xff\xd9':
        fn = 'cam_snap.jpg'
        with open(fn, 'wb') as f:
            f.write(jpg)
        print('saved %s (%d bytes, try %d)' % (fn, len(jpg), attempt))
        sys.exit(0)
    print('bad jpeg on try %d (len=%d), retrying...' % (attempt, 0 if jpg is None else len(jpg)),
          file=sys.stderr)
    time.sleep(1.0)

print('ERROR: could not capture a valid JPEG', file=sys.stderr)
sys.exit(1)
