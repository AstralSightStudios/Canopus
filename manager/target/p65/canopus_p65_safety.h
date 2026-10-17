/* Historical P65 Supervisor deployment remains withdrawn.
 * The recovered runtime uses corrected callables and a new registry namespace.
 * This enables installation candidates, not an unimplemented system boot hook.
 * Callback code is retained until power-off. */
#ifndef CANOPUS_P65_SAFETY_H
#define CANOPUS_P65_SAFETY_H
#define CANOPUS_P65_DEPLOYMENT_ENABLED 0
#ifdef CANOPUS_P65_RECOVERED_RUNTIME
#define CANOPUS_P65_SESSION_ENABLED 1
#else
#define CANOPUS_P65_SESSION_ENABLED CANOPUS_P65_DEPLOYMENT_ENABLED
#endif
#define CANOPUS_P65_ERR_DEPLOYMENT_BLOCKED (-130)
#endif
