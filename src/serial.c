#include "serial.h"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <string.h>

int serial_open(const char *port)
{
    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return -1;

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd, &tty) != 0) {
        close(fd);
        return -1;
    }

    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);

    tty.c_cflag  = (tty.c_cflag & ~CSIZE) | CS8; /* 8 data bits */
    tty.c_cflag &= ~PARENB;                        /* no parity   */
    tty.c_cflag &= ~CSTOPB;                        /* 1 stop bit  */
    tty.c_cflag |=  CREAD | CLOCAL;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_iflag = 0;
    tty.c_oflag = 0;
    tty.c_lflag = 0;

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    tcflush(fd, TCIOFLUSH);
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

void serial_close(int fd)
{
    close(fd);
}

void serial_flush(int fd)
{
    tcflush(fd, TCIOFLUSH);
}

int serial_write(int fd, const uint8_t *buf, size_t len)
{
    return (int)write(fd, buf, len);
}

int serial_read(int fd, uint8_t *buf, size_t len, int timeout_ms)
{
    size_t received = 0;

    while (received < len) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int ret = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0)
            return -1;
        if (ret == 0)
            break; /* timeout */

        int n = (int)read(fd, buf + received, len - received);
        if (n < 0)
            return -1;
        received += (size_t)n;
    }

    return (int)received;
}
