# One-shot module installer watchface

This watchface installs the signed `canopus_hello` example module through an
already-installed `/dev/canopus` supervisor. Opening the watchface is the only
action required.

The runtime flow is intentionally fail-closed:

1. Validate the packaged receipt shape and ELF bounds.
2. Copy `receipt.bin` and `module.bin` to `/data/canopus/inbox/`.
3. Send a bounded CPC2 `INSTALL` request containing only the
   `canopus_hello` token.
4. The supervisor verifies the CMI1 Ed25519 signature, exact target and
   firmware identity, artifact size, and SHA-256 digest; then atomically moves
   the verified ELF out of the public inbox into its supervisor-owned path.
5. Register the module as **installed and disabled**.
6. Notify the user to open Canopus Manager and enable it.
7. Ask the stock watchface manager to switch away and remove this installer.

If verification or communication fails, the page remains installed and shows
a diagnostic. If the stock manager refuses self-removal (for example because
this is the final watchface), installation remains complete and the page asks
for manual removal.

## Build the package payloads

Build and verify the example module first, then produce the signed receipt:

```sh
cp modules/examples/hello/build/hello_module.elf \
  watchfaces/canopus_hello/module.bin

python3 scripts/build-module-installer-receipt.py \
  --module watchfaces/canopus_hello/module.bin \
  --module-id canopus_hello \
  --version 1 \
  --lifecycle 0 \
  --private-key /secure/path/module-installer-ed25519.pem \
  --output watchfaces/canopus_hello/receipt.bin
```

The private Ed25519 key is never packaged or committed. The corresponding
32-byte public key is compiled into the exact-target supervisor. The `.bin`
payloads are build artifacts ignored by Git; they must be present when the
watchface package is assembled.

Current target lock:

- Target: `xiaomi-band-10-pro-3.101.030`
- Firmware SHA-256:
  `f701a84ffcafa67f4d4603ad8cd66a11e5442f27140f5af0982e0975dccd225b`
- Receipt wire format: CMI1, 256 bytes, 192-byte signed prefix
