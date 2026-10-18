import sys,re,datetime,statistics as st,collections
ctrl={}; lat=collections.defaultdict(list); times=[]
for l in open(sys.argv[1]):
    m=re.match(r'(\S+)Z virtio_gpu_fence_(ctrl|resp) fence 0x([0-9a-f]+)(?:, type 0x([0-9a-f]+))?',l)
    if not m: continue
    t=datetime.datetime.fromisoformat(m[1]).timestamp()*1000
    if m[2]=='ctrl': ctrl[m[3]]=(t,m[4])
    elif m[3] in ctrl:
        t0,ty=ctrl.pop(m[3]); lat[ty].append(t-t0); times.append(t0)
for ty,v in sorted(lat.items()):
    q=st.quantiles(v,n=10) if len(v)>2 else [0]*9
    print("type 0x%s n=%d p10=%.2f p50=%.2f p90=%.2f max=%.2f under2ms=%d"%(ty,len(v),q[0],st.median(v),q[8],max(v),sum(x<2 for x in v)))
