#!/usr/bin/env python3
"""Judge the Done items of docs/brief.md from one run's evidence directory.

    check.py <evidence-dir>

Reads what tests/e2e/run.sh collected, writes results.json and results.txt
into the directory, prints the pass/fail table, and exits 1 when any Done
item fails (rows marked "extra" are reported but do not count).
"""
import json, os, re, sys

EV = sys.argv[1]
ALLOWED = {'system32', 'syswow64', 'system_subdir', 'driverstore'}
# The module each API must load to count as running on our stack.
STACK = {
    'd3d9': [r'\\viogpu_d3d10\.dll$'],
    'd3d11': [r'\\viogpu_d3d10\.dll$'],
    'd3d12': [r'\\d3d12\.dll$', r'\\vmlab-win3d\\vkd3dcore\.dll$', r'\\libvulkan_virtio\.dll$'],
    'opengl': [r'\\viogpu_wgl\.dll$', r'\\libvulkan_virtio\.dll$'],
    'vulkan': [r'\\libvulkan_virtio\.dll$'],
}
ADAPTER = {'d3d9': 'Hardsoft VirtIO GPU 3D', 'd3d11': 'Hardsoft VirtIO GPU 3D', 'd3d12': 'Hardsoft VirtIO GPU 3D',
           'opengl': 'Virtio-GPU Venus', 'vulkan': 'Virtio-GPU Venus'}
API_NAME = {'d3d9': 'Direct3D 9', 'd3d11': 'Direct3D 11', 'd3d12': 'Direct3D 12', 'opengl': 'OpenGL', 'vulkan': 'Vulkan'}

rows = []


def p(*parts):
    return os.path.join(EV, *parts)


def load(*parts):
    try:
        with open(p(*parts), encoding='utf-8-sig') as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def read(*parts):
    try:
        with open(p(*parts), encoding='utf-8-sig', errors='replace') as f:
            return f.read()
    except OSError:
        return None


def row(item, check, ok, evidence, note, extra=False):
    rows.append({'item': item, 'check': check, 'result': 'pass' if ok else 'FAIL', 'evidence': evidence,
                 'note': note, 'counts': not extra})


# 1. One script builds the package.
b = read('build.txt') or ''
m = re.search(r'^mode: (\S+)', b, re.M)
rc = re.search(r'^exit: (\d+)', b, re.M)
row('1', 'scripts/build.sh builds the package', bool(rc and rc.group(1) == '0'), 'build.txt',
    f"{m.group(1) if m else '?'}; exit {rc.group(1) if rc else '?'}")

# 2. Install with the documented command.
log = read('install.log') or ''
irc = re.search(r'^exit: (\d+)', read('install.rc') or '', re.M)
reboots = len(re.findall(r'state: RebootPending', log))
row('2', 'documented install command (scripts/install-into-vm.sh)', bool(irc and irc.group(1) == '0' and 'done: Installed' in log),
    'install.log', f"exit {irc.group(1) if irc else '?'}; {'Installed' if 'done: Installed' in log else 'not Installed'}")

st = load('installed-gpu-state.json')
if st is not None:
    devs = st.get('display_devices') or []
    ok = any('Hardsoft VirtIO GPU 3D' in (dv.get('name') or '') and dv.get('problem_code') == 0 for dv in devs)
    row('2', 'display device on our driver, problem code 0', ok, 'installed-gpu-state.json',
        'Hardsoft VirtIO GPU 3D controller' if ok else 'device not on viogpu3d or has a problem code', extra=True)

# 2. vulkaninfo lists the Venus device.
for arch in ('x64', 'x86'):
    v = read('vulkaninfo', f'vulkaninfo-{arch}.txt') or ''
    names = re.findall(r'deviceName\s*=\s*(.+)', v)
    venus = [n.strip() for n in names if 'Venus' in n]
    types = re.findall(r'deviceType\s*=\s*(\S+)', v)
    ok = bool(venus) and 'PHYSICAL_DEVICE_TYPE_CPU' not in types[:1]
    row('2', f'vulkaninfo --summary ({arch}) lists the Venus device', ok, f'vulkaninfo/vulkaninfo-{arch}.txt',
        venus[0] if venus else ('no Venus device: ' + (names[0].strip() if names else 'no device')), extra=(arch != 'x64'))

# 2. Each API: renders on the GPU, no app-local DLLs, visible on the console.
shots = load('shots.json') or {}
for api in ('d3d9', 'd3d11', 'd3d12', 'opengl', 'vulkan'):
    for arch in ('x64', 'x86'):
        name = f'{arch}-{api}_probe'
        run = load('probes', f'{name}.run.json')
        res = load('probes', f'{name}.json')
        why = []
        if run is None or res is None:
            why.append('no result')
        else:
            if run.get('exit_code') != 0 or res.get('result') != 'pass':
                why.append(f"exit {run.get('exit_code')}, {res.get('result')}: {'; '.join(res.get('errors') or [])[:120]}")
            ad = res.get('adapter') or {}
            if ad.get('software') is not False:
                why.append('software renderer: ' + str(ad.get('software_reason')))
            if ADAPTER[api] not in (ad.get('name') or ''):
                why.append(f"adapter {ad.get('name')!r}")
            if not (res.get('readback') or {}).get('ok'):
                why.append('readback mismatch')
            if run.get('app_local_modules'):
                why.append('app-local modules: ' + ', '.join(run['app_local_modules']))
            gm = run.get('graphics_modules') or []
            bad = [g for g in gm if (re.search(r'\[(\w+)\]', g) or [None, 'other'])[1] not in ALLOWED]
            if bad:
                why.append('graphics modules outside system paths: ' + ', '.join(bad))
            paths = [g.split(' [')[0] for g in gm]
            for pat in STACK[api]:
                if not any(re.search(pat, x, re.I) for x in paths):
                    why.append(f'did not load {pat}')
        sh = shots.get(name) or {}
        if not sh.get('visible'):
            why.append('probe window not found in any console capture')
        note = '; '.join(why) if why else (
            f"{res['adapter']['name']}, {res.get('avg_fps')} fps, readback ok, "
            f"{len(run.get('graphics_modules') or [])} graphics modules all in system paths, 0 app-local; "
            f"console: window found{', animating' if sh.get('animating') else ''}")
        ev = f'probes/{name}.run.json, probes/{name}.modules.txt, {sh.get("png") or "(no png)"}'
        row('2', f'{API_NAME[api]} ({arch}) renders on the GPU and shows on the console', not why, ev, note,
            extra=(api == 'vulkan'))

# 2. dxdiag reports hardware acceleration on our driver.
d = load('dxdiag', 'dxdiag.json')
s = (d or {}).get('summary') or {}
ours = bool(re.search(r'viogpu3d|Hardsoft', f"{s.get('card_name')} {s.get('driver_name')}", re.I))
row('2', 'dxdiag reports hardware acceleration on our driver', bool(s.get('hardware_acceleration')) and ours,
    'dxdiag/dxdiag.json', f"hardware_acceleration={s.get('hardware_acceleration')}, card {s.get('card_name')!r}, "
    f"D3D {s.get('direct3d_acceleration')}, DDraw {s.get('directdraw_acceleration')}, {s.get('driver_model')}")

failed = [r for r in rows if r['counts'] and r['result'] != 'pass']
out = {'evidence_dir': EV, 'passed': not failed, 'rows': rows}
with open(p('results.json'), 'w') as f:
    json.dump(out, f, indent=2)
lines = [f"{'Item':<5} {'Result':<6} Check", '-' * 78]
for r in rows:
    lines.append(f"{r['item'] + ('' if r['counts'] else '+'):<5} {r['result']:<6} {r['check']}")
    lines.append(f"{'':<12} {r['note']}")
lines += ['-' * 78, f"{'PASS' if not failed else 'FAIL'}: {sum(1 for r in rows if r['counts']) - len(failed)} of "
          f"{sum(1 for r in rows if r['counts'])} Done checks pass ('+' rows are extra and do not count)"]
text = '\n'.join(lines) + '\n'
with open(p('results.txt'), 'w') as f:
    f.write(text)
sys.stdout.write(text)
sys.exit(1 if failed else 0)
