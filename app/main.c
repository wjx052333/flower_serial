#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include "device.h"
#include "relay.h"
#include "soil.h"

static volatile int running = 1;

static void sig_handler(int sig) { (void)sig; running = 0; }

static void print_soil(const soil_data_t *d)
{
    printf("moisture:     %.1f %%\n",    d->moisture);
    printf("temperature:  %.1f C\n",     d->temperature);
    printf("conductivity: %.0f us/cm\n", d->conductivity);
    printf("pH:           %.1f\n",       d->ph);
    printf("nitrogen:     %u mg/kg\n",   d->nitrogen);
    printf("phosphorus:   %u mg/kg\n",   d->phosphorus);
    printf("potassium:    %u mg/kg\n",   d->potassium);
    printf("salinity:     %u mg/kg\n",   d->salinity);
}

int main(void)
{
    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);
    setbuf(stdout, NULL);

    int relay_fd = relay_device_init();
    int soil_fd  = soil_device_init(relay_fd);

    printf("relay: %s  soil: %s\n",
           relay_fd >= 0 ? "online" : "offline",
           soil_fd  >= 0 ? "online" : "offline");
    printf("commands: o=relay open  c=relay close  r=read sensor  q=quit\n");

    char line[16];
    while (running && fgets(line, sizeof(line), stdin)) {
        switch (line[0]) {
        case 'o':
            if (relay_fd < 0) { printf("relay: offline\n"); break; }
            if (relay_open(relay_fd) == OK)
                printf("relay: opened\n");
            else {
                fprintf(stderr, "relay open failed, reconnecting...\n");
                relay_device_reinit(&relay_fd);
            }
            break;

        case 'c':
            if (relay_fd < 0) { printf("relay: offline\n"); break; }
            if (relay_close(relay_fd) == OK)
                printf("relay: closed\n");
            else {
                fprintf(stderr, "relay close failed, reconnecting...\n");
                relay_device_reinit(&relay_fd);
            }
            break;

        case 'r': {
            if (soil_fd < 0) { printf("soil: offline\n"); break; }
            soil_data_t d;
            if (soil_read(soil_fd, &d) == OK)
                print_soil(&d);
            else {
                fprintf(stderr, "soil read failed, reconnecting...\n");
                soil_device_reinit(&soil_fd, relay_fd);
            }
            break;
        }

        case 'q':
            goto done;
        }
    }

done:
    relay_device_deinit(&relay_fd);
    soil_device_deinit(&soil_fd);
    printf("bye.\n");
    return 0;
}
