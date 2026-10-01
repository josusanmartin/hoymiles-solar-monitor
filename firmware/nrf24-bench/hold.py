# keep the port open (so the board isn't reset) and run commands, then hold for N seconds
import os, serial, sys, time
s = serial.Serial(); s.port=os.environ.get('BENCH_PORT', '/dev/cu.usbserial-0001'); s.baudrate=115200; s.timeout=0.3; s.dtr=False; s.rts=False; s.open()
buf=''; t=time.time()
while time.time()-t<15 and 'READY' not in buf: buf+=s.read(4096).decode(errors='replace')
print('boot ok' if 'READY' in buf else 'no READY')
for c in sys.argv[2:]:
    s.write((c+'\n').encode()); time.sleep(0.5); print(s.read(4096).decode(errors='replace').strip())
time.sleep(float(sys.argv[1]))
