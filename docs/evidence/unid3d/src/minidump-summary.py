import struct,sys
b=open(sys.argv[1],'rb').read()
sig,ver,n,rva=struct.unpack_from('<4sIII',b,0)
streams={}
for i in range(n):
    t,sz,r=struct.unpack_from('<III',b,rva+12*i); streams.setdefault(t,[]).append((sz,r))
sz,r=streams[6][0]
tid,_=struct.unpack_from('<II',b,r)
code,flags,rec,addr,np,_=struct.unpack_from('<IIQQII',b,r+8)
info=struct.unpack_from('<15Q',b,r+8+32)
csz,crva=struct.unpack_from('<II',b,r+8+32+120)
print('tid',tid,'code',hex(code),'addr',hex(addr),'info',[hex(x) for x in info[:np]],'ctxsize',csz)
if csz==0x2cc:
    edi,esi,ebx,edx,ecx,eax,ebp,eip,cs,efl,esp=struct.unpack_from('<11I',b,crva+0x9c)
    print('eax',hex(eax),'ebx',hex(ebx),'ecx',hex(ecx),'edx',hex(edx),'esi',hex(esi),'edi',hex(edi),'ebp',hex(ebp),'esp',hex(esp),'eip',hex(eip))
# memory list: find stack memory around esp
def readmem(a,ln):
    for t in (5,9):
        for s,rr in streams.get(t,[]):
            if t==5:
                cnt=struct.unpack_from('<I',b,rr)[0]
                for i in range(cnt):
                    st,ds,dr=struct.unpack_from('<QII',b,rr+4+16*i)
                    if st<=a<st+ds: return b[dr+(a-st):dr+(a-st)+ln]
    return None
if csz==0x2cc:
    m=readmem(esp,256)
    if m: print('stack', ' '.join(hex(x) for x in struct.unpack_from('<64I',m)))
# modules
for s,rr in streams.get(4,[]):
    cnt=struct.unpack_from('<I',b,rr)[0]
    for i in range(cnt):
        base,size=struct.unpack_from('<QI',b,rr+4+108*i)
        nrva=struct.unpack_from('<I',b,rr+4+108*i+20)[0]
        ln=struct.unpack_from('<I',b,nrva)[0]; name=b[nrva+4:nrva+4+ln].decode('utf-16le')
        if any(k in name.lower() for k in ('viogpu','heaven','unigine','ntdll','verifier')): print(hex(base),hex(size),name)
