#!/usr/bin/env bash
# Sign release lists for zeroos-verify (docs/UPDATE.md, "Signatures"):
# writes <file>.sig, an Ed25519 signature, next to each file named.
#
# Usage: tools/sign-release.sh <public-key.pem> <file>...
# Env:   ZEROOS_SIGNING_KEY  the private key (PEM text), from the "release"
#                            environment's secret of that name
#
# Once the public key is in ZeroOS (image/stage-zeroos/files/keys/
# zeroos-release.pem), devices refuse unsigned lists, so a release without
# the private key, or with one that doesn't match, stops here. Before that,
# a release without the secret goes out unsigned, as before.
set -euo pipefail
PUB=$1
shift
if [ -z "${ZEROOS_SIGNING_KEY:-}" ]; then
    if [ -s "$PUB" ]; then
        echo "error: ZeroOS ships a release key ($PUB) but the ZEROOS_SIGNING_KEY secret isn't set;" >&2
        echo "       devices would refuse this release. Add the secret to the 'release' environment." >&2
        exit 1
    fi
    echo "::notice::No signing key yet (tools/signing-key.sh): publishing unsigned lists, as before."
    exit 0
fi
command -v openssl >/dev/null || { echo "error: openssl is missing" >&2; exit 1; }
KEY=$(mktemp)
trap 'rm -f "$KEY" "$KEY.pub"' EXIT
chmod 600 "$KEY"
printf '%s\n' "$ZEROOS_SIGNING_KEY" > "$KEY"
openssl pkey -in "$KEY" -pubout -out "$KEY.pub" 2>/dev/null ||
    { echo "error: ZEROOS_SIGNING_KEY isn't a private key openssl can read" >&2; exit 1; }
if [ -s "$PUB" ]; then
    # The key devices trust must be the one we sign with.
    cmp -s <(openssl pkey -pubin -in "$PUB" -outform DER) <(openssl pkey -pubin -in "$KEY.pub" -outform DER) ||
        { echo "error: ZEROOS_SIGNING_KEY doesn't match $PUB, the key devices check with" >&2; exit 1; }
else
    echo "::warning::Signing, but no public key at $PUB yet: devices won't check these signatures."
fi
for f in "$@"; do
    openssl pkeyutl -sign -inkey "$KEY" -rawin -in "$f" -out "$f.sig"
    openssl pkeyutl -verify -pubin -inkey "$KEY.pub" -rawin -in "$f" -sigfile "$f.sig" >/dev/null
    echo "signed $f"
done
