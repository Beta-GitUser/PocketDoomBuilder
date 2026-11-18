#!/bin/sh
# Launch the built APK on an emulator/device and prove it stays alive.
#
# This is the only check that exercises the real thing: EGL context creation,
# the ImGui GLES3 backend, the asset manager (icons.txt, the icon PNGs, both
# Arimo fonts) and the first frame. A green Gradle build says none of that.
#
# Usage: sh Android/tools/smoke_test.sh <apk> [screenshot.png] [wait-seconds]
#
# Requires adb on PATH and exactly one connected device/emulator.
# Exits non-zero when the activity is not running after the wait, or when the
# process logged a fatal exception / native crash.

set -eu

APK=${1:?usage: smoke_test.sh <apk> [screenshot.png] [wait-seconds]}
SHOT=${2:-smoke-test.png}
WAIT=${3:-20}

PACKAGE=com.pocketdoombuilder
ACTIVITY=$PACKAGE/.MainActivity

log() { printf '%s\n' "$*"; }

# Always leave a logcat (and screenshot, when possible) behind, even if the
# device never becomes usable, so the artifacts explain the failure.
dump_logs() {
    if [ "${LOGS_DUMPED:-0}" = "0" ]; then
        LOGS_DUMPED=1
        adb logcat -d > smoke-test-logcat.txt 2>/dev/null || true
        if [ ! -s "$SHOT" ]; then
            adb exec-out screencap -p > "$SHOT" 2>/dev/null || true
        fi
    fi
}
trap 'dump_logs' EXIT

log "== waiting for the device"
if ! command -v adb > /dev/null 2>&1; then
    log "FAIL: adb is not on PATH"
    exit 1
fi
if ! adb wait-for-device; then
    log "FAIL: no device/emulator answered adb"
    exit 1
fi
# The boot is not complete when the device answers adb; the package manager
# has to be up before 'install' works reliably.
i=0
while [ "$(adb shell getprop sys.boot_completed 2>/dev/null | tr -d '\r')" != "1" ]; do
    i=$((i + 1))
    if [ "$i" -gt 120 ]; then
        log "device did not finish booting"
        exit 1
    fi
    sleep 2
done
adb shell input keyevent 82 > /dev/null 2>&1 || true   # dismiss the lock screen

# Fresh logs, so any crash below is this run's.
adb logcat -c || true

log "== installing $APK"
adb install -r -t "$APK"
PACKAGES=$(adb shell pm list packages "$PACKAGE" | tr -d '\r')
case "$PACKAGES" in
    *"$PACKAGE"*) ;;
    *) log "package $PACKAGE is not installed"; exit 1 ;;
esac

log "== launching $ACTIVITY"
adb shell am start -W -n "$ACTIVITY" | sed 's/^/   /'
sleep "$WAIT"

FAILED=0

# 1. The process must still be alive and owned by the launcher activity.
PID=$(adb shell pidof "$PACKAGE" 2>/dev/null | tr -d '\r')
if [ -z "$PID" ]; then
    log "FAIL: $PACKAGE is not running after ${WAIT}s"
    FAILED=1
else
    log "process alive, pid $PID"
fi

# API 29+ reports topResumedActivity, older releases mResumedActivity.
RESUMED=$(adb shell dumpsys activity activities 2>/dev/null \
    | grep -cE "(top)?[mM]?[rR]esumedActivity.*$PACKAGE" || true)
if [ "${RESUMED:-0}" -lt 1 ]; then
    log "FAIL: no resumed $PACKAGE activity"
    FAILED=1
else
    log "activity is resumed"
fi

# 2. No fatal exception, no native crash. Save the logs for the artifact first.
dump_logs
if grep -qE "FATAL EXCEPTION|beginning of crash|Fatal signal|native crash" smoke-test-logcat.txt; then
    log "FAIL: crash in logcat"
    grep -nE -A5 "FATAL EXCEPTION|beginning of crash|Fatal signal" smoke-test-logcat.txt | head -40
    FAILED=1
else
    log "no crash markers in logcat"
fi

# The shell logs through __android_log_print with the PocketDoomBuilder tag.
if grep -q "PocketDoomBuilder" smoke-test-logcat.txt; then
    log "native log lines:"
    grep "PocketDoomBuilder" smoke-test-logcat.txt | tail -12 | sed 's/^/   /'
    if grep -q "ImGui ready" smoke-test-logcat.txt; then
        log "ImGui initialised on the device"
    else
        log "FAIL: the native side never reported 'ImGui ready'"
        FAILED=1
    fi
else
    log "FAIL: no native log output at all (library not loaded?)"
    FAILED=1
fi

# 3. A screenshot, so the result can be looked at as well as grepped.
if adb exec-out screencap -p > "$SHOT" 2>/dev/null; then
    log "screenshot: $SHOT ($(wc -c < "$SHOT") bytes)"
else
    log "warning: could not take a screenshot"
fi

if [ "$FAILED" -ne 0 ]; then
    log "smoke test FAILED"
    exit 1
fi
log "smoke test passed"
