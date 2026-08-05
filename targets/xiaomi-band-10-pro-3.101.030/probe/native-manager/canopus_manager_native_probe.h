#ifndef CANOPUS_MANAGER_NATIVE_PROBE_H
#define CANOPUS_MANAGER_NATIVE_PROBE_H

/*
 * Run the exact-target Manager registration chain in the caller's process.
 * The caller must be the miwear/UI process because app_install ultimately
 * signals its libuv loop through process-local file descriptors.
 */
int canopus_manager_native_install(void);

#endif /* CANOPUS_MANAGER_NATIVE_PROBE_H */
