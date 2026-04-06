#include "relay.h"
#include "serial.h"

#include <stdint.h>

/* checksum byte = 0xA0 + 0x01 + data_byte */
static const uint8_t CMD_REVERSE[] = {0xA0, 0x01, 0x04, 0xA5};
static const uint8_t CMD_OPEN[]  = {0xA0, 0x01, 0x03, 0xA4};
static const uint8_t CMD_CLOSE[] = {0xA0, 0x01, 0x02, 0xA3};
static const uint8_t CMD_QUERY[] = {0xA0, 0x01, 0x05, 0xA6};

/* send a 4-byte command and wait for a 4-byte response starting with 0xA0 */
static result_t send_and_recv(int fd, const uint8_t *cmd)
{
    uint8_t resp[4];
    serial_flush(fd);
    serial_write(fd, cmd, 4);
    int n = serial_read(fd, resp, sizeof(resp), 500);
    return (n == 4 && resp[0] == 0xA0 && resp[1] == 0x01) ? OK : ERR;
}

result_t relay_probe(int fd)  { return send_and_recv(fd, CMD_QUERY); }
result_t relay_open(int fd)   { return send_and_recv(fd, CMD_OPEN);  }
result_t relay_close(int fd)  { return send_and_recv(fd, CMD_CLOSE); }
result_t relay_reverse(int fd)  { return send_and_recv(fd, CMD_REVERSE); }

relay_state_t relay_query(int fd)
{
    uint8_t resp[4];
    serial_flush(fd);
    serial_write(fd, CMD_QUERY, sizeof(CMD_QUERY));
    int n = serial_read(fd, resp, sizeof(resp), 500);
    if (n != 4 || resp[0] != 0xA0 || resp[1] != 0x01)
        return RELAY_ERR;
    /* response byte 2: 0x01 = on, 0x00 = off */
    return (resp[2] & 0x01) ? RELAY_ON : RELAY_OFF;
}
