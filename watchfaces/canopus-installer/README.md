# watchfaces/canopus-installer

Staging directory for the Supervisor builds (`scripts/build_canopus_supervisor.sh` writes
`canopus_supervisor-<target>.bin` and `manager_icon.bin` here). The standalone development
installer watchface that used to live here was removed with the CPC1/CPS1 protocol: the
production framework installers are in `../canopus-installer-prod/`, and everything the old
dev page did (enable, disable, remove, rollback, safe mode) is available in the Canopus
Manager app on the band.

Device protocol: CPC2 only (`docs/` ADR-CAN-004). Framework installer commands are
`NATIVE_STAGE` (payload u32 stage 0..2) and `RESTORE_AFTER_BOOT`; failures are explained by
`QUERY_DEVICE` (last error code at payload offset 24).
