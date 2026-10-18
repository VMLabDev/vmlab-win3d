import csv,sys,statistics as st
rows=list(csv.reader(open(sys.argv[1],encoding='utf-8',errors='replace')))
ev=[]
for r in rows[1:]:
    r=[x.strip() for x in r]
    if r[0]!='Microsoft-Windows-DxgKrnl': continue
    ev.append((int(r[16])/1e4,r[1],r[2],r[19:27]))
ev.sort()
comp=[e[0] for e in ev if e[2]=='177' and 'DMA_COMPLETED' in e[3][4]]
d=[b-a for a,b in zip(comp,comp[1:])]
print("DMA_COMPLETED interrupts:",len(comp),"gaps ms: p10 %.2f p50 %.2f p90 %.2f"%tuple(st.quantiles(d,n=10)[i] for i in (0,4,8)))
import collections
h=collections.Counter(int(x) for x in d); print(sorted(h.items())[:30])
# submit (178 Start, by ctx+fence) -> complete (177 by ctx+fence)
sub={}
lat=[]
for t,typ,eid,u in ev:
    if eid=='178' and typ=='Start': sub[(u[0],u[2])]=t
    if eid=='177': 
        k=(u[0],u[3])
        if k in sub: lat.append((t-sub.pop(k), u[1]))
for kind in set(k for _,k in lat):
    v=[x for x,k in lat if k==kind]
    if len(v)>3: print(kind,len(v),"submit->complete ms p10 %.2f p50 %.2f p90 %.2f"%tuple(st.quantiles(v,n=10)[i] for i in (0,4,8)))
print("per context:")
sub={}; per={}
for t,typ,eid,u in ev:
    if eid=='178' and typ=='Start': sub[(u[0],u[2])]=(t,u[1],u[3],u[4])
    if eid=='177':
        k=(u[0],u[3])
        if k in sub:
            t0,kind,a,b=sub.pop(k); per.setdefault((u[0],kind.strip('" ')),[]).append(t-t0)
for k,v in per.items():
    if len(v)>3: print(k,len(v),"p10 %.2f p50 %.2f p90 %.2f min %.2f"%(st.quantiles(v,n=10)[0],st.median(v),st.quantiles(v,n=10)[8],min(v)))
