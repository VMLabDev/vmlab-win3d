#!/usr/bin/env bash
# Container side of sign-driver.sh. /s holds the key, /pkg the package.
set -euo pipefail

INF=$1
KEY=/s/test-signing.key
CRT=/s/test-signing.crt
SIGN_TIME=$(cat /s/SIGN_TIME)
TMP=$(mktemp -d)

inf_text() { tr -d '\r' <"$INF"; }

CAT_NAME=$(inf_text | sed -n 's/^[[:space:]]*CatalogFile[[:space:]]*=[[:space:]]*\([^[:space:];]*\).*/\1/Ip' | head -1)
[ -n "$CAT_NAME" ] || { echo "no CatalogFile= in $INF" >&2; exit 1; }
# Hardware IDs from the models sections, lower case as Inf2Cat writes them.
HWIDS=$(inf_text | grep -oiE '(PCI|ACPI|USB|ROOT)\\[^ ,;]+' | tr 'A-Z' 'a-z' | sort -u | paste -sd, -)
[ -n "$HWIDS" ] || { echo "no hardware IDs in $INF" >&2; exit 1; }

is_pe() { [ "$(head -c 2 "$1")" = MZ ]; }

echo "== sign PE files"
mapfile -t files < <(find . -type f ! -iname '*.cat' ! -iname '*.pdb' | sed 's|^\./||' | LC_ALL=C sort)
for f in "${files[@]}"; do
    is_pe "$f" || continue
    # osslsigncode will not overwrite an existing output file.
    rm -f "$TMP/sig" "$TMP/unsigned" "$TMP/signed"
    if osslsigncode extract-signature -in "$f" -out "$TMP/sig" >/dev/null 2>&1; then
        osslsigncode remove-signature -in "$f" -out "$TMP/unsigned" >/dev/null
    else
        cp "$f" "$TMP/unsigned"
    fi
    osslsigncode sign -certs "$CRT" -key "$KEY" -h sha256 -time "$SIGN_TIME" \
        -in "$TMP/unsigned" -out "$TMP/signed" >/dev/null
    cat "$TMP/signed" >"$f"
    echo "  signed $f"
done

echo "== catalog $CAT_NAME"
# Catalog member hashes: Authenticode (PE) or flat SHA-1, sorted by hash,
# as gencat.sh does. The GUID is derived from the member list.
entries=()
for f in "${files[@]}"; do
    if strip-pe-image "$f" >/dev/null 2>&1; then
        entries+=("$(basename "$f"):$(strip-pe-image "$f" | sha1sum | cut -d' ' -f1 | tr a-f A-F):PE")
    else
        entries+=("$(basename "$f"):$(sha1sum <"$f" | cut -d' ' -f1 | tr a-f A-F)")
    fi
done
mapfile -t entries < <(printf '%s\n' "${entries[@]}" | LC_ALL=C sort -t: -k2)
h=$(printf '%s\n' "$HWIDS" "${entries[@]}" | sha256sum | cut -c1-32)
GUID="${h:0:8}-${h:8:4}-4${h:13:3}-8${h:17:3}-${h:20:12}"
GEN_TIME=$(date -u -d "@$SIGN_TIME" +%y%m%d%H%M%SZ)
generate-cat-file -G "$GUID" -T "$GEN_TIME" -O _v100_X64 -A 2:10.0 -h "$HWIDS" "${entries[@]}" >"$TMP/unsigned.cat"
rm -f "$TMP/signed.cat"
osslsigncode sign -certs "$CRT" -key "$KEY" -h sha256 -time "$SIGN_TIME" \
    -in "$TMP/unsigned.cat" -out "$TMP/signed.cat" >/dev/null
cat "$TMP/signed.cat" >"$CAT_NAME"
printf '  hwids %s\n' "$HWIDS"
printf '  member %s\n' "${entries[@]}"

echo "== verify"
for f in "${files[@]}" "$CAT_NAME"; do
    if is_pe "$f" || [ "$f" = "$CAT_NAME" ]; then
        rm -f "$TMP/verify.log"
        osslsigncode verify -CAfile "$CRT" -in "$f" >"$TMP/verify.log" 2>&1 \
            || { cat "$TMP/verify.log" >&2; echo "verify failed: $f" >&2; exit 1; }
        echo "  ok $f"
    fi
done
rm -rf "$TMP"
