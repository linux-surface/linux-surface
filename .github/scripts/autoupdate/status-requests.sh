#!/usr/bin/env bash
#
# Print the packages (scripts) for which an update has been requested in the
# package status issue, i.e. whose update checkbox is checked.
#
# Usage: status-requests.sh [<body-file>...]
#
# Additional issue bodies can be passed as files, e.g. the body of an 'edited'
# event. This makes sure that a request is not lost if a concurrent run has
# overwritten the issue in the meantime.
#
# Environment variables:
#   GITHUB_REPOSITORY: Repository of the status issue
#   GH_TOKEN:          GitHub token with issues: read permission

set -euo pipefail

if [ "${DEBUG:-}" = "1" ]; then
    set -x
fi

LABEL="package-status"

if [ -z "${GITHUB_REPOSITORY:-}" ]; then
    echo "ERROR: GITHUB_REPOSITORY environment variable is required" >&2
    exit 1
fi

{
    gh issue list -R "${GITHUB_REPOSITORY}" --label "${LABEL}" --state open \
        --json number,body --jq 'sort_by(.number) | .[0].body // empty'

    for file in "$@"; do
        cat "${file}"
    done
} | tr -d '\r' \
    | sed -nE 's/^\s*- \[[xX]\] .*<!-- update:([a-z0-9-]+) -->\s*$/\1/p' \
    | sort -u
