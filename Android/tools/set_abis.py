#!/usr/bin/env python3
"""Rewrite the abiFilters line in Android/app/build.gradle.

The CI workflow takes the ABIs as a manual input; rather than templating the
whole build file, this rewrites the one line that matters.

Usage: python3 Android/tools/set_abis.py "arm64-v8a,x86_64"
"""

import pathlib
import re
import sys

GRADLE = pathlib.Path(__file__).resolve().parent.parent / "app" / "build.gradle"

# ABIs the Android toolchain knows about, in the order they should appear.
KNOWN_ABIS = ("arm64-v8a", "armeabi-v7a", "x86", "x86_64")


def main(argv):
    if len(argv) != 2:
        print("usage: set_abis.py <abi[,abi...]>", file=sys.stderr)
        return 2

    requested = [a.strip() for a in argv[1].split(",") if a.strip()]
    unknown = [a for a in requested if a not in KNOWN_ABIS]
    if unknown:
        print("unknown ABI(s): %s" % ", ".join(unknown), file=sys.stderr)
        return 2
    if not requested:
        print("no ABIs given", file=sys.stderr)
        return 2

    abis = [a for a in KNOWN_ABIS if a in requested]
    quoted = ", ".join("'%s'" % a for a in abis)

    text = GRADLE.read_text()
    # Keep the line inside the ndk { } block of defaultConfig and keep the
    # original indentation so the diff stays minimal.
    pattern = r"(abiFilters\s+)[^\n]+"
    match = re.search(pattern, text)
    new_text, count = re.subn(pattern, lambda m: m.group(1) + quoted, text)
    if count != 1 or match is None:
        print("expected exactly one abiFilters line, found %d" % count, file=sys.stderr)
        return 1

    GRADLE.write_text(new_text)
    print("abiFilters -> %s" % quoted)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
