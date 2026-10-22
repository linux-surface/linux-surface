#!/usr/bin/env bash
#
# Print the update requests of the package status issue, i.e. its checked
# update checkboxes: 'update:<script>' for requested updates, 'auto:<script>'
# for packages to update automatically.
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

ISSUE_FILE="$(dirname "${BASH_SOURCE[0]}")/../../data/autoupdate/status-issue"

if [ -z "${GITHUB_REPOSITORY:-}" ]; then
    echo "ERROR: GITHUB_REPOSITORY environment variable is required" >&2
    exit 1
fi

{
    # See update-status-issue.sh
    if [ -f "${ISSUE_FILE}" ]; then
        gh issue view "$(tr -d '[:space:]' < "${ISSUE_FILE}")" -R "${GITHUB_REPOSITORY}" \
            --json body --jq .body
    fi

    for file in "$@"; do
        cat "${file}"
    done
} | tr -d '\r' \
    | sed -nE 's/^\s*- \[[xX]\] .*<!-- ((update|auto):[a-z0-9-]+) -->\s*$/\1/p' \
    | sort -u
