#!/usr/bin/env bash
#
# Print the update requests of the package status issue: 'auto:<script>' for
# each package to update automatically (i.e. whose checkbox is checked).
#
# Usage: status-requests.sh [<body> [<previous-body>]]
#
# The body of an 'edited' event can be passed as file. This makes sure that a
# request is not lost if a concurrent run has overwritten the issue in the
# meantime. If the previous body (before the edit) is passed as well, packages
# whose box has been checked by the edit are also printed as 'retry:<script>'.
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

# Print the checked packages of an issue body (on stdin).
checked()
{
    tr -d '\r' | sed -nE 's/^\s*- \[[xX]\] .*<!-- auto:([a-z0-9-]+) -->\s*$/\1/p' | sort -u
}

BODY="${1:-/dev/null}"
PREVIOUS="${2:-}"

{
    # See update-status-issue.sh
    if [ -f "${ISSUE_FILE}" ]; then
        gh issue view "$(tr -d '[:space:]' < "${ISSUE_FILE}")" -R "${GITHUB_REPOSITORY}" \
            --json body --jq .body
    fi

    cat "${BODY}"
} | checked | sed 's/^/auto:/'

if [ -n "${PREVIOUS}" ]; then
    comm -23 <(checked < "${BODY}") <(checked < "${PREVIOUS}") | sed 's/^/retry:/'
fi
