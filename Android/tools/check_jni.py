#!/usr/bin/env python3
"""Check that MainActivity's native declarations match the JNI implementations.

A wrong parameter list here does not fail the build: the app compiles, the
library loads, and then the first call throws UnsatisfiedLinkError on the
device. So compare both sides textually and also match the JNI type letters.

Usage: python3 Android/tools/check_jni.py            (prints a summary)
       exits 1 on a mismatch.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
JAVA = ROOT / "Android/app/src/main/java/com/pocketdoombuilder/MainActivity.java"
CPP = ROOT / "Android/app/src/main/cpp/src/platform_android.cpp"

# Java type -> JNI parameter type, longest first so "byte[]" wins over "byte".
JAVA_TO_JNI = [
    ("android.content.res.AssetManager", "jobject"),
    ("byte[]", "jbyteArray"),
    ("float[]", "jfloatArray"),
    ("int[]", "jintArray"),
    ("String", "jstring"),
    ("boolean", "jboolean"),
    ("float", "jfloat"),
    ("double", "jdouble"),
    ("long", "jlong"),
    ("int", "jint"),
    ("byte", "jbyte"),
    ("char", "jchar"),
    ("short", "jshort"),
]


def split_params(text):
    """Split a parameter list on commas that are not inside a generic."""
    parts, depth, current = [], 0, ""
    for ch in text:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(current.strip())
            current = ""
        else:
            current += ch
    if current.strip():
        parts.append(current.strip())
    return parts


def java_natives(text):
    """{name: [jni param types]} for the private native declarations."""
    pattern = re.compile(
        r"private\s+native\s+([\w\[\].]+)\s+(\w+)\s*\(([^;]*)\)\s*;", re.S)
    found = {}
    for ret, name, params in pattern.findall(text):
        jni = []
        for param in split_params(params):
            java_type = " ".join(param.split()[:-1]) if " " in param else param
            java_type = java_type.replace("final ", "").strip()
            for candidate, mapped in JAVA_TO_JNI:
                if java_type == candidate:
                    jni.append(mapped)
                    break
            else:
                raise SystemExit("%s: unknown parameter type %r" % (name, java_type))
        found[name] = (ret.strip(), jni)
    return found


def cpp_impls(text):
    """{name: [jni param types]} for the Java_..._nativeX definitions."""
    pattern = re.compile(
        r"Java_com_pocketdoombuilder_MainActivity_(\w+)\s*\(([^)]*)\)\s*\{", re.S)
    found = {}
    for name, params in pattern.findall(text):
        types = []
        for param in split_params(params):
            words = param.replace("*", " * ").split()
            types.append(words[0] if words else "?")
        # JNIEnv* and the receiver (jobject/jclass) are implicit on the Java side.
        found[name] = types[2:]
    return found


def main():
    java = java_natives(JAVA.read_text())
    cpp = cpp_impls(CPP.read_text())

    problems = []
    for name, (ret, jni) in java.items():
        if name not in cpp:
            problems.append("no implementation for native %s" % name)
            continue
        if cpp[name] != jni:
            problems.append("native %s: Java declares (%s), implementation takes (%s)"
                            % (name, ", ".join(jni), ", ".join(cpp[name])))

    for name in cpp:
        if name not in java:
            problems.append("implementation %s has no Java declaration" % name)

    # The saved-bytes entry point returns an array rather than void.
    if java.get("nativeSaveWad", ("", []))[0] != "byte[]":
        problems.append("nativeSaveWad must return byte[]")

    # Methods the native side calls back into must exist with the right shape.
    for name, signature in (
            ("launchOpenPicker", "()V"),
            ("launchSavePicker", "(Z)V"),
            ("appendNativeLog", "(Ljava/lang/String;)V"),
            # Help menu (ui_help.cpp / ui_toolbar.cpp request these).
            ("openUrl", "(Ljava/lang/String;)V"),
            ("showDiagnosticsPanel", "()V"),
            ("showConfigFolder", "()V"),
            ("exportManual", "(Ljava/lang/String;)V")):
        if not re.search(r"public\s+void\s+%s\s*\(" % name, JAVA.read_text()):
            problems.append("MainActivity is missing the public callback %s%s"
                            % (name, signature))

    if problems:
        for problem in problems:
            print("  FAIL: %s" % problem)
        return 1
    print("  %d native methods match their JNI implementations" % len(java))
    return 0


if __name__ == "__main__":
    sys.exit(main())
