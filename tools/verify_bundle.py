#!/usr/bin/env python3
"""Static Mach-O / ad-hoc signature verification; NOT Apple's codesign/Gatekeeper."""
from pathlib import Path
import hashlib, plistlib, struct, sys
app=Path(sys.argv[1]);contents=app/'Contents';exe=contents/'MacOS/AppDeck';helper=contents/'MacOS/appdeck-proxy'
info=(contents/'Info.plist').read_bytes();metadata=plistlib.loads(info)
assert metadata['LSMinimumSystemVersion']=='13.0'
assert metadata['CFBundleIdentifier']=='local.appdeck.manager'
res=(contents/'_CodeSignature/CodeResources').read_bytes();resdata=plistlib.loads(res)

def check(path,identifier,bundle,libsystem_only):
 """Both slices of a universal executable: Mach-O shape, imports, CodeDirectory and every code page.
 bundle: the main executable, whose signature binds Info.plist and the resource seal. Returns the cdhashes."""
 binary=path.read_bytes();assert struct.unpack_from('>II',binary)==(0xcafebabe,2),path
 archs=[];cdhashes=[]
 for i in range(2):
  cpu,sub,offset,size,align=struct.unpack_from('>IIIII',binary,8+i*20)
  assert offset%(1<<align)==0 and offset+size<=len(binary)
  b=binary[offset:offset+size];assert struct.unpack_from('<I',b)[0]==0xfeedfacf
  assert struct.unpack_from('<I',b,4)[0]==cpu
  assert struct.unpack_from('<I',b,12)[0]==2 # MH_EXECUTE
  flags=struct.unpack_from('<I',b,24)[0];assert flags&0x200000 # PIE
  n,sz=struct.unpack_from('<II',b,16);pos=32;deps=[];sig=None;minimum=None;entry=None
  for _ in range(n):
   cmd,cmdsize=struct.unpack_from('<II',b,pos);assert cmdsize>=8
   if cmd==0xc:
    noff=struct.unpack_from('<I',b,pos+8)[0];deps.append(b[pos+noff:pos+cmdsize].split(b'\0')[0].decode())
   if cmd==0x32:minimum=struct.unpack_from('<III',b,pos+8);assert minimum[:2]==(1,13<<16)
   if cmd==0x80000028:entry=struct.unpack_from('<Q',b,pos+8)[0]
   if cmd==0x1d:
    so,ss=struct.unpack_from('<II',b,pos+8);sig=b[so:so+ss];assert len(sig)==ss
   pos+=cmdsize
  assert pos==32+sz and minimum and entry and sig
  if libsystem_only: # the proxy bridge: libSystem and nothing else from outside /usr/lib (no AppKit, no frameworks)
   assert '/usr/lib/libSystem.B.dylib' in deps and all(d.startswith('/usr/lib/') for d in deps),deps
  else:
   # Cross builds import libSystem/libobjc only; a local Apple-clang build also links the system AppKit/libc++.
   assert {'/usr/lib/libSystem.B.dylib','/usr/lib/libobjc.A.dylib'}<=set(deps)
   assert all(d.startswith('/usr/lib/') or d.startswith('/System/Library/Frameworks/') for d in deps),deps
  magic,length,nblobs=struct.unpack_from('>III',sig);assert magic==0xfade0cc0 and length<=len(sig) # Apple codesign pads the reserved signature space
  blobs={}
  for j in range(nblobs):
   typ,start=struct.unpack_from('>II',sig,12+j*8);blen=struct.unpack_from('>I',sig,start+4)[0];blobs[typ]=sig[start:start+blen]
  cd=blobs[0];magic,length,version,flags,hoff,ioff,nspecial,nslots,limit=struct.unpack_from('>9I',cd)
  hsize,htype,platform,page=struct.unpack_from('>4B',cd,36)
  assert magic==0xfade0c02 and length==len(cd) and flags&2 and hsize==32 and htype==2 and page in (12,14) # 4 KiB pages, or 16 KiB from Apple codesign on arm64
  assert cd[ioff:].split(b'\0')[0].decode()==identifier
  # Slot 1 Info.plist, 2 requirements, 3 resource seal; a standalone helper binds requirements only.
  special=((1,info),(2,blobs[2]),(3,res)) if bundle else ((1,None),(2,blobs[2]))
  assert nspecial>=len(special) # Apple codesign may add empty entitlement/DER slots
  for slot,data in special:
   assert (hashlib.sha256(data).digest() if data is not None else bytes(32))==cd[hoff-slot*32:hoff-(slot-1)*32],f'special slot {slot}'
  size=1<<page;assert nslots==(limit+size-1)//size
  for j in range(nslots):assert hashlib.sha256(b[j*size:min((j+1)*size,limit)]).digest()==cd[hoff+j*32:hoff+(j+1)*32],f'code page {j}'
  arch='arm64' if cpu==0x100000c else 'x86_64';archs.append(arch);cdhashes.append(hashlib.sha256(cd).digest()[:20])
  if bundle:print(f'PASS {arch}: macOS 13+, PIE, LC_MAIN, only system imports, {nslots} verified code pages, 3 signed special slots')
  else:print(f'PASS {path.name} {arch}: macOS 13+, PIE, LC_MAIN, libSystem only, {nslots} verified code pages, signed requirements')
 assert set(archs)=={'arm64','x86_64'}
 return cdhashes

check(exe,metadata['CFBundleIdentifier'],True,False)
nested={'MacOS/appdeck-proxy':check(helper,'local.appdeck.proxy',False,True)}
# Resources are sealed by SHA-256; nested code (the helper) by its cdhash and a requirement naming every slice.
for rel,v in resdata['files2'].items():
 if 'cdhash' in v:assert rel in nested and v['cdhash'] in nested[rel] and all(f'H"{h.hex()}"' in v['requirement'] for h in nested[rel]),rel
 else:assert hashlib.sha256((contents/rel).read_bytes()).digest()==v['hash2'],rel
assert all(rel in resdata['files2'] for rel in nested),'the proxy bridge is not sealed into the bundle'
# Localized resources (*.lproj) are sealed as {hash, optional}; the others as a plain SHA-1.
for rel,v in resdata['files'].items():assert hashlib.sha1((contents/rel).read_bytes()).digest()==(v['hash'] if isinstance(v,dict) else v),rel
print('PASS Universal containers, Info.plist, resource SHA-1/SHA-256, nested proxy bridge seal, ad-hoc CodeDirectories.')
print('This static check does not run Apple codesign, Gatekeeper, AppKit or any account test.')
