"""Standard vectors plus cross-language envelopes; tampering never reaches load."""
import hashlib
import hmac
import importlib.util
from pathlib import Path
import re
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('protect_lua',ROOT/'scripts/protect_lua.py')
p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
LUA=ROOT/'watchfaces/canopus-installer-prod/xiaomi-band-11/build/host-lua54/lua-5.4.0/src/lua'

@unittest.skipUnless(LUA.exists(),'Lua 5.4.0 required')
class CryptoTests(unittest.TestCase):
    def run_lua(self,source):
        return subprocess.run([str(LUA),'-'],input=source,text=True,capture_output=True,cwd=ROOT)
    def test_vectors(self):
        key=bytes(range(32));nonce=bytes.fromhex('000000090000004a00000000')
        expected='10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4ed2826446079faa0914c2d705d98b02a2b5129cd1de164eb9cbd083e8a2503c4e'
        self.assertEqual(p.crypt(key,nonce,bytes(64)).hex(),expected)
        src="local C=dofile('scripts/templates/lua_crypto.lua')\nlocal function d(s)return(s:gsub('%x%x',function(v)return string.char(tonumber(v,16))end))end\n"
        for n in (0,1,55,56,63,64,65,255,4096):
            data=bytes(i%256 for i in range(n))
            src+=f"assert(C.sha(d('{data.hex()}'))==d('{hashlib.sha256(data).hexdigest()}'))\n"
            src+=f"assert(C.mac(d('{key.hex()}'),d('{data.hex()}'))==d('{hmac.digest(key,data,'sha256').hex()}'))\n"
            src+=f"assert(C.crypt(d('{key.hex()}'),d('{nonce.hex()}'),d('{data.hex()}'),1)==d('{p.crypt(key,nonce,data).hex()}'))\n"
        r=self.run_lua(src);self.assertEqual(r.returncode,0,r.stderr)
    def test_tamper_rejected_before_loader(self):
        source=p.wrap(b'not-bytecode',b'x'*32)
        # Change the ciphertext byte in logical first block, leaving valid length/header.
        m=re.search(r"\[1\]='([0-9a-f]+)'",source);self.assertIsNotNone(m)
        raw=bytearray.fromhex(m[1]);raw[20]^=1
        damaged=source[:m.start(1)]+raw.hex()+source[m.end(1):]
        r=self.run_lua('load=function() error("LOAD REACHED") end\n'+damaged)
        self.assertNotEqual(r.returncode,0)
        self.assertIn('authentication failed',r.stderr)
        self.assertNotIn('LOAD REACHED',r.stderr)
    def test_layout_is_reproducible_and_seed_specific(self):
        self.assertEqual(p.wrap(b'abc',b'a'*32),p.wrap(b'abc',b'a'*32))
        self.assertNotEqual(p.wrap(b'abc',b'a'*32),p.wrap(b'abc',b'b'*32))

if __name__=='__main__':unittest.main()
