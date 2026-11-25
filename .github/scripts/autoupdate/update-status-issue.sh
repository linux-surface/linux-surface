#!/usr/bin/env bash
#
# Create or update the package status issue.
#
# The issue number is recorded in .github/data/autoupdate/status-issue. Looking
# the issue up by its label is not reliable, the API occasionally returns no
# issues. If the file does not exist, a new issue is created, locked and pinned,
# and its number needs to be recorded in the file.
#
# Usage: update-status-issue.sh <report.md>
#
# Environment variables:
#   GITHUB_REPOSITORY: Repository to create / update the issue in
#   GH_TOKEN:          GitHub token with issues: write permission

set -euo pipefail

if [ "${DEBUG:-}" = "1" ]; then
    set -x
fi

REPORT="${1:-}"
LABEL="package-status"
TITLE="Package status"

if [ -z "${REPORT}" ] || [ ! -f "${REPORT}" ]; then
    echo "Usage: $0 <report.md>"
    exit 1
fi

if [ -z "${GITHUB_REPOSITORY:-}" ]; then
    echo "ERROR: GITHUB_REPOSITORY environment variable is required"
    exit 1
fi

ISSUE_FILE="$(dirname "${BASH_SOURCE[0]}")/../../data/autoupdate/status-issue"
ISSUE=""

if [ -f "${ISSUE_FILE}" ]; then
    ISSUE="$(tr -d '[:space:]' < "${ISSUE_FILE}")"
fi

if [ -n "${ISSUE}" ]; then
    echo "Updating issue #${ISSUE}"
    gh issue edit "${ISSUE}" -R "${GITHUB_REPOSITORY}" --body-file "${REPORT}"
else
    echo "Creating new status issue"

    if ! gh api "repos/${GITHUB_REPOSITORY}/labels/${LABEL}" > /dev/null 2>&1; then
        gh label create "${LABEL}" -R "${GITHUB_REPOSITORY}" \
            --color "0e8a16" --description "Automatically updated package status"
    fi

    URL="$(gh issue create -R "${GITHUB_REPOSITORY}" --title "${TITLE}" \
        --label "${LABEL}" --body-file "${REPORT}")"
    echo "Created ${URL}"

    # Only allow collaborators to comment
    gh issue lock "${URL}" -R "${GITHUB_REPOSITORY}"

    # Pinning may not be permitted for the token, don't fail on that
    gh issue pin "${URL}" -R "${GITHUB_REPOSITORY}" || echo "WARNING: Could not pin ${URL}"

    echo "::warning::Created status issue ${URL}, record its number in .github/data/autoupdate/status-issue"
fi
