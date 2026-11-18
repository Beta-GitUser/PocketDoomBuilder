#!/usr/bin/env python3
"""Sanity-check a built PocketDoomBuilder APK.

The APK is a zip, so its contents can be inspected without the Android SDK.
This guards against the classic "the build succeeded but the APK is useless"
failures: the native library missing for the requested ABI, the icon manifest
or fonts not packaged, or the assets being compressed (the native side reads
them straight through AAssetManager, which needs noCompress).

Usage: python3 Android/tools/verify_apk.py <apk> [abi[,abi...]]
"""

import hashlib
import struct
import sys
import zipfile

EXPECTED_ASSETS = ("assets/icons.txt", "assets/Arimo-Regular.ttf", "assets/Arimo-Bold.ttf")

# Magic string of the APK Signing Block (v2/v3 signature scheme).
SIGNING_BLOCK_MAGIC = b"APK Sig Block 42"


def has_signing_block(apk):
    """True if the APK carries an APK Signing Block (v2/v3 signatures)."""
    with open(apk, "rb") as f:
        f.seek(0, 2)
        size = f.tell()
        window = min(size, 1 << 20)
        f.seek(size - window)
        return SIGNING_BLOCK_MAGIC in f.read(window)


# IDs of the signature schemes inside the signing block.
ID_V2_SIGNATURE = 0x7109871A
ID_V3_SIGNATURE = 0xF05368C0


class _Reader(object):
    """Cursor over a byte string with little-endian length prefixes."""

    def __init__(self, data):
        self.data = data
        self.pos = 0

    def u32(self):
        value = struct.unpack_from("<I", self.data, self.pos)[0]
        self.pos += 4
        return value

    def u64(self):
        value = struct.unpack_from("<Q", self.data, self.pos)[0]
        self.pos += 8
        return value

    def take(self, length):
        blob = self.data[self.pos:self.pos + length]
        if len(blob) != length:
            raise ValueError("truncated")
        self.pos += length
        return blob

    def length_prefixed(self):
        return self.take(self.u32())


def _find_signing_block(apk):
    """Return (start, end) of the APK Signing Block, or None."""
    with open(apk, "rb") as f:
        f.seek(0, 2)
        size = f.tell()
        f.seek(max(0, size - (1 << 20)))
        window = f.read()
    offset = size - len(window)
    magic_pos = window.rfind(SIGNING_BLOCK_MAGIC)
    if magic_pos < 0:
        return None
    magic_pos += offset  # absolute position of the magic
    if magic_pos < 8:
        return None
    with open(apk, "rb") as f:
        f.seek(magic_pos - 8)
        block_size = struct.unpack("<Q", f.read(8))[0]
    # Layout: [size][pairs][size][magic]. The size field counts the pairs, the
    # trailing size field and the magic, so the block starts at the magic
    # position minus that count, plus the 8-byte size field itself.
    start = magic_pos - block_size + 8
    if start < 0:
        return None
    return start, magic_pos


def _signer_certificates(value):
    """Extract the signer certificates from a v2/v3 signature value."""
    # value is a length-prefixed sequence of signer records; each record is
    # length-prefixed and holds: signed data, signatures, public key. Each of
    # those three fields is itself length-prefixed, and the record's own length
    # prefix covers all three - so skip it before reading the fields.
    reader = _Reader(value)
    record = reader.take(reader.u32())
    inner = _Reader(record)
    inner.u32()  # the record's total length, already covered by the prefix
    signed_data = inner.take(inner.u32())
    # signed data: digests (length-prefixed), certificates (length-prefixed
    # sequence of length-prefixed DER blobs).
    signed = _Reader(signed_data)
    digests = signed.take(signed.u32())
    certs_blob = signed.take(signed.u32())
    certs = _Reader(certs_blob)
    # The sequence is just repeated length-prefixed certificates (no count).
    certificates = []
    while certs.pos < len(certs_blob):
        certificates.append(certs.length_prefixed())
    return certificates


def certificate_fingerprints(apk):
    """Fingerprints of the APK's signing certificates, or None if unparsable.

    Returns a list of (sha256, sha1) hex strings, one per signer certificate,
    in the order they appear in the APK Signing Block.
    """
    block = _find_signing_block(apk)
    if block is None:
        return None
    start, end = block
    with open(apk, "rb") as f:
        f.seek(start)
        data = f.read(end - start)
    # Skip the block's leading 8-byte size field; the pairs start right after it.
    reader = _Reader(data)
    if len(data) >= 8:
        reader.pos = 8
    found = []
    # Each ID-value pair is an 8-byte length (covering the ID and value) followed
    # by a 4-byte ID and the value itself.
    while reader.pos + 12 <= len(data):
        pair_len = reader.u64()
        pair_id = reader.u32()
        if pair_id not in (ID_V2_SIGNATURE, ID_V3_SIGNATURE):
            reader.pos += pair_len - 8
            continue
        value = reader.take(pair_len - 4)
        try:
            for cert in _signer_certificates(value):
                found.append((hashlib.sha256(cert).hexdigest(),
                              hashlib.sha1(cert).hexdigest()))
        except (ValueError, struct.error):
            continue
    return found or None


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

        # An unsigned APK cannot be installed. With minSdk 24 AGP signs using
        # APK Signature Scheme v2/v3 only (no v1 META-INF files), so look for
        # the signing block that sits just before the central directory, and
        # fall back to v1 signature files.
        v1 = [n for n in names
              if n.startswith("META-INF/")
              and (n.endswith(".RSA") or n.endswith(".DSA") or n.endswith(".EC"))]
        v2 = has_signing_block(apk)
        signed = bool(v1) or v2
        if not signed:
            problems.append("APK is not signed (no v1 signature files and no APK Signing Block)")

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
    scheme = "v2/v3" if v2 else ("v1" if v1 else "no")
    print("  signed: %s" % scheme)
    # Which key signed it. Compare this against `keytool -list -v -keystore
    # <your.jks> -alias <alias>` to confirm the APK really came from your key.
    if scheme == "v2/v3":
        certs = certificate_fingerprints(apk)
        if certs:
            for sha256, sha1 in certs:
                print("  certificate sha256: %s" % sha256)
                print("  certificate sha1:   %s" % sha1)
        else:
            # A signing block without a readable certificate means the APK is
            # signed but we cannot say by whom - fail rather than silently
            # skipping the check that the key is the one you configured.
            problems.append("signing block present but no certificate could be read")
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
