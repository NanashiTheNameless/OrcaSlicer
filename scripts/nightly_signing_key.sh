#!/usr/bin/env bash
# Creates the Ed25519 key that signs the nightly manifest (docs/HLSD/nightly_updates.md).
#
#   scripts/nightly_signing_key.sh <private-key.pem>
#
# Store the private key as the NIGHTLY_MANIFEST_KEY repository secret, keep a copy
# offline, and delete the file. Put the public key it prints into
# NIGHTLY_MANIFEST_PUBLIC_KEY in src/slic3r/GUI/NightlyUpdater.cpp.
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "usage: $0 <private-key.pem>" >&2
    exit 2
fi
if [ -e "$1" ]; then
    echo "$1 already exists; not overwriting a key" >&2
    exit 1
fi

umask 077
openssl genpkey -algorithm ed25519 -out "$1"
# The DER public key ends with the 32 raw key bytes, which is what the app reads.
echo "Public key: $(openssl pkey -in "$1" -pubout -outform DER | tail -c 32 | base64)"
