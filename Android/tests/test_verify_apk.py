#!/usr/bin/env python3
"""Self-test for Android/tools/verify_apk.py.

Builds synthetic APKs (plain zips) with known defects and checks that the
verifier accepts the good one and rejects each broken one for the right reason.
Runs on the host, no Android SDK needed.

Usage: python3 Android/tests/test_verify_apk.py
"""

import os
import pathlib
import subprocess
import sys
import tempfile
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
VERIFY = ROOT / "Android" / "tools" / "verify_apk.py"

CORE = ("AndroidManifest.xml", "classes.dex", "resources.arsc")
ASSETS = ("assets/icons.txt", "assets/Arimo-Regular.ttf", "assets/Arimo-Bold.ttf")


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
        # Real v2/v3 APKs carry an "APK Signing Block" just before the
        # central directory; appending the magic is enough to detect it.
        with open(path, "ab") as f:
            f.write(b"\0" * 16 + b"APK Sig Block 42" + b"\0" * 16)


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

        expect("rejects a non-APK file", run(ROOT / "README.md", "arm64-v8a"), False)

    print("%d checks, %d failures" % (checks, failures))
    if failures:
        print("FAILED")
        return 1
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
