"""USB serial session with a minimal VT100 screen, enough for Rogue's curses.

Rogue's built-in curses only emits the capabilities in rogue.termcap (cl, cm,
up, do, ce, cd, so, se), plus CR/LF/BS. Everything is logged raw.
"""
import glob
import os
import re
import select
import termios
import time
import tty

ROWS, COLS = 24, 80
CSI = re.compile(rb'\x1b\[([0-9;?]*)([@-~])')


class Screen:
    def __init__(self):
        self.grid = [[' '] * COLS for _ in range(ROWS)]
        self.row = self.col = 0
        self.pending = b''

    def feed(self, data):
        data = self.pending + data
        self.pending = b''
        i = 0
        while i < len(data):
            b = data[i]
            if b == 0x1b:
                m = CSI.match(data, i)
                if not m:
                    if len(data) - i < 16:
                        self.pending = data[i:]
                        return
                    i += 1
                    continue
                self.csi(m.group(1).decode(), chr(m.group(2)[0]))
                i = m.end()
                continue
            if b == 0x0d:
                self.col = 0
            elif b == 0x0a:
                self.row = min(self.row + 1, ROWS - 1)
            elif b == 0x08:
                self.col = max(self.col - 1, 0)
            elif 0x20 <= b < 0x7f:
                if self.col < COLS:
                    self.grid[self.row][self.col] = chr(b)
                self.col = min(self.col + 1, COLS - 1)
            i += 1

    def csi(self, params, final):
        args = [int(p) if p.isdigit() else 0 for p in params.split(';')] if params else []
        n = args[0] if args and args[0] else 1
        if final in 'Hf':
            r = args[0] if args else 1
            c = args[1] if len(args) > 1 else 1
            self.row, self.col = min(max(r, 1), ROWS) - 1, min(max(c, 1), COLS) - 1
        elif final == 'A':
            self.row = max(self.row - n, 0)
        elif final == 'B':
            self.row = min(self.row + n, ROWS - 1)
        elif final == 'C':
            self.col = min(self.col + n, COLS - 1)
        elif final == 'D':
            self.col = max(self.col - n, 0)
        elif final == 'K':
            for c in range(self.col, COLS):
                self.grid[self.row][c] = ' '
        elif final == 'J':
            mode = args[0] if args else 0
            if mode == 2:
                self.grid = [[' '] * COLS for _ in range(ROWS)]
            else:
                for c in range(self.col, COLS):
                    self.grid[self.row][c] = ' '
                for r in range(self.row + 1, ROWS):
                    self.grid[r] = [' '] * COLS

    def line(self, r):
        return ''.join(self.grid[r])

    def text(self):
        return '\n'.join(self.line(r).rstrip() for r in range(ROWS))


def find_port(timeout=20):
    deadline = time.monotonic() + timeout
    while True:
        ports = glob.glob('/dev/cu.usbmodem*') + glob.glob('/dev/ttyACM*')
        if len(ports) == 1:
            return ports[0]
        if len(ports) > 1:
            raise SystemExit(f'Expected exactly one board serial port: {ports}')
        if time.monotonic() > deadline:
            raise SystemExit('Board serial port did not appear')
        time.sleep(.2)


class Session:
    def __init__(self, log_path):
        self.port = find_port()
        time.sleep(.3)
        self.fd = os.open(self.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        tty.setraw(self.fd)
        attrs = termios.tcgetattr(self.fd)
        attrs[4] = attrs[5] = termios.B115200
        termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        self.log = open(log_path, 'ab', buffering=0)
        self.screen = Screen()
        self.raw = bytearray()

    def close(self):
        try:
            os.close(self.fd)
        except OSError:
            pass
        self.log.close()

    def send(self, keys):
        if isinstance(keys, str):
            keys = keys.encode('latin1')
        os.write(self.fd, keys)

    def pump(self, seconds):
        """Read whatever arrives within `seconds`; returns bytes read."""
        end = time.monotonic() + seconds
        got = bytearray()
        while True:
            left = end - time.monotonic()
            if left <= 0:
                break
            if not select.select([self.fd], [], [], min(left, .05))[0]:
                continue
            try:
                b = os.read(self.fd, 65536)
            except BlockingIOError:
                continue
            except OSError:
                raise ConnectionError('serial port went away')
            if not b:
                raise ConnectionError('serial port closed')
            self.log.write(b)
            self.raw.extend(b)
            got.extend(b)
            self.screen.feed(b)
        return bytes(got)

    def settle(self, quiet=.15, limit=10):
        """Read until the board has been silent for `quiet` seconds."""
        end = time.monotonic() + limit
        while time.monotonic() < end:
            if not self.pump(quiet):
                return
        raise TimeoutError('output did not settle')

    def expect(self, pattern, timeout=10, raw=False, since=None):
        """Wait until regex `pattern` appears on screen, or in raw output
        received since offset `since` (default: from now on)."""
        rx = re.compile(pattern.encode() if raw else pattern)
        mark = len(self.raw) if since is None else since
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if raw:
                m = rx.search(bytes(self.raw[mark:]))
            else:
                m = rx.search(self.screen.text())
            if m:
                return m
            self.pump(.05)
        raise TimeoutError(f'not seen: {pattern!r}\n{self.screen.text()}')
