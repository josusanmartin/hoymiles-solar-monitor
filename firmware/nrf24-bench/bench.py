# talk to the ESP32 nRF24 bench firmware: bench.py reset | bench.py <cmd> [<cmd> ...]
import os, serial, sys, time
s = serial.Serial()
s.port = os.environ.get('BENCH_PORT', '/dev/cu.usbserial-0001'); s.baudrate = 115200; s.timeout = 0.3
s.dtr = False; s.rts = False
s.open()
def show(h):
    print("max", max(h), "sum", sum(h))
    for i in range(0, 126, 21):
        print(f"{i:3d}-{i+20:3d}: " + " ".join(f"{x:2d}" for x in h[i:i+21]))
def read_until(token, timeout):
    buf = ''; t = time.time()
    while time.time() - t < timeout:
        buf += s.read(4096).decode(errors='replace')
        if token in buf: break
    for line in buf.splitlines():
        if line.startswith('SCAN '):
            show([int(x) for x in line[5:].split(',')])
        elif line.strip():
            print(line)
args = sys.argv[1:]
if args and args[0] == 'reset':
    s.rts = True; time.sleep(0.1); s.rts = False
    args = args[1:]
read_until('READY', 15)   # opening the port resets the board on macOS
for cmd in args:
    s.write((cmd + '\n').encode())
    read_until('SCAN' if cmd == 's' else ('\n'), 10 if cmd == 's' else 2)
