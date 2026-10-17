#!/usr/bin/env python3
"""Generate a local P65 candidate key; never include the private key in delivery."""
from pathlib import Path
import os,sys
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives import serialization
out=Path(sys.argv[1]);private=out/'local-signing-key.bin'
if not private.exists():
 fd=os.open(private,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
 with os.fdopen(fd,'wb') as f:f.write(Ed25519PrivateKey.generate().private_bytes(serialization.Encoding.Raw,serialization.PrivateFormat.Raw,serialization.NoEncryption()))
key=Ed25519PrivateKey.from_private_bytes(private.read_bytes());public=key.public_key().public_bytes(serialization.Encoding.Raw,serialization.PublicFormat.Raw)
(out/'p65_runtime_public_key.h').write_text('#define P65_RUNTIME_PUBLIC_KEY {'+','.join('0x%02x'%x for x in public)+'}\n')
(out/'runtime-signing-public-key.bin').write_bytes(public)
