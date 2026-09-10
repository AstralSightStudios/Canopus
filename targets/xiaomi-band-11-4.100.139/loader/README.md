# Band 11 4.100.139 loader

The complete native installer is a **STATIC_TEST_CANDIDATE**, device **NOT_PROBED**.
It includes os.execute recovery, Lua-owned PIC stage1, stage2, Supervisor, /dev/canopus,
and the corrected native Manager app/page/launcher backend. General SDK callables remain pending.

- [Current native ABI, ownership, MPU/cache audit and validation](native-audit.md)
- [Latest firmware integration checks and tested bundle](revalidation-2026-09-10.md)
- [Packing and device-test instructions](../../../watchfaces/canopus-installer-prod/xiaomi-band-11/docs/README.md)
- [Initial audit before the native implementation](audit-2026-09-08.md)

The initial audit's BLOCKED/diagnostic-only statements are historical. The current installer
uses the same Run / Clear Env structure as Band 9. Its resource root is exactly one Lua file
and five .bin files; instructions and build intermediates are in excluded subdirectories.
