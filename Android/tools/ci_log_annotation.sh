#!/bin/sh
# Echo the tail of a log file as a GitHub Actions error annotation.
#
# Annotations are readable through the API and show up inline on the run page,
# which makes a failed step diagnosable without scrolling the whole raw log.
# The workflow-command format needs "%" escaped and newlines written as %0A.
#
# Usage: sh Android/tools/ci_log_annotation.sh <logfile> <title>

set -eu

LOG=${1:-}
TITLE=${2:-error}
LINES=${PDB_CI_LOG_LINES:-60}

if [ -z "$LOG" ] || [ ! -f "$LOG" ]; then
    exit 0
fi

ESCAPED=$(tail -n "$LINES" "$LOG" | tr -d '\r' | sed 's/%/%25/g' | awk 'BEGIN { ORS = "%0A" } { print }')
printf '::error title=%s::%s\n' "$TITLE" "$ESCAPED"
exit 0
