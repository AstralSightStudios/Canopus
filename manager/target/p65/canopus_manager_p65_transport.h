/* In-process CPC2 character-device adapter for the P65 native Manager. */
#ifndef CANOPUS_MANAGER_P65_TRANSPORT_H
#define CANOPUS_MANAGER_P65_TRANSPORT_H

#include "canopus_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The cookie supplied to canopus_client_init must be a live supervisor
 * instance. The adapter forwards complete CPC2 records to its device
 * read/write handlers, preserving query payloads without /dev registration. */
const struct canopus_client_io_v1 *
canopus_manager_p65_local_client_io(void);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_MANAGER_P65_TRANSPORT_H */
