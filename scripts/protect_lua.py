"""Offline Lua packaging. Keys ship with the package: not a secrecy boundary.
ChaCha20 + HMAC-SHA256 encrypt-then-MAC, with independent 32-byte keys.
"""
import hashlib
import hmac
import random
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def crypt(key, nonce, data):
    def rol(x, n):
        return ((x << n) | (x >> (32-n))) & 0xffffffff
    result = bytearray()
    for off in range(0, len(data), 64):
        initial = list(struct.unpack('<4I', b'expand 32-byte k')) + list(struct.unpack('<8I', key))
        initial += [1 + off//64] + list(struct.unpack('<3I', nonce))
        x = initial.copy()
        def q(a,b,c,d):
            for n,m in ((16,12),(8,7)):
                x[a]=(x[a]+x[b])&0xffffffff; x[d]=rol(x[d]^x[a],n)
                x[c]=(x[c]+x[d])&0xffffffff; x[b]=rol(x[b]^x[c],m)
        for _ in range(10):
            for args in ((0,4,8,12),(1,5,9,13),(2,6,10,14),(3,7,11,15),
                         (0,5,10,15),(1,6,11,12),(2,7,8,13),(3,4,9,14)):
                q(*args)
        block=struct.pack('<16I',*((a+b)&0xffffffff for a,b in zip(x,initial)))
        result.extend(a^b for a,b in zip(data[off:off+64],block))
    return bytes(result)


def wrap(data, seed):
    # Deterministic per payload for --check; a random private build seed gives
    # each distributor a different envelope. Domain-separated derivations.
    digest=hashlib.sha256(data).digest()
    def derive(label):
        return hmac.digest(seed, label+digest, 'sha256')
    key, auth, nonce=derive(b'cipher'),derive(b'auth'),derive(b'nonce')[:12]
    header=b'CLP1'+nonce+struct.pack('<I',len(data))
    encrypted=crypt(key,nonce,data)
    envelope=header+encrypted+hmac.digest(auth,header+encrypted,'sha256')
    rng=random.Random(derive(b'layout'))
    chunks=[envelope[i:i+128] for i in range(0,len(envelope),128)]
    order=list(range(len(chunks))); rng.shuffle(order)
    shares=derive(b'share0')+derive(b'share1')
    other=bytes(a^b for a,b in zip(shares,key+auth))
    hx=lambda b: "'"+b.hex()+"'"
    crypto=(ROOT/'scripts/templates/lua_crypto.lua').read_text()
    # Inner decoder is stripped bytecode by the caller. No extra wrapper frame
    # survives the tail call to the recovered installer.
    return '\n'.join([
        'local C=(function()\n'+crypto+'\nend)()',
        'local function d(s) return (s:gsub("%x%x",function(x) return string.char(tonumber(x,16)) end)) end',
        'local p={'+','.join('['+str(i+1)+']='+hx(chunks[i]) for i in order)+'}',
        'for i=1,#p do p[i]=d(p[i]) end',
        'local b=table.concat(p); p=nil',
        'local a,z=d('+hx(shares)+'),d('+hx(other)+')',
        'local t={} for i=1,64 do t[i]=string.char(a:byte(i)~z:byte(i)) end',
        'local k=table.concat(t); t=nil; a=nil; z=nil',
        'assert(#b>=52 and b:sub(1,4)=="CLP1","Canopus: invalid envelope")',
        'assert(string.unpack("<I4",b,17)==#b-52,"Canopus: invalid length")',
        'local m=C.mac(k:sub(33),b:sub(1,-33)); local diff=0',
        'for i=1,32 do diff=diff|(m:byte(i)~b:byte(#b-32+i)) end',
        'assert(diff==0,"Canopus: authentication failed")',
        'local plain=C.crypt(k:sub(1,32),b:sub(5,16),b:sub(21,-33),1)',
        'k=nil; b=nil; C=nil',
        'local f,e=load(plain,"@canopus","b",_ENV); plain=nil',
        'if not f then error(e,0) end',
        'return f(...)',
    ])
