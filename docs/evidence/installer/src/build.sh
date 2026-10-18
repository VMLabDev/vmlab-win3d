#!/usr/bin/env bash
# Build the driver-pinning repro's inputs (docs/installer.md, "Evidence").
#
#   docs/evidence/installer/src/build.sh <viogpudo-dir>
#
# <viogpudo-dir> holds Red Hat's viogpudo package as the windows-11 template
# has it in its driver store (repro.sh pulls it from a fresh lab; the sha256
# of each file is checked against VIOGPUDO.sha256 next to this script).
# Writes out/installer-repro/:
#   newer-viogpudo/     viogpudo with DriverVer 12/31/2026, later than this
#                       package's 09/27/2026: what a Windows Update viogpudo
#                       released after this build looks like to PnP. Same
#                       hardware ID and FeatureScore (F9) as Red Hat's.
#   hostile-viogpudo/   the same with FeatureScore 00, which outranks any
#                       driver: only the forced install and the pin hold it off.
#   control-bundle/     this checkout's bundle with the installer and INF of
#                       commit b4b8c3f (before the fix): FeatureScore F9, no
#                       forced install, no pin. Same certificate.
# The two viogpudo copies are re-signed with this checkout's test
# certificate. PnP ranks a test signature and Microsoft's WHQL signature the
# same (00 in the rank's signer byte, docs/evidence/kmd/06-setupapi-dev-log.txt),
# so only DriverVer and FeatureScore tell them apart, as for the real one.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../../.." && pwd)
SRC=$(cd "${1:?usage: $0 <viogpudo-dir>}" && pwd)
OUT=$REPO/out/installer-repro
BUNDLE=$REPO/out/package/vmlab-win3d-$(cat "$REPO/installer/VERSION")
BEFORE=b4b8c3f
# FAKES_ONLY=1 builds only the two viogpudo copies, without a bundle.
FAKES_ONLY=${FAKES_ONLY:-0}
[ "$FAKES_ONLY" = 1 ] || [ -f "$BUNDLE/manifest.json" ] || { echo "no bundle at $BUNDLE; run scripts/build.sh" >&2; exit 1; }

(cd "$SRC" && sha256sum -c "$HERE/VIOGPUDO.sha256")
rm -rf "$OUT"
mkdir -p "$OUT"

# fake <name> <driverver> [featurescore]
fake() {
    local d=$OUT/$1
    mkdir -p "$d"
    for f in "$SRC"/*; do
        case $f in *.cat) ;; *) cp "$f" "$d/" ;; esac
    done
    python3 - "$d/viogpudo.inf" "$2" "${3:-}" <<'EOF'
import re, sys
path, ver, score = sys.argv[1:4]
raw = open(path, "rb").read()
text = raw.decode("utf-16") if raw[:2] in (b"\xff\xfe", b"\xfe\xff") else raw.decode("ascii")
lines = text.splitlines()
hits = {"DriverVer": 0, "FeatureScore": 0}
for i, l in enumerate(lines):
    if re.match(r"^\s*DriverVer\s*=", l, re.I):
        lines[i] = "DriverVer = " + ver
        hits["DriverVer"] += 1
    elif score and re.match(r"^\s*FeatureScore\s*=", l, re.I):
        lines[i] = "FeatureScore=" + score
        hits["FeatureScore"] += 1
if hits["DriverVer"] != 1 or (score and hits["FeatureScore"] != 1):
    sys.exit(f"{path}: expected one DriverVer and FeatureScore line, found {hits}")
open(path, "w", newline="").write("\r\n".join(lines) + "\r\n")
EOF
    "$REPO/build/signing/sign-driver.sh" "$d"
}
fake newer-viogpudo 12/31/2026,100.101.104.29900
fake hostile-viogpudo 12/31/2026,100.101.104.29901 00
[ "$FAKES_ONLY" = 1 ] && exit 0

# The control bundle: the bundle as commit $BEFORE would have built it.
C=$OUT/control-bundle
cp -a "$BUNDLE" "$C"
rm -f "$C/manifest.json"
git -C "$REPO" show "$BEFORE:installer/vmlab-win3d.ps1" >"$C/vmlab-win3d.ps1"
git -C "$REPO" show "$BEFORE:build/package/make-inf.py" >"$OUT/make-inf-before.py"
python3 "$OUT/make-inf-before.py" "$REPO/out/kmd/viogpu3d_x64_wow64.inf.template" "$C/driver/viogpu3d.inf"
rm -f "$C/driver/viogpu3d.cat"
grep -q '^FeatureScore=F9' "$C/driver/viogpu3d.inf"
"$REPO/build/signing/sign-driver.sh" "$C/driver"
python3 "$REPO/build/package/manifest.py" "$C" "$(cat "$REPO/installer/VERSION")" "$BEFORE-control" "$OUT/control-bundle.zip"
rm -f "$OUT/control-bundle.zip" "$OUT/make-inf-before.py"
(cd "$OUT" && find . -type f \( -name '*.inf' -o -name '*.cat' \) -exec sha256sum {} + | sort -k2)
