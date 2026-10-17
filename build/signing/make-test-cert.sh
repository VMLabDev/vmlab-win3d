#!/usr/bin/env bash
# Create this repo's test code-signing certificate and key, once.
#
#   build/signing/make-test-cert.sh
#
# Writes .signing/ at the repo root (never committed; the directory carries
# its own .gitignore):
#   test-signing.key   RSA 3072 private key, mode 0600
#   test-signing.crt   self-signed certificate, PEM
#   test-signing.cer   the same certificate, DER, for certutil on the guest
#   SIGN_TIME          the fixed signing time sign-driver.sh stamps (epoch)
#   CERT-INFO.txt      subject, validity and thumbprints
#
# Running it again with the files present changes nothing. Delete .signing/
# to start over; every package signed with the old key must then be re-signed.
set -euo pipefail
source "$(dirname "$0")/tools.sh"

SUBJECT="/CN=vmlab-win3d Test Driver Signing/O=vmlab-win3d local test (not for distribution)"
DAYS=3650

if [ -f "$SIGNING_DIR/test-signing.key" ] && [ -f "$SIGNING_DIR/test-signing.crt" ]; then
    echo "test certificate exists: $SIGNING_DIR"
    cat "$SIGNING_DIR/CERT-INFO.txt"
    exit 0
fi

build_tools_image
mkdir -p "$SIGNING_DIR"
chmod 700 "$SIGNING_DIR"
printf '*\n' >"$SIGNING_DIR/.gitignore"

# Code signing EKU, digitalSignature, no basicConstraints: the same shape as
# a New-SelfSignedCertificate -Type CodeSigningCert certificate. The guest
# trusts it by importing it into Root and TrustedPublisher.
run_tool -v "$SIGNING_DIR:/s" -- bash -c '
set -euo pipefail
umask 077
cat >/tmp/ext.cnf <<EOC
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
[ext]
keyUsage = critical, digitalSignature
extendedKeyUsage = codeSigning
subjectKeyIdentifier = hash
EOC
openssl req -x509 -newkey rsa:3072 -sha256 -nodes -days '"$DAYS"' \
    -subj "'"$SUBJECT"'" -config /tmp/ext.cnf \
    -keyout /s/test-signing.key -out /s/test-signing.crt 2>/dev/null
openssl x509 -in /s/test-signing.crt -outform DER -out /s/test-signing.cer
chmod 644 /s/test-signing.crt /s/test-signing.cer
# Sign with a fixed time one hour after notBefore, so re-signing the same
# input gives the same bytes.
nb=$(openssl x509 -in /s/test-signing.crt -noout -startdate | cut -d= -f2)
echo $(( $(date -u -d "$nb" +%s) + 3600 )) >/s/SIGN_TIME
{
    echo "subject:        $(openssl x509 -in /s/test-signing.crt -noout -subject -nameopt RFC2253 | cut -d= -f2-)"
    echo "notBefore:      $(openssl x509 -in /s/test-signing.crt -noout -startdate | cut -d= -f2)"
    echo "notAfter:       $(openssl x509 -in /s/test-signing.crt -noout -enddate | cut -d= -f2)"
    echo "serial:         $(openssl x509 -in /s/test-signing.crt -noout -serial | cut -d= -f2)"
    echo "sha1 thumbprint: $(openssl x509 -in /s/test-signing.crt -noout -fingerprint -sha1 | cut -d= -f2 | tr -d :)"
    echo "sha256:         $(openssl x509 -in /s/test-signing.crt -noout -fingerprint -sha256 | cut -d= -f2 | tr -d :)"
    echo "sign time:      $(date -u -d @$(cat /s/SIGN_TIME) +%Y-%m-%dT%H:%M:%SZ)"
} >/s/CERT-INFO.txt
'
echo "created test certificate: $SIGNING_DIR"
cat "$SIGNING_DIR/CERT-INFO.txt"
