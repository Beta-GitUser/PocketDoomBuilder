#!/usr/bin/env python3
"""Decode the CI signing key and hand it to Gradle.

The keystore itself is stored in the repository secrets as base64 text
(`PDB_KEYSTORE_BASE64`) because Actions secrets are text, not files. This writes
it to a scratch file outside the checkout and exports the variables
`app/build.gradle` looks for.

With no secret configured this is a no-op and the build is signed with the
throwaway debug key, exactly as before - so forks and pull requests from forks
(which never see secrets) keep building.

Run by .github/workflows/build_android.yml; safe to run locally too.
"""

import base64
import binascii
import os
import pathlib
import sys

OUT = pathlib.Path(os.environ.get("RUNNER_TEMP", "/tmp")) / "pdb-release.jks"


def fail(message: str) -> None:
    print(f"setup_signing: {message}", file=sys.stderr)
    sys.exit(1)


def main() -> int:
    encoded = os.environ.get("PDB_KEYSTORE_BASE64", "").strip()
    if not encoded:
        print("setup_signing: no PDB_KEYSTORE_BASE64 secret - using the debug key.")
        print("setup_signing: installs will not replace each other until you add one.")
        return 0

    # Tolerate the base64 being wrapped by whichever editor produced it.
    encoded = "".join(encoded.split())
    try:
        blob = base64.b64decode(encoded, validate=True)
    except (binascii.Error, ValueError) as error:
        fail(f"PDB_KEYSTORE_BASE64 is not valid base64 ({error})")

    # A JKS and a PKCS#12 keystore are both single binary blobs that start with
    # a magic number; catching a truncated paste here saves a confusing Gradle
    # error several minutes later.
    if blob[:4] not in (b"\xfe\xed\xfe\xed", b"\xce\xce\xce\xce") and blob[:4] != b"\x30\x82":
        fail("PDB_KEYSTORE_BASE64 does not decode to a JKS/PKCS#12 keystore")

    missing = [
        name
        for name in ("PDB_KEYSTORE_PASSWORD", "PDB_KEY_ALIAS", "PDB_KEY_PASSWORD")
        if not os.environ.get(name)
    ]
    if missing:
        fail("missing signing secret(s): " + ", ".join(missing))

    OUT.write_bytes(blob)
    OUT.chmod(0o600)
    print(f"setup_signing: wrote {OUT} ({len(blob)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
