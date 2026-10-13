#include <assert.h>
#include <errno.h>
#include <string.h>
#include <termios.h>
int main(void)
{
    struct termios t, readback;
    assert(tcgetattr(0, &t) == 0);
    assert(cfgetospeed(&t) == B9600 && cfgetispeed(&t) == B9600);
    t.c_cflag |= CS8 | CLOCAL;
    assert(cfsetospeed(&t, B115200) == 0);
    assert((t.c_cflag & CBAUD) == B115200);
    assert((t.c_cflag & CSIZE) == CS8 && (t.c_cflag & CLOCAL));
    assert(cfsetispeed(&t, B0) == 0 && cfgetispeed(&t) == B115200);
    assert(cfsetspeed(&t, B4800) == 0 && cfgetispeed(&t) == B4800);
    struct termios before = t;
    errno = 0;
    assert(cfsetspeed(&t, B115200 + 1) == -1 && errno == EINVAL);
    assert(memcmp(&t, &before, sizeof t) == 0);
    t.c_lflag &= ~ECHO;
    assert(tcsetattr(0, TCSANOW, &t) == 0);
    assert(tcgetattr(0, &readback) == 0 && !(readback.c_lflag & ECHO));
    errno = 0;
    assert(cfsetospeed(NULL, B9600) == -1 && errno == EFAULT);
    assert(tcgetattr(0, NULL) == -1);
    return 0;
}
