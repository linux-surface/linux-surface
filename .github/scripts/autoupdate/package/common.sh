#!/usr/bin/env bash
#
# Shared helpers for the package update scripts.
#
# The package scripts print their results as key=value lines on stdout (so
# they can be appended to $GITHUB_OUTPUT directly) and log to stderr.
#
# Exit codes of the 'check' command:
#   0: All upstream sources for the requested version are available
#   1: Error
#   2: Upstream sources are not available yet

set -euo pipefail

if [ "${DEBUG:-}" = "1" ]; then
    set -x
fi

COMMON_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${COMMON_DIR}/../../../.." && pwd)"
MAINTAINED_BRANCHES="${REPO_ROOT}/.github/data/autoupdate/maintained-branches.conf"

# shellcheck disable=SC2034 # used by the package scripts
EXIT_PENDING=2

log()
{
    echo "$@" >&2
}

die()
{
    log "ERROR: $*"
    exit 1
}

emit()
{
    echo "$1=$2"
}

# Filter tag names by an extended regular expression (anchored on both ends).
filter_tags()
{
    { grep -E "^$1\$" || true; } | sort -V
}

# List the tag names of a remote git repository starting with the given prefix
# and matching the given extended regular expression.
#
# Usage: remote_tags <url> <prefix> <regex>
remote_tags()
{
    local url="$1"
    local prefix="$2"
    local regex="$3"
    local refs
    local attempt

    # Some servers (Launchpad) occasionally fail to list refs, so retry
    for attempt in 1 2 3; do
        if refs="$(timeout 120 git ls-remote --tags --refs "${url}" "refs/tags/${prefix}*")"; then
            break
        fi

        if [ "${attempt}" -eq 3 ]; then
            die "Failed to list tags of ${url}"
        fi

        log "Failed to list tags of ${url}, retrying..."
        sleep 10
    done

    echo "${refs}" | awk '{ print $2 }' | sed 's|^refs/tags/||' | filter_tags "${regex}"
}

# Same as remote_tags, but for repositories on GitHub. Listing the refs of
# large GitHub repositories via git can take minutes, so use the API instead.
# Uses GH_TOKEN or GITHUB_TOKEN if set, to avoid rate limits.
#
# Usage: github_tags <owner/repo> <prefix> <regex>
github_tags()
{
    local repo="$1"
    local prefix="$2"
    local regex="$3"
    local token="${GH_TOKEN:-${GITHUB_TOKEN:-}}"
    local auth=()
    local refs

    if [ -n "${token}" ]; then
        auth=(-H "Authorization: Bearer ${token}")
    fi

    if ! refs="$(curl -sf --retry 3 "${auth[@]}" \
            "https://api.github.com/repos/${repo}/git/matching-refs/tags/${prefix}")"; then
        die "Failed to list tags of github.com/${repo}"
    fi

    echo "${refs}" | jq -r '.[].ref' | sed 's|^refs/tags/||' | filter_tags "${regex}"
}

# Check whether a URL can be fetched (following redirects).
url_exists()
{
    curl -sfIL -o /dev/null --retry 3 "$1"
}

# Escape the dots in a version for use in a regular expression.
regex_escape()
{
    echo "${1//./\\.}"
}

# Check whether version $1 is lower than or equal to version $2.
version_le()
{
    [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -n1)" = "$1" ]
}

# Get the series (major.minor) of a kernel version (e.g. 7.2.12 -> 7.2).
version_series()
{
    echo "$1" | cut -d. -f1-2
}

# Read the base version of the patches for a series (e.g. 7.2 -> 7.2.12).
patches_base()
{
    local file="${REPO_ROOT}/patches/$1/BASE"

    if [ ! -f "${file}" ]; then
        return
    fi

    sed 's/^v//' "${file}"
}

# List the maintained series (see maintained-branches.conf).
maintained_series()
{
    grep -vE '^\s*(#|$)' "${MAINTAINED_BRANCHES}" | cut -d: -f2 | sort -uV
}

# Get the series packages should be on: the newest maintained series with
# patches for a stable release (X.Y.Z). Patches for the initial release of a
# series (X.Y) are not packaged.
target_series()
{
    local series

    for series in $(maintained_series | sort -rV); do
        if patches_base "${series}" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
            echo "${series}"
            return
        fi
    done
}

# Make sure the patches of a series are for the given version.
require_patches_base()
{
    local version="$1"
    local series
    local base

    series="$(version_series "${version}")"
    base="$(patches_base "${series}")"

    if [ "${base}" != "${version}" ]; then
        die "patches/${series} are for ${base:-an unknown version}, not ${version}"
    fi
}

# Determine the package release when updating from <current>-<release> to
# <version>: bump the release for the same version, reset it otherwise.
#
# Usage: next_release <current> <release> <version>
next_release()
{
    local current="$1"
    local release="$2"
    local version="$3"

    if [ "${current}" = "${version}" ]; then
        echo $((release + 1))
    elif version_le "${version}" "${current}"; then
        die "Refusing to downgrade from ${current} to ${version}"
    else
        echo 1
    fi
}

# Validate the version argument of a command and print it without a 'v' prefix.
parse_version()
{
    local version="${1:-}"

    version="${version#v}"

    if ! echo "${version}" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
        die "Invalid kernel version '${1:-}', expected X.Y.Z"
    fi

    echo "${version}"
}
