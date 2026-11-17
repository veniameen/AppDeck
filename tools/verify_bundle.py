#!/usr/bin/env python3
"""Static Mach-O / ad-hoc signature verification; NOT Apple's codesign/Gatekeeper."""
from pathlib import Path
import hashlib, plistlib, struct, sys, tarfile
app=Path(sys.argv[1]);contents=app/'Contents';exe=contents/'MacOS/AppDeck';helper=contents/'MacOS/appdeck-proxy';vless=contents/'MacOS/appdeck-vless'
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
  assert cpu in (0x1000007,0x100000c),f'unsupported architecture in {path}'
  assert offset%(1<<align)==0 and offset+size<=len(binary)
  b=binary[offset:offset+size];assert struct.unpack_from('<I',b)[0]==0xfeedfacf
  assert struct.unpack_from('<I',b,4)[0]==cpu
  assert struct.unpack_from('<I',b,12)[0]==2 # MH_EXECUTE
  flags=struct.unpack_from('<I',b,24)[0];assert flags&0x200000 # PIE
  n,sz=struct.unpack_from('<II',b,16);pos=32;deps=[];sig=None;minimum=None;entry=None
  for _ in range(n):
   cmd,cmdsize=struct.unpack_from('<II',b,pos);assert cmdsize>=8
   # Include weak, re-exported, upward and lazy imports; none may evade the system-library policy.
   if cmd in (0xc,0x80000018,0x8000001f,0x80000023,0x20):
    noff=struct.unpack_from('<I',b,pos+8)[0];deps.append(b[pos+noff:pos+cmdsize].split(b'\0')[0].decode())
   if cmd==0x32:minimum=struct.unpack_from('<III',b,pos+8);assert minimum[:2]==(1,13<<16)
   if cmd==0x80000028:entry=struct.unpack_from('<Q',b,pos+8)[0]
   if cmd==0x1d:
    so,ss=struct.unpack_from('<II',b,pos+8);sig=b[so:so+ss];assert len(sig)==ss
   pos+=cmdsize
  assert pos==32+sz and minimum and entry and sig
  if libsystem_only: # the proxy bridge: libSystem and nothing else from outside /usr/lib (no AppKit, no frameworks)
   assert '/usr/lib/libSystem.B.dylib' in deps and all(d.startswith('/usr/lib/') for d in deps),deps
  elif bundle:
   # Cross builds import libSystem/libobjc only; a local Apple-clang build also links the system AppKit/libc++.
   assert {'/usr/lib/libSystem.B.dylib','/usr/lib/libobjc.A.dylib'}<=set(deps)
   assert all(d.startswith('/usr/lib/') or d.startswith('/System/Library/Frameworks/') for d in deps),deps
  else:
   # The separately built Go helper may import system frameworks, never bundled/shared third-party libraries.
   assert '/usr/lib/libSystem.B.dylib' in deps
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
  else:print(f'PASS {path.name} {arch}: macOS 13+, PIE, LC_MAIN, {"libSystem only" if libsystem_only else "only system imports"}, {nslots} verified code pages, signed requirements')
 assert set(archs)=={'arm64','x86_64'}
 return cdhashes

check(exe,metadata['CFBundleIdentifier'],True,False)
nested={'MacOS/appdeck-proxy':check(helper,'local.appdeck.proxy',False,True),
        'MacOS/appdeck-vless':check(vless,'local.appdeck.vless',False,False)}
# Resources are sealed by SHA-256; nested code by its cdhash and a requirement naming every slice.
for rel,v in resdata['files2'].items():
 if 'cdhash' in v:assert rel in nested and v['cdhash'] in nested[rel] and all(f'H"{h.hex()}"' in v['requirement'] for h in nested[rel]),rel
 else:assert hashlib.sha256((contents/rel).read_bytes()).digest()==v['hash2'],rel
assert all(rel in resdata['files2'] for rel in nested),'a network helper is not sealed into the bundle'
# Localized resources (*.lproj) are sealed as {hash, optional}; the others as a plain SHA-1.
for rel,v in resdata['files'].items():assert hashlib.sha1((contents/rel).read_bytes()).digest()==(v['hash'] if isinstance(v,dict) else v),rel
license_dir=contents/'Resources/vless'
assert (license_dir/'LICENSE').stat().st_size>0
license_text=(license_dir/'COPYING').read_text()
assert 'GNU GENERAL PUBLIC LICENSE' in license_text and 'Version 3, 29 June 2007' in license_text and 'END OF TERMS AND CONDITIONS' in license_text
assert (license_dir/'THIRD_PARTY_NOTICES.md').stat().st_size>0
with tarfile.open(license_dir/'source.tar.gz','r:gz') as source:
 names=source.getnames()
 assert names and all(not Path(name).is_absolute() and '..' not in Path(name).parts for name in names)
 for required in ('go.mod','go.sum','main.go','COPYING','vendor/modules.txt','build-vless.sh'):
  assert any(name==required or name.endswith('/'+required) for name in names),f'missing VLESS corresponding source: {required}'
print('PASS Universal containers, Info.plist, resource SHA-1/SHA-256, nested network helper seals, ad-hoc CodeDirectories, VLESS licenses/source.')
print('This static check does not run Apple codesign, Gatekeeper, AppKit or any account test.')
