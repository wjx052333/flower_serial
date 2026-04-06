#pragma once

#include "result.h"

/*
 * Each device is initialised independently.
 * On success the function opens the serial port and returns the fd (>= 0).
 * On failure it returns -1.
 *
 * *_deinit closes the fd and resets it to -1.
 * *_reinit closes, waits for USB re-enumeration, then re-detects.
 */

/* Relay --------------------------------------------------------------- */

/* Scan /dev/ttyUSB*, probe each port, return fd of the relay device.
 * Returns -1 if no relay found. */
int relay_device_init(void);

/* Close fd and make it -1. */
void relay_device_deinit(int *fd);

/* Deinit, wait for USB settle, then init again. */
int relay_device_reinit(int *fd);

/* Soil sensor --------------------------------------------------------- */

/* Scan /dev/ttyUSB*, skip ports already used (pass relay_fd to exclude it),
 * return fd of the soil sensor device.
 * Pass relay_fd = -1 if relay is not present.
 * Returns -1 if no soil sensor found. */
int soil_device_init(int relay_fd);

/* Close fd and make it -1. */
void soil_device_deinit(int *fd);

/* Deinit, wait for USB settle, then init again. */
int soil_device_reinit(int *fd, int relay_fd);
