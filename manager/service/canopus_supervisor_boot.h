#ifndef CANOPUS_SUPERVISOR_BOOT_H
#define CANOPUS_SUPERVISOR_BOOT_H

#include <stdint.h>

struct canopus_supervisor_v1;

#define CANOPUS_BOOT_SUPPORTED 1u
#define CANOPUS_BOOT_PINNED 2u

int canopus_supervisor_boot_query(struct canopus_supervisor_v1 *sup, uint32_t *status);
int canopus_supervisor_boot_set(struct canopus_supervisor_v1 *sup, int pinned);
int canopus_supervisor_boot_start(struct canopus_supervisor_v1 *sup);
void canopus_supervisor_boot_poll(struct canopus_supervisor_v1 *sup);

#endif
