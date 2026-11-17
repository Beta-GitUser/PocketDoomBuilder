#!/bin/sh
# Publish the tail of a failed CI step where it can be read without the
# Actions log UI (which is served from a host that some automated clients
# cannot reach).
#
# The log is written into a single rolling issue whose title is fixed, so
# repeated failures update one place instead of piling up new issues.
#
# Usage: sh Android/tools/report_ci_log.sh <logfile> <step name>
#
# Requires GH_TOKEN and a repo checkout (gh picks the repository from it).

set -eu

LOG=${1:-/tmp/pdb-ci.log}
STEP=${2:-build}
TITLE="Android CI failure log"
LIMIT=${PDB_CI_LOG_LINES:-400}
MAX_BYTES=${PDB_CI_LOG_BYTES:-55000}

if [ ! -f "$LOG" ]; then
    echo "no log file at $LOG" >&2
    exit 0
fi

BODY=$(mktemp)
{
    echo "Automatic report for a failed step; safe to delete."
    echo
    echo "* step: \`$STEP\`"
    echo "* commit: \`${GITHUB_SHA:-unknown}\`"
    echo "* run: ${GITHUB_SERVER_URL:-https://github.com}/${GITHUB_REPOSITORY:-}/actions/runs/${GITHUB_RUN_ID:-0}"
    echo
    echo '```'
    # Tail, then hard-truncate from the front so the failing lines survive.
    tail -n "$LIMIT" "$LOG" | tail -c "$MAX_BYTES"
    echo
    echo '```'
} > "$BODY"

EXISTING=$(gh issue list --state open --search "\"$TITLE\" in:title" --json number,title \
    --jq "[.[] | select(.title == \"$TITLE\")][0].number // empty")

if [ -n "${EXISTING:-}" ]; then
    gh issue edit "$EXISTING" --body-file "$BODY" > /dev/null
    echo "updated issue #$EXISTING"
else
    gh issue create --title "$TITLE" --body-file "$BODY" --label ci-log > /dev/null 2>&1 \
        || gh issue create --title "$TITLE" --body-file "$BODY" > /dev/null
    echo "created issue"
fi
rm -f "$BODY"
