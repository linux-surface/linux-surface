#!/usr/bin/env bash
#
# Create or update the package status issue.
#
# The issue is identified by its label. If no open issue with that label
# exists, a new one is created and pinned.
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

gh label create "${LABEL}" -R "${GITHUB_REPOSITORY}" --force \
    --color "0e8a16" --description "Automatically updated package status"

ISSUE="$(gh issue list -R "${GITHUB_REPOSITORY}" --label "${LABEL}" --state open \
    --json number --jq 'sort_by(.number) | .[0].number // empty')"

if [ -n "${ISSUE}" ]; then
    echo "Updating issue #${ISSUE}"
    gh issue edit "${ISSUE}" -R "${GITHUB_REPOSITORY}" --body-file "${REPORT}"
else
    echo "Creating new status issue"
    URL="$(gh issue create -R "${GITHUB_REPOSITORY}" --title "${TITLE}" \
        --label "${LABEL}" --body-file "${REPORT}")"
    echo "Created ${URL}"

    # Only allow collaborators to comment
    gh issue lock "${URL}" -R "${GITHUB_REPOSITORY}"

    # Pinning may not be permitted for the token, don't fail on that
    gh issue pin "${URL}" -R "${GITHUB_REPOSITORY}" || echo "WARNING: Could not pin ${URL}"
fi
