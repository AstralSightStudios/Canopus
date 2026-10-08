# Restricted .139 blur/calendar port approval

The user explicitly authorized this restricted private ABI port on 2026-10-08.
Only exact supplied reference mappings are used. Approval is not public SDK
promotion, device acceptance, signing or installation. Records remain restricted,
STATIC_RECOVERED and not_probed. Firmware ranges and capabilities are unchanged.
Source .177 ownership/UI-owner and GPU failure constraints remain applicable.

Evidence: EVID-RESOURCE-BACKGROUND-CALENDAR-PORT-139-001.json; adjacent
.receipts contains complete normalized fingerprints, ordered-reference mappings,
witnesses and AP basic-block byte hashes. Timestamp midnight represents date only.

## Validation (2026-10-08)

Parent independently verified all 153 source/target function starts and flowchart
boundaries across .177/.139/.155; recomputed exact-AP thumb-full-v1 hashes and
ordered-reference witnesses for all 62 roles per target. Native calendar generator
pseudocode and .139 snapshot-free were independently reviewed. No style literal
records were added or promoted.

Exact .139 production build passes unchanged verifier: 20 sections, zero undefined
symbols, 1417 relocations, one constructor/destructor. Unsigned ELF SHA256:
`48b49ed2328e4e3eb3dc382d1bc77741d1854271d6e4ea9784859a072f537a76`.
This is static/build evidence only, not device acceptance.

CLI symbol and evidence schema validation passed. Temporary ELF/pack negative
controls passed: exact .139 calendar_snapshot probe accepted; withdrawing its
record rejects it with “not in target pack allowlist”; .177 calendar_snapshot
probe against .139 also rejected. Real packs/ranges/verifier were not changed by
negative controls. Parent authorized this .139-only SDK commit after validation;
.155 remains separate and awaits its build/commit authorization.
