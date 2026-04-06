#pragma once

#include "result.h"

typedef enum {
    RELAY_OFF = 0,
    RELAY_ON  = 1,
    RELAY_ERR = -1,
} relay_state_t;

/* Send query command; returns OK if port responds as relay, ERR otherwise. */
result_t relay_probe(int fd);

/* Open relay with feedback; returns OK on acknowledgement, ERR on timeout. */
result_t relay_open(int fd);

/* Close relay with feedback; returns OK on acknowledgement, ERR on timeout. */
result_t relay_close(int fd);

/* Query relay state; returns RELAY_ON, RELAY_OFF, or RELAY_ERR. */
relay_state_t relay_query(int fd);
