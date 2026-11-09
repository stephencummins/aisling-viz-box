# Reads test 4's log from its serial port, optionally sending keys first. Run with python3 -I.
#   python3 -I log.py /dev/cu.usbmodemXXXX 10          # the snapshot it sends on opening, then 10 s of log
#   python3 -I log.py /dev/cu.usbmodemXXXX 20 ssm      # SPI up twice, the MIDI log on, then 20 s
# Opens at 115200, never 1200: 1200 reboots the board into its bootloader.
import fcntl, os, select, struct, sys, termios, time

port, secs = sys.argv[1], float(sys.argv[2])
keys = sys.argv[3].encode('ascii') if len(sys.argv) > 3 else b''
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = a[1] = a[3] = 0
a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
a[4] = a[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, a)
TIOCMBIS, TIOCM_DTR, TIOCM_RTS = 0x8004746C, 0x002, 0x004
fcntl.ioctl(fd, TIOCMBIS, struct.pack('I', TIOCM_DTR | TIOCM_RTS))  # DTR: the board sends its snapshot


def read_for(t):
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                sys.stdout.write(os.read(fd, 8192).decode('ascii', 'replace').replace('\r', ''))
                sys.stdout.flush()
            except BlockingIOError:
                pass


read_for(0.5)
for k in keys:
    os.write(fd, bytes([k]))
    read_for(0.4)
read_for(secs)
os.close(fd)
