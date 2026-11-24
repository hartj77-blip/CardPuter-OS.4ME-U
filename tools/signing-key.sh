#!/usr/bin/env bash
# Make the Ed25519 key that signs ZeroOS updates and app lists (docs/UPDATE.md,
# "Signatures"). Run it once, on your own computer, from the repository root:
#
#     tools/signing-key.sh ~/zeroos-release-key.pem
#
# It writes the private key to the path given (keep it safe and out of any
# repository: whoever has it can sign updates) and the public key to
# image/stage-zeroos/files/keys/zeroos-release.pem, which you commit.
# Then give the private key to the release workflows of both repositories, as
# the secret ZEROOS_SIGNING_KEY of their "release" environment:
#
#     gh secret set ZEROOS_SIGNING_KEY --env release --repo hartj77-blip/CardPuter-OS.4ME-U < ~/zeroos-release-key.pem
#     gh secret set ZEROOS_SIGNING_KEY --env release --repo hartj77-blip/CardPuter-Apps.4ME-U < ~/zeroos-release-key.pem
#
# Order matters: set the secrets first, then publish a new Apps release (Actions
# > Release > Run workflow in the Apps repository), then merge the public key
# and release ZeroOS. Devices start checking once they have that ZeroOS.
set -euo pipefail
OUT=${1:?usage: tools/signing-key.sh <where-to-put-the-private-key.pem>}
PUB=image/stage-zeroos/files/keys/zeroos-release.pem
[ -d image/stage-zeroos/files/keys ] || { echo "run it from the repository root" >&2; exit 1; }
[ -e "$OUT" ] && { echo "$OUT exists; not overwriting it" >&2; exit 1; }
case $(cd "$(dirname "$OUT")" && pwd)/ in
    "$(pwd)"/*) echo "put the private key outside this repository" >&2; exit 1 ;;
esac
( umask 077; openssl genpkey -algorithm ed25519 -out "$OUT" )
openssl pkey -in "$OUT" -pubout -out "$PUB"
echo "private key: $OUT  (secret: never commit it)"
echo "public key:  $PUB  (commit this)"
