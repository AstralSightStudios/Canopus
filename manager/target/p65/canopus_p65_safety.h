/* P65 deployment is withdrawn after a device Logo boot-loop report.
 * An ABI correction is not proof of cold-boot safety. No CLI/env override.
 * Offline tests may model the enabled path, but must not produce a device pack.
 */
#ifndef CANOPUS_P65_SAFETY_H
#define CANOPUS_P65_SAFETY_H
#define CANOPUS_P65_DEPLOYMENT_ENABLED 0
#define CANOPUS_P65_ERR_DEPLOYMENT_BLOCKED (-130)
#endif
