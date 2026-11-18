#!/usr/bin/env python3
"""Self-test for Android/tools/verify_apk.py.

Builds synthetic APKs (plain zips) with known defects and checks that the
verifier accepts the good one and rejects each broken one for the right reason.
Runs on the host, no Android SDK needed.

Usage: python3 Android/tests/test_verify_apk.py
"""

import hashlib
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
VERIFY = ROOT / "Android" / "tools" / "verify_apk.py"

CORE = ("AndroidManifest.xml", "classes.dex", "resources.arsc")
ASSETS = ("assets/icons.txt", "assets/Arimo-Regular.ttf", "assets/Arimo-Bold.ttf")


# A tiny (fake) DER certificate, used to prove the verifier can read a real
# APK Signing Block back out of an APK.
FAKE_CERT = bytes.fromhex(
    "3059301306072a8648ce3d020106082a8648ce3d030107034200")
FAKE_CERT_SHA256 = hashlib.sha256(FAKE_CERT).hexdigest()
FAKE_CERT_SHA1 = hashlib.sha1(FAKE_CERT).hexdigest()


def _lp(blob):
    """Length-prefix a blob, the way the APK signing block does."""
    return struct.pack("<I", len(blob)) + blob


def _signing_block(cert=FAKE_CERT):
    """Build a real APK Signing Block (v2 scheme) carrying `cert`.

    Layout: [size][id-value pairs][size][magic], where each pair is
    [8-byte length][4-byte id][value]. The v2 signature value is a
    length-prefixed sequence of signer records; each record is
    [len][signed data][signatures][public key], and the signed data holds
    the digests and the certificates.
    """
    signed_data = _lp(b"\0" * 8) + _lp(_lp(cert))
    record = _lp(signed_data) + _lp(b"\0" * 16) + _lp(b"\0" * 32)
    signers = _lp(record)
    value = _lp(signers)
    pair_body = struct.pack("<I", 0x7109871A) + value  # v2 signature ID
    pair = struct.pack("<Q", len(pair_body)) + pair_body
    size = len(pair) + 8 + 16
    return struct.pack("<Q", size) + pair + struct.pack("<Q", size) + b"APK Sig Block 42"


def build_apk(path, abis=("arm64-v8a",), icons=150, compress=False, signed="v1"):
    mode = zipfile.ZIP_DEFLATED if compress else zipfile.ZIP_STORED
    with zipfile.ZipFile(path, "w") as z:
        for name in CORE:
            z.writestr(name, b"x")
        if signed == "v1":
            z.writestr("META-INF/CERT.RSA", b"x")
            z.writestr("META-INF/CERT.SF", b"x")
        for abi in abis:
            z.writestr("lib/%s/libpocketdoombuilder.so" % abi, b"\0" * 4096)
        for name in ASSETS:
            z.writestr(name, b"x" * 64, compress_type=mode)
        for i in range(icons):
            z.writestr("assets/icons/icon%03d.png" % i, b"x" * 16)
    if signed == "v2":
        # Real v2/v3 APKs carry an "APK Signing Block" just before the central
        # directory. Put it there (not at the end) and fix the EOCD's central
        # directory offset, exactly as a real signer does.
        with open(path, "rb") as f:
            pre = f.read()
        cd = pre.find(b"PK\x01\x02")
        block = _signing_block()
        with open(path, "wb") as f:
            f.write(pre[:cd])
            f.write(block)
            f.write(pre[cd:])
        with open(path, "r+b") as f:
            f.seek(pre.rfind(b"PK\x05\x06") + len(block) + 16)
            f.write(struct.pack("<I", cd + len(block)))


def run(apk, *abis):
    return subprocess.run([sys.executable, str(VERIFY), str(apk)] + list(abis),
                          capture_output=True, text=True)


def main():
    checks = 0
    failures = 0

    def expect(label, result, want_ok, want_text=""):
        nonlocal checks, failures
        checks += 1
        ok = (result.returncode == 0) == want_ok and want_text in result.stdout
        if not ok:
            failures += 1
            print("  FAIL: %s (rc=%d)\n%s" % (label, result.returncode, result.stdout))
        else:
            print("  ok: %s" % label)

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)

        good = tmp / "good.apk"
        build_apk(good)
        expect("accepts a well-formed APK", run(good, "arm64-v8a"), True, "APK check passed")

        two = tmp / "two-abi.apk"
        build_apk(two, abis=("arm64-v8a", "x86_64"))
        expect("accepts both requested ABIs", run(two, "arm64-v8a", "x86_64"), True)

        missing_abi = tmp / "missing-abi.apk"
        build_apk(missing_abi)
        expect("rejects an APK missing a requested ABI",
               run(missing_abi, "arm64-v8a,x86_64"), False, "missing native library for x86_64")

        no_native = tmp / "no-native.apk"
        build_apk(no_native, abis=())
        expect("rejects an APK with no native libraries",
               run(no_native, "arm64-v8a"), False, "no native libraries packaged")

        few_icons = tmp / "few-icons.apk"
        build_apk(few_icons, icons=3)
        expect("rejects an APK that lost the icon set",
               run(few_icons, "arm64-v8a"), False, "only 3 icon files packaged")

        compressed = tmp / "compressed.apk"
        build_apk(compressed, compress=True)
        expect("rejects compressed assets (noCompress missing)",
               run(compressed, "arm64-v8a"), False, "is compressed in the APK")

        unsigned = tmp / "unsigned.apk"
        build_apk(unsigned, signed=None)
        expect("rejects an unsigned APK",
               run(unsigned, "arm64-v8a"), False, "APK is not signed")

        v2 = tmp / "v2-signed.apk"
        build_apk(v2, signed="v2")
        expect("accepts an APK signed with the v2/v3 scheme only",
               run(v2, "arm64-v8a"), True, "signed: v2/v3")

        # The fingerprint must be the certificate that actually signed the APK,
        # so a build can be checked against the keystore that produced it.
        v2_result = run(v2, "arm64-v8a")
        expect("reports the signing certificate's fingerprints",
               v2_result, True, "certificate sha256: " + FAKE_CERT_SHA256)
        expect("reports the certificate's sha1 as well",
               v2_result, True, "certificate sha1:   " + FAKE_CERT_SHA1)

        expect("rejects a non-APK file", run(ROOT / "README.md", "arm64-v8a"), False)

    print("%d checks, %d failures" % (checks, failures))
    if failures:
        print("FAILED")
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
