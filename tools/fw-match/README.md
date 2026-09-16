# Firmware corpus matching

`ensemble_match.py` combines entry bytes, CFG edge topology, strings/constants,
caller/callee neighborhoods and data-reference ownership. `fingerprint_corpus.py`
adds full Thumb instruction fingerprints, preserving field offsets, registers,
instruction widths, internal branch destinations and block boundaries. It records
external call and pointer-literal destinations separately. Both tools produce
static matching evidence; matching a wrongly named source does not fix its name
or establish a callable prototype.

The added mutually unique full-body seeds follow the unique-match / neighborhood
strategy described in the [BinDiff manual](https://www.zynamics.com/bindiff/manual/).
This implementation does not claim to reproduce BinDiff's MD-index or block
matching algorithms. Bodies shorter than eight instructions and duplicate hashes
cannot independently seed matching. Full-body and entry-byte evidence share one
family so correlated instruction evidence does not inflate the independence count.

Assignment is one-to-one between physical functions. Source aliases may share a
target entry. Previously `bt_free`/`heap_free` and the pad-bottom aliases displaced
one another; in the `.139 → .155` run this produced three below-top assignments.
The corrected assignment produces zero such displacements, 140 matched names for
138 distinct physical entries, and 27,039 initial full-body seeds. Ten independently
reviewed critical-path oracle entries match. This is a small port regression set,
not a calibrated claim about overall accuracy, other firmware, or uncertain source
semantics. Candidate output has no `callable_address`; source rejections still
require review in any target port.

Corpus v1/v2 remains readable; `body_fingerprint` and `firmware_sha256` are optional
schema-2-compatible additions. Fingerprint enrichment checks every available
function entry against the raw image before writing. Unknown/truncated decoding
produces no fingerprint. The IDA extractor verifies the database input SHA against
the adjacent binary, rejects empty results, disables auto-analysis and closes
without saving. IDB extraction needs the locally installed `idapro` environment;
Thumb enrichment needs `capstone`.

```sh
python tools/fw-match/extract_corpus.py \
  --idb fwbins/xiaomi-band-11-4.100.155/vela_ap.bin.i64 \
  --target-id xiaomi-band-11-4.100.155 \
  --output targets/fw-corpus/xiaomi-band-11-4.100.155.json

# Run for both the source and target corpora.
python tools/fw-match/fingerprint_corpus.py \
  --corpus targets/fw-corpus/xiaomi-band-11-4.100.155.json \
  --firmware fwbins/xiaomi-band-11-4.100.155/vela_ap.bin \
  --output targets/fw-corpus/xiaomi-band-11-4.100.155.json

python tools/fw-match/ensemble_match.py \
  --source-symbols targets/xiaomi-band-11-4.100.139/symbols \
  --source-corpus targets/fw-corpus/xiaomi-band-11-4.100.139.json \
  --target-corpus targets/fw-corpus/xiaomi-band-11-4.100.155.json \
  --target-id xiaomi-band-11-4.100.155 \
  --target-firmware-sha256 ea0bdf1920cb30223d616432af00565ca67622e6468328f5eab155f8cdc2fb9f \
  --oracle targets/xiaomi-band-11-4.100.155/evidence/fw-match/oracle.json \
  --output build/ensemble-139-to-155.json

python -m unittest discover -s tools/fw-match/tests -p 'test_*.py'
```

The global pass still uses bounded owner/data-reference records. Normalized
fingerprints do not model pointer ownership, complete interprocedural data flow,
allocation failure or callback lifetime. Those require independent consumer/ABI
review. The `.139` notification crash is a concrete example: the insert function
and 96-byte message layout were right, but the reminder's mandatory borrowed
16-byte context was missed by an insertion-only test.
