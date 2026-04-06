#pragma once

#include <stdint.h>
#include "result.h"

typedef struct {
    float    moisture;     /* % (0-100.0)       */
    float    temperature;  /* C (-20.0-80.0)    */
    float    conductivity; /* us/cm (0-20000)   */
    float    ph;           /* (3.0-10.0)        */
    uint16_t nitrogen;     /* mg/kg (0-2000)    */
    uint16_t phosphorus;   /* mg/kg (0-2000)    */
    uint16_t potassium;    /* mg/kg (0-2000)    */
    uint16_t salinity;     /* mg/kg (0-2000)    */
} soil_data_t;

/* Read all 8 parameters via Modbus-RTU (device address 0x01).
 * Returns OK on success, ERR on timeout / CRC error / framing error. */
result_t soil_read(int fd, soil_data_t *data);
