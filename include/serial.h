#pragma once

#include <stdint.h>
#include <stddef.h>

int  serial_open(const char *port);
void serial_close(int fd);
void serial_flush(int fd);
int  serial_write(int fd, const uint8_t *buf, size_t len);

/* 读取最多 len 字节，超时返回实际收到字节数（可能为 0） */
int  serial_read(int fd, uint8_t *buf, size_t len, int timeout_ms);
