#include "device.h"
#include "serial.h"
#include "relay.h"
#include "soil.h"

#include <stdio.h>
#include <unistd.h>
#include <glob.h>

#define MAX_PORTS      8
#define USB_SETTLE_SEC 1

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/* Open every /dev/ttyUSB* and return the count.
 * fds[] must have room for MAX_PORTS entries; unused slots are -1.   */
static size_t open_all_ports(int fds[MAX_PORTS], char paths[MAX_PORTS][64])
{
    glob_t gl;
    size_t n = 0;

    for (int i = 0; i < MAX_PORTS; i++) fds[i] = -1;

    if (glob("/dev/ttyUSB*", 0, NULL, &gl) != 0 || gl.gl_pathc == 0)
        return 0;

    size_t count = gl.gl_pathc < MAX_PORTS ? gl.gl_pathc : MAX_PORTS;
    for (size_t i = 0; i < count; i++) {
        int fd = serial_open(gl.gl_pathv[i]);
        if (fd >= 0) {
            fds[n] = fd;
            snprintf(paths[n], 64, "%s", gl.gl_pathv[i]);
            n++;
        }
    }
    globfree(&gl);
    return n;
}

static void close_fds(int fds[MAX_PORTS], size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (fds[i] >= 0) { serial_close(fds[i]); fds[i] = -1; }
}

/* ------------------------------------------------------------------ */
/* Relay                                                               */
/* ------------------------------------------------------------------ */

int relay_device_init(void)
{
    int  fds[MAX_PORTS];
    char paths[MAX_PORTS][64];
    size_t n = open_all_ports(fds, paths);

    for (size_t i = 0; i < n; i++) {
        if (relay_probe(fds[i]) == OK) {
            printf("[detect] relay       -> %s\n", paths[i]);
            /* close the rest */
            for (size_t j = 0; j < n; j++)
                if (j != i) serial_close(fds[j]);
            return fds[i];
        }
    }

    fprintf(stderr, "[detect] relay: not found\n");
    close_fds(fds, n);
    return -1;
}

void relay_device_deinit(int *fd)
{
    if (*fd >= 0) { serial_close(*fd); *fd = -1; }
}

int relay_device_reinit(int *fd)
{
    relay_device_deinit(fd);
    sleep(USB_SETTLE_SEC);
    *fd = relay_device_init();
    return *fd;
}

/* ------------------------------------------------------------------ */
/* Soil sensor                                                         */
/* ------------------------------------------------------------------ */

int soil_device_init(int relay_fd)
{
    int  fds[MAX_PORTS];
    char paths[MAX_PORTS][64];
    size_t n = open_all_ports(fds, paths);

    for (size_t i = 0; i < n; i++) {
        /* skip the port already claimed by the relay */
        if (fds[i] == relay_fd) continue;

        /* Any port that is not the relay is assumed to be the soil sensor.
         * Flush residue that might remain from a relay probe on this port. */
        serial_flush(fds[i]);
        printf("[detect] soil sensor -> %s\n", paths[i]);

        for (size_t j = 0; j < n; j++)
            if (j != i && fds[j] != relay_fd) serial_close(fds[j]);
        return fds[i];
    }

    fprintf(stderr, "[detect] soil sensor: not found\n");
    /* close everything we opened (relay_fd stays untouched) */
    for (size_t i = 0; i < n; i++)
        if (fds[i] != relay_fd) serial_close(fds[i]);
    return -1;
}

void soil_device_deinit(int *fd)
{
    if (*fd >= 0) { serial_close(*fd); *fd = -1; }
}

int soil_device_reinit(int *fd, int relay_fd)
{
    soil_device_deinit(fd);
    sleep(USB_SETTLE_SEC);
    *fd = soil_device_init(relay_fd);
    return *fd;
}
