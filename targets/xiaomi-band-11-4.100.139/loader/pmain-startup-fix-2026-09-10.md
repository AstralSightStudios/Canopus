# Fix for `Startup failed: Unexpected pmain arguments`

The user reported this startup failure on the device. The previous C fixture
incorrectly retained all three pmain arguments. This record supersedes that
assumption in the September 8 recovery audit. The updated bundle has passed
host tests and the firmware checks below; a successful device run is pending.

Firmware SHA-256:
`31ce82257f7c127950dc5070b86316730cf468a41f0d004559e41e7d923b2c74`.

## Root cause in the firmware

`pmain` at `0x0c6d33b0` initially receives path, loop lightuserdata and a pointer
to a native root context. At `0x0c6d3868` it calls the loop-context getter
`0x0c6c89e4`. The getter's first-use branch pops the missing registry entry at
`0x0c6c8a0c`, pushes a key/new userdata, and consumes both with rawset at
`0x0c6c8a30`. The unconditional pop at `0x0c6c8a3a` then removes the original
root argument. On subsequent getter calls, the final pop is balanced.

`pmain` subsequently pushes error handler `0x0c6cecd1` at `0x0c6d38de`, so
debug local slot 3 contains that C function when the entry script runs.
Requiring slot 3 to be userdata rejected this actual first-use path.

## Recovery and dataman isolation

The adapter still requires the exact identity and pmain address. It accepts
the changed stack layout only when slot 3 is the exact error handler. It then
uses the full userdata stored under registry key `luavgl_key`, checking that
it has no metatable and exactly one nil uservalue. These are provenance/shape
checks against this firmware's initializer, not a generic userdata-size check.

`0x0c6ca92c` allocates this 16-byte userdata with one uservalue. The pmain
prefix at `0x0c6d3402..0x0c6d3416` copies the original root object and two
callbacks into its first three words. Those words can therefore be reused.
The fourth word differs: `luaopen_lvgl` sets a destructor at `0x0c6cace8`,
whereas the original pmain argument has a dataman pointer there.

The adapter saves and temporarily removes registry `dataman.meta` before
reentry. The prefix at `0x0c6d341a..0x0c6d346c` then creates a separate
8-byte userdata and stores the fourth word into it without dereferencing that
word. Already-loaded LVGL is required. The OS-registration sentinel unwinds
before another script is run. Both success and failure restore the original
dataman registry object; its original payload is never overwritten. An absent
original dataman entry is restored to absence. The temporary userdata has no
metatable/finalizer and can be collected normally.

The original retained-root path remains supported. Each Lua state permits one
reentry attempt. The previously documented single strdup leak still applies.
This startup failure occurred before native stage execution or MPU operations.

## Validation

- `scripts/tests/band11_pmain_firmware.py`: executes the exact binary's
  loop getter and pmain prefix in Unicorn, with Lua C APIs modeled. Confirms
  first-use root removal, balanced repeated lookup, cached-root copying,
  separate temporary dataman allocation and unchanged live context bytes.
- `scripts/tests/band11_lua_fixture.c`: real Lua C frames now reproduce the
  first-use pop, error handler and native root/dataman storage. Lua 5.4.0 and
  5.5 cover restoration, absent dataman, failure cleanup, invalid root/handler,
  retained original arguments, once-only recovery and generated Run/Clear UI.
- All six installer tests pass, including the existing five ARM bootstrap
  tests and the new two firmware-prefix tests. Supervisor ELF verification
  passes with zero undefined symbols.
- The rebuilt directory and resource ZIP contain identical `main.lua` plus
  five `.bin` files. No SDK/MPU/native-stage changes are required by this fix.

These checks do not emulate the entire firmware, physical cache or display.
They establish the corrected startup path's tested behavior, not a promise
that the complete native loader has already succeeded on hardware.
