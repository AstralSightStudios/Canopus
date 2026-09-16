-- Pure Lua 5.4: ChaCha20 (RFC 8439) and encrypt-then-MAC HMAC-SHA256.
-- Arithmetic is explicitly reduced to 32 bits; independent keys are required.
local C = {}
local function r(x,n) return ((x >> n) | (x << (32-n))) & 0xffffffff end
local K={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2}
function C.sha(s)
 local len=#s
 s=s..'\128'..string.rep('\0',(55-len)%64)..string.pack('>I8',len*8)
 local h={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}
 for p=1,#s,64 do
  local w={}
  for i=1,16 do w[i]=string.unpack('>I4',s,p+(i-1)*4) end
  for i=17,64 do
   local x,y=w[i-15],w[i-2]
   w[i]=(w[i-16]+(r(x,7)~r(x,18)~(x>>3))+w[i-7]+(r(y,17)~r(y,19)~(y>>10)))&0xffffffff
  end
  local a,b,c,d,e,f,g,z=table.unpack(h)
  for i=1,64 do
   local t=(z+(r(e,6)~r(e,11)~r(e,25))+((e&f)~((~e)&g))+K[i]+w[i])&0xffffffff
   local u=((r(a,2)~r(a,13)~r(a,22))+((a&b)~(a&c)~(b&c)))&0xffffffff
   a,b,c,d,e,f,g,z=(t+u)&0xffffffff,a,b,c,(d+t)&0xffffffff,e,f,g
  end
  local v={a,b,c,d,e,f,g,z}
  for i=1,8 do h[i]=(h[i]+v[i])&0xffffffff end
 end
 return string.pack('>I4I4I4I4I4I4I4I4',table.unpack(h))
end
local function xor(s,n)
 return (s:gsub('.',function(c) return string.char(c:byte()~n) end))
end
function C.mac(k,s)
 if #k>64 then k=C.sha(k) end
 k=k..string.rep('\0',64-#k)
 return C.sha(xor(k,0x5c)..C.sha(xor(k,0x36)..s))
end
local function q(x,a,b,c,d)
 x[a]=(x[a]+x[b])&0xffffffff; x[d]=r(x[d]~x[a],16)
 x[c]=(x[c]+x[d])&0xffffffff; x[b]=r(x[b]~x[c],20)
 x[a]=(x[a]+x[b])&0xffffffff; x[d]=r(x[d]~x[a],24)
 x[c]=(x[c]+x[d])&0xffffffff; x[b]=r(x[b]~x[c],25)
end
function C.crypt(k,n,s,counter)
 assert(#k==32 and #n==12)
 local out={}
 for p=1,#s,64 do
  local x={0x61707865,0x3320646e,0x79622d32,0x6b206574}
  for i=1,8 do x[i+4]=string.unpack('<I4',k,(i-1)*4+1) end
  x[13]=counter; counter=counter+1
  assert(counter<=0x100000000)
  for i=1,3 do x[i+13]=string.unpack('<I4',n,(i-1)*4+1) end
  local original={table.unpack(x)}
  for i=1,10 do
   q(x,1,5,9,13); q(x,2,6,10,14); q(x,3,7,11,15); q(x,4,8,12,16)
   q(x,1,6,11,16); q(x,2,7,12,13); q(x,3,8,9,14); q(x,4,5,10,15)
  end
  for i=1,16 do x[i]=(x[i]+original[i])&0xffffffff end
  local block=string.pack('<I4I4I4I4I4I4I4I4I4I4I4I4I4I4I4I4',table.unpack(x))
  local t={}
  for i=1,math.min(64,#s-p+1) do t[i]=string.char(s:byte(p+i-1)~block:byte(i)) end
  out[#out+1]=table.concat(t)
 end
 return table.concat(out)
end
return C
