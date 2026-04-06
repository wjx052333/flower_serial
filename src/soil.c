#include "soil.h"
#include "serial.h"

#include <stdint.h>
#include <stddef.h>

/* ---------- CRC16-Modbus ---------- */
static uint16_t crc16_modbus(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

/* request:  01 03 00 00 00 08 44 0C
 *   addr=0x01, fn=0x03, start_reg=0x0000, count=8, CRC little-endian
 * response: 01 03 10 [16 bytes data] [CRC lo] [CRC hi]  total 21 bytes
 */
static const uint8_t CMD_READ_ALL[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x08, 0x44, 0x0C};

#define RESP_LEN 21   /* 1+1+1 + 16 + 2 */

result_t soil_read(int fd, soil_data_t *data)
{
    uint8_t resp[RESP_LEN];

    serial_flush(fd);
    serial_write(fd, CMD_READ_ALL, sizeof(CMD_READ_ALL));
    int n = serial_read(fd, resp, RESP_LEN, 1000);

    if (n != RESP_LEN)
        return ERR;

    /* header check */
    if (resp[0] != 0x01 || resp[1] != 0x03 || resp[2] != 0x10)
        return ERR;

    /* CRC check: little-endian */
    uint16_t crc_calc = crc16_modbus(resp, RESP_LEN - 2);
    uint16_t crc_recv = (uint16_t)(resp[RESP_LEN - 2]) |
                        (uint16_t)(resp[RESP_LEN - 1]) << 8;
    if (crc_calc != crc_recv)
        return ERR;

    /* parse 8 x 16-bit registers, big-endian */
    uint16_t raw[8];
    for (int i = 0; i < 8; i++)
        raw[i] = ((uint16_t)resp[3 + i * 2] << 8) | resp[4 + i * 2];

    data->moisture     = raw[0] / 10.0f;
    data->temperature  = (int16_t)raw[1] / 10.0f;  /* signed: -200~800 -> -20.0~80.0 C */
    data->conductivity = (float)raw[2];
    data->ph           = raw[3] / 10.0f;
    data->nitrogen     = raw[4];
    data->phosphorus   = raw[5];
    data->potassium    = raw[6];
    data->salinity     = raw[7];

    return OK;
}
