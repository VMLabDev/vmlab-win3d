#!/usr/bin/env python3
"""Write the results tables in docs/acceptance.md from each template's results.json.

    tests/e2e/report.py

Replaces the text between the `<!-- e2e:results -->` and
`<!-- /e2e:results -->` markers with one table per template that has
docs/evidence/acceptance/<template>/results.json, plus the host stack and
commit that run recorded in host.txt.
"""
import json, os, re, sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOC = os.path.join(REPO, 'docs', 'acceptance.md')
EVROOT = os.path.join(REPO, 'docs', 'evidence', 'acceptance')


def cell(s):
    return str(s).replace('|', '\\|').replace('\n', ' ')


out = []
for t in ('windows-11', 'windows-10'):
    ev = os.path.join(EVROOT, t)
    try:
        res = json.load(open(os.path.join(ev, 'results.json')))
    except OSError:
        continue
    host = {}
    for line in open(os.path.join(ev, 'host.txt')):
        k, _, v = line.partition(': ')
        host[k.strip()] = v.strip()
    stage = ''
    m = re.search(r'^stage: (.*)$', open(os.path.join(ev, 'results.txt')).read(), re.M)
    if m:
        stage = m.group(1)
    rows = res['rows']
    counted = [r for r in rows if r['counts']]
    passed = sum(1 for r in counted if r['result'] == 'pass')
    out.append(f'### {t}\n')
    out.append(f"Run {host.get('date', '?')} at commit `{host.get('commit', '?')[:12]}`, template "
               f"`{host.get('template', '?')}`. **{'PASS' if res['passed'] else 'FAIL'}: {passed} of "
               f"{len(counted)} Done checks pass** (run stage: {stage or '?'}). Evidence: "
               f"`docs/evidence/acceptance/{t}/`; `results.txt` is the script's own table.\n")
    out.append('| Done item | Check | Result | Evidence | Note |')
    out.append('| --- | --- | --- | --- | --- |')
    for r in rows:
        item = r['item'] if r['counts'] else f"{r['item']} (extra)"
        res_s = '**pass**' if r['result'] == 'pass' else '**FAIL**'
        evid = ', '.join(f'`{e.strip()}`' for e in r['evidence'].split(','))
        out.append(f"| {item} | {cell(r['check'])} | {res_s} | {evid} | {cell(r['note'])} |")
    out.append('')

doc = open(DOC).read()
new = '<!-- e2e:results -->\n' + '\n'.join(out) + '<!-- /e2e:results -->'
doc2 = re.sub(r'<!-- e2e:results -->.*?<!-- /e2e:results -->', lambda _: new, doc, flags=re.S)
if doc2 == doc and new not in doc:
    sys.exit('markers not found in docs/acceptance.md')
open(DOC, 'w').write(doc2)
print(f'updated {DOC}')
