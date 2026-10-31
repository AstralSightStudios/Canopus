# Superseded string-census finding

This historical finding is disproved by EVID-ASTROLINK-N66-ABI-001.

```json
{
  "schema": 1,
  "name": "capability_finding_feature_runtime_absent",
  "kind": "finding",
  "prototype": "",
  "instruction_set": "n/a",
  "contexts": {
    "allowed": [
      "target-private"
    ],
    "blocking": true
  },
  "ownership": {
    "argument": "Astrolink is a Canopus module: it publishes a Feature through the firmware's QuickJS Feature runtime (`feature_register_feature`, `feature_manager_post_task`, the framework and Application vtables). This firmware does not contain that runtime, so no address in this pack can stand in for it and the module cannot run here. Recorded so the target is not re-investigated as if it were a missing-address problem."
  },
  "policy": "restricted",
  "status": "STATIC_CONFIRMED",
  "approval_state": "PENDING",
  "symbol_id": "xiaomi-band-9-3.1.32.capability_finding.feature_runtime_absent",
  "target_id": "xiaomi-band-9-3.1.32",
  "entry_address": "",
  "proof": {
    "static": "confirmed",
    "device": "not_probed",
    "evidence_ids": [
      "EVID-CAPAB-9132-001"
    ]
  },
  "provenance": {
    "firmware_sha256": "9c02dab4020b2cc9666ee7d34cf27d311b76aadcec519a38361bbcbd94c53264",
    "evidence_ids": [
      "EVID-CAPAB-9132-001"
    ],
    "source": "String census over the extracted AP analysis-n66nfc_v3.1.32/vela_ap.bin (6,791,552 bytes, ro.build.version=3.1.32): the reference Band 11 image carries FeatureManagerQjs x7, FeaturePrototypeQjs, feature_value_t and 93 `frameworks/runtimes/feature/...` __FILE__ paths, while this image carries FeatureManagerQjs x0, FeaturePrototypeQjs x0, feature_value_t x0 and zero `runtimes/` paths. Its AIOTJS log strings are still present, so the JavaScript app runtime was built; the Feature layer that Canopus modules register into was not. It does carry the other three hosts: btm_* Bluelet BLE entry names (240 matches), gap_if_ble_l2cap_connection_state_cb and btm_/bts_ble_listen_l2cap_channel for LE CoC, and libuv/src/*.c plus /var/pipe/%d for the loop."
  },
  "notes": "No address, no callable, no partial support. This record exists to make an unsupported verdict auditable."
}
```
