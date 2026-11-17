#!/usr/bin/env python3
"""Sanity-check a built PocketDoomBuilder APK.

The APK is a zip, so its contents can be inspected without the Android SDK.
This guards against the classic "the build succeeded but the APK is useless"
failures: the native library missing for the requested ABI, the icon manifest
or fonts not packaged, or the assets being compressed (the native side reads
them straight through AAssetManager, which needs noCompress).

Usage: python3 Android/tools/verify_apk.py <apk> [abi[,abi...]]
"""

import sys
import zipfile

EXPECTED_ASSETS = ("assets/icons.txt", "assets/Arimo-Regular.ttf", "assets/Arimo-Bold.ttf")


def main(argv):
    if len(argv) < 2:
        print("usage: verify_apk.py <apk> [abis]", file=sys.stderr)
        return 2
    apk = argv[1]
    # ABIs may be given comma separated or as separate arguments.
    abis = [a for a in (",".join(argv[2:])).split(",") if a]

    try:
        z_handle = zipfile.ZipFile(apk)
    except (OSError, zipfile.BadZipFile) as error:
        print("PROBLEM: %s is not a readable APK (%s)" % (apk, error))
        print("APK check FAILED")
        return 1

    with z_handle as z:
        names = z.namelist()
        nameset = set(names)

        problems = []
        for required in ("AndroidManifest.xml", "classes.dex", "resources.arsc"):
            if required not in nameset:
                problems.append("missing %s" % required)
        core_ok = not [p for p in problems if "AndroidManifest" in p
                       or "classes.dex" in p or "resources.arsc" in p]

        packaged_abis = sorted({n.split("/")[1] for n in names if n.startswith("lib/")})
        for abi in abis:
            so = "lib/%s/libpocketdoombuilder.so" % abi
            if so not in nameset:
                problems.append("missing native library for %s" % abi)
        if not packaged_abis:
            problems.append("no native libraries packaged at all")
        for abi in abis:
            if abi not in packaged_abis:
                problems.append("requested ABI %s not in %s" % (abi, packaged_abis))

        for asset in EXPECTED_ASSETS:
            if asset not in nameset:
                problems.append("missing asset %s" % asset)

        # An unsigned APK cannot be installed; debug builds and the release
        # build (signed with the debug key) both carry v1/v2 signature files.
        signatures = [n for n in names
                      if n.startswith("META-INF/")
                      and (n.endswith(".RSA") or n.endswith(".DSA") or n.endswith(".EC")
                           or n.endswith(".SF"))]
        if not signatures:
            problems.append("APK is not signed (no META-INF signature files)")

        icons = [n for n in names if n.startswith("assets/icons/")]
        if len(icons) < 100:
            problems.append("only %d icon files packaged" % len(icons))

        # The native asset reader opens these uncompressed; aapt must leave
        # them stored, which is what noCompress is for.
        for asset in EXPECTED_ASSETS:
            if asset in nameset:
                info = z.getinfo(asset)
                if info.compress_type != zipfile.ZIP_STORED:
                    problems.append("%s is compressed in the APK" % asset)

        so_sizes = {abi: z.getinfo("lib/%s/libpocketdoombuilder.so" % abi).file_size
                    for abi in packaged_abis
                    if "lib/%s/libpocketdoombuilder.so" % abi in nameset}

    import os
    print("APK: %s (%.1f MB)" % (apk, os.path.getsize(apk) / (1024.0 * 1024.0)))
    print("  entries: %d, dex/manifest/arsc present: %s"
          % (len(names), "yes" if core_ok else "no"))
    print("  ABIs: %s" % ", ".join(packaged_abis))
    for abi, size in sorted(so_sizes.items()):
        print("  lib/%s/libpocketdoombuilder.so: %.1f MB" % (abi, size / (1024.0 * 1024.0)))
    print("  signed: %s" % ("yes" if signatures else "no"))
    print("  icon assets: %d files, fonts: %s"
          % (len(icons), "yes" if all(a in nameset for a in EXPECTED_ASSETS[1:]) else "no"))

    if problems:
        for p in problems:
            print("PROBLEM: %s" % p)
        print("APK check FAILED")
        return 1
    print("APK check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
