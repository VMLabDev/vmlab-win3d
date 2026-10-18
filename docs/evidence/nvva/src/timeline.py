# For each minute with NVRM "can't alloc VA space" or Xid kernel lines since
# argv[1] (local time), list the win3d NVIDIA labs (name contains "nv") that
# were between vm.starting and vm.stopped in their vmlab lab-daemon logs.
#   python3 timeline.py "2026-10-19 01:00"
import datetime as dt,glob,os,re,subprocess,sys
ev=[]
for f in glob.glob(os.path.expanduser("~/.local/state/vmlab/labd-win3d-*.log")):
    n=os.path.basename(f)[5:-4]
    for l in open(f,errors="replace"):
        l=re.sub(r"\x1b\[[0-9;]*m","",l)
        m=re.match(r"^(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d).*event=(vm\.starting|vm\.stopped)",l)
        if m: ev.append([n,m.group(1),m.group(2)])
iv={};open_={}
for n,t,e in sorted(ev,key=lambda x:x[1]):
    T=dt.datetime.fromisoformat(t)+dt.timedelta(hours=10)
    if e=="vm.starting": open_[n]=T
    elif n in open_: iv.setdefault(n,[]).append((open_.pop(n),T))
now=dt.datetime.now()
for n,T in open_.items(): iv.setdefault(n,[]).append((T,now))
out=subprocess.run("journalctl -k --since '"+sys.argv[1]+"' -o short-iso | grep -E 'alloc VA space|Xid'",shell=True,capture_output=True,text=True).stdout
bursts={}
for l in out.splitlines():
    t=dt.datetime.fromisoformat(l.split()[0][:19]); k=t.replace(second=0)
    kind='VA' if 'VA space' in l else 'Xid'+(re.search(r'PCI:0000:(\d\d)',l).group(1))
    bursts[(k,kind)]=bursts.get((k,kind),0)+1
for (k,kind),c in sorted(bursts.items()):
    run=[n for n,ivs in iv.items() if any(a<=k+dt.timedelta(seconds=59) and b>=k for a,b in ivs) and re.search('nv',n)]
    print(k.strftime('%H:%M'),kind,c,' '.join(sorted(run)))
