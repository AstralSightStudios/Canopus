# P67 3.101.036 native integration evidence

AP SHA-256 `662d67f5e247e31e194d3161024890ba93b9d29d70b290fadb9aac8ce8ec3c81`,
XIP base `0x0C0C0000`. All addresses below were located in this exact image;
this document establishes static ABI evidence, not device or radio validation.

## Generic GAP and LE CoC

The generic GAP functions retain the .043 contracts:
`ble_advertising_start 0xC39AA88`, `gap_scan_start_impl 0xC39AC70`,
`ble_scan_stop 0xC39AD08`, `bt_sal_le_stop_advertising 0xC39AB58`.
The stock GAP client global is `0x20122FB0`, with its business loop at +56.

The ordinary channel producer entries use original SDU buffers, the same
68-byte request/config layout and the common callback identifiers. Examples:
connect `0xC7E94FC`, accept `0xC7EA1D4`, send `0xC7E9748`, PSM register
`0xC7E805C`. The ordinary send handler creates and owns fragment nodes; it does
not accept a fragment node in place of the original buffer. Disconnect policy
is the same state-dependent branch sequence implemented by the .043 shim.

## Ferry Feature registration

`FrameworkExt::getInstance 0xC8E6C70` reads singleton `0x20138D30`; its
constructor writes primary vtable `0x2CD7D618`. The factory at vtable slot 8 is
`0xC8CCCB4`, uses implementation mutex +348 and app vector +460/+464, and
creates the Application with primary vtable `0x2CD7C5C0`. Both primary tables
retain two prefix words and 15/52 total words.

Application initialization at slot 2 is `0xC8C2A00`; runtime is at app +100
and its manager at +164. The AIOTJS constructor at `0xC8F8B0E` stores the
manager created from the real application host. Manager registry is +4 and
manager async +60. These are the native Feature objects, not JS opaque objects.

The async handle is 112 bytes; loop +4, busy +24 and pending +108. Exact mutex
entries are `0xC2C6B54`, `0xC2C6C30`, `0xC2C6C4C`; `uv_close` is `0xC2C5774`.
The UV key is `0x201003AC`. NuttX file wake fields match .043: TCB TLS +100,
group filelist +720, UV-state loop +760. The TLS header's first word points to
taskinfo; its key slots start at +16 with stride 4. Exact-image instructions
`0xC1F0522` / `0xC1F0524` copy TCB +100 to out +4, and caller `0xC1E5940`
loads that word at frame +8 because out is frame +4. The former +104 value
confused the output structure with the caller's stack frame. Taskinfo is
loaded at `0xC1E5928`, its key slot at `0xC1E595A`, and the UV-state loop at
`0xC2C866C`. The pipe operation table
`0x2CC9910C` was identified from its ten exact Thumb handler pointers.

## QuickJS

All six value APIs are direct calls, with full 48-byte identical instruction
prefixes against the reviewed .043 implementations: string `0xC27EA50`,
ToCString `0xC27E50C`, property `0xC280E14`, ArrayBuffer copy `0xC2BD3C8`,
ArrayBuffer getter `0xC2BD52C`, typed-array getter `0xC2BD58C`. AAPCS argument
placement and buffer/string ownership therefore match the shared native JS
layer; no custom allocator or opaque object decoding is used.

The original owner-pump-only scope is superseded by the exact-image L2 recovery
in `EVID-PORT-1036-L2-001.json` and `l2-sram-probe.json`. RX, send, session,
crypto, SAR connection list and Bluetooth owner-loop records now enable the
existing encrypted CPC2 backend. No shared transport implementation or approval
state changed. The L2-enabled Supervisor builds and passes normal verification.
Astrolink v42 was installed, enabled and activated over the real companion CLI,
reported state 13 and activation error 0, and independently passed all 18 native
lifecycle checks in Mirage. This is emulator evidence only; physical-device and
CoC peer acceptance remain separate. Full logs are at
`/tmp/astrolink-p67-036-v42-native-probe`; the module repository retains a compact
result in `docs/evidence/p67-036-v42.json`.

Every added record remains `restricted`, `STATIC_RECOVERED`, `PENDING`.
No `.043` runtime result is inherited by `.036`.
