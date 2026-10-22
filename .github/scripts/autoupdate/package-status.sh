#!/usr/bin/env bash
#
# Generate the package status report (Markdown) on stdout.
#
# The report contains:
# 1. For each package target: packaged version, release state, version of our
#    patches, latest distro upstream version, latest kernel.org stable version,
#    and what's blocking an update.
# 2. For each maintained kernel branch: latest kernel.org stable version vs.
#    our branch and patches.
# 3. Recently failed autoupdate runs and notices (newer series, EOL).
#
# Environment variables:
#   GITHUB_REPOSITORY: (optional) Repository to check tags and runs of
#   GH_TOKEN:          (optional) GitHub token, to avoid API rate limits

# shellcheck source-path=SCRIPTDIR source=package/common.sh
source "$(dirname "${BASH_SOURCE[0]}")/package/common.sh"

PACKAGE_SCRIPTS="${COMMON_DIR}"
REPOSITORY="${GITHUB_REPOSITORY:-linux-surface/linux-surface}"
KERNEL_REPOSITORY="linux-surface/kernel"
MAINTAINED_BRANCHES="${REPO_ROOT}/.github/data/autoupdate/maintained-branches.conf"

TODAY="$(date -u +%Y-%m-%d)"

# ============================================================================
# Helpers
# ============================================================================

declare -A STABLE=()
declare -A RELEASE_DATES=()

# Load the latest kernel.org release of each maintained series.
load_kernel_org()
{
    local json

    if ! json="$(curl -sf --retry 3 https://www.kernel.org/releases.json)"; then
        die "Failed to fetch kernel.org releases"
    fi

    while read -r version; do
        STABLE["$(version_series "${version}")"]="${version}"
    done < <(echo "${json}" | jq -r '.releases[] | select(.moniker == "stable" or .moniker == "longterm") | select(.iseol | not) | .version')

    NEWEST_STABLE="$(echo "${json}" | jq -r '.releases[] | select(.moniker == "stable") | .version' | sort -V | tail -n1)"
}

# Print the release date (YYYY-MM-DD) of a kernel version, based on the
# kernel.org file listing.
release_date()
{
    local version="$1"
    local major="${version%%.*}"
    local date

    if [ -z "${RELEASE_DATES[${major}]:-}" ]; then
        RELEASE_DATES[${major}]="$(curl -sf --retry 3 "https://cdn.kernel.org/pub/linux/kernel/v${major}.x/" || true)"
    fi

    date="$(echo "${RELEASE_DATES[${major}]}" \
        | sed -nE "s|.*>linux-$(regex_escape "${version}")\.tar\.xz</a> +([0-9]+-[A-Za-z]+-[0-9]+) .*|\\1|p")"

    if [ -n "${date}" ]; then
        date -u -d "${date}" +%Y-%m-%d
    fi
}

days_since()
{
    local days=$(( ($(date -u -d "${TODAY}" +%s) - $(date -u -d "$1" +%s)) / 86400 ))

    if [ "${days}" -eq 1 ]; then
        echo "1 day"
    else
        echo "${days} days"
    fi
}

# Next patch version (e.g. 7.2.11 -> 7.2.12).
next_version()
{
    echo "$1" | awk -F. '{ printf "%d.%d.%d", $1, $2, $3 + 1 }'
}

version_lt()
{
    [ "$1" != "$2" ] && version_le "$1" "$2"
}

# Run a package script command and load its key=value output into the
# associative array named by $1. Returns the exit code of the command.
load_kv()
{
    local -n _kv="$1"
    local out
    local rc=0

    shift
    out="$("$@")" || rc=$?

    _kv=()
    while IFS='=' read -r key value; do
        [ -n "${key}" ] && _kv["${key}"]="${value}"
    done <<< "${out}"

    return "${rc}"
}

# "Behind since" column: date of the first stable release newer than the
# packaged version.
behind_since()
{
    local version="$1"
    local stable="$2"
    local next
    local date

    if [ -z "${stable}" ] || ! version_lt "${version}" "${stable}"; then
        echo "–"
        return
    fi

    next="$(next_version "${version}")"
    date="$(release_date "${next}")"

    if [ -z "${date}" ]; then
        echo "${next}"
    else
        echo "${next}, ${date} ($(days_since "${date}"))"
    fi
}

# ============================================================================
# Packages
# ============================================================================

# Print a table row for one package target.
#
# Usage: package_row <name> <script> <tag-prefix>
package_row()
{
    local name="$1"
    local script="$2"
    local prefix="$3"
    local -A cur=()
    local -A latest=()
    local -A check=()
    local series
    local base
    local stable
    local tag
    local released
    local status
    local upstream

    load_kv cur "${PACKAGE_SCRIPTS}/${script}.sh" current
    load_kv latest "${PACKAGE_SCRIPTS}/${script}.sh" latest

    series="${cur[series]}"
    base="$(patches_base "${series}")"
    stable="${STABLE[${series}]:-}"

    tag="${prefix}-${cur[version]}-${cur[release]}"

    if [ -n "$(github_tags "${REPOSITORY}" "${tag}" "$(regex_escape "${tag}")")" ]; then
        released="✅ \`${tag}\`"
    else
        released="❌ \`${tag}\`"
    fi

    upstream="${latest[upstream]:-?}"
    if [ -n "${latest[packaging]:-}" ]; then
        upstream="${upstream} (packaging ${latest[packaging]})"
    fi

    if [ -n "${base}" ] && version_lt "${cur[version]}" "${base}"; then
        if load_kv check "${PACKAGE_SCRIPTS}/${script}.sh" check "${base}"; then
            status="🟢 Ready to update to ${base}"
        else
            status="⏳ Waiting for upstream: ${check[reason]:-check failed}"
        fi
    elif [[ "${released}" == ❌* ]]; then
        status="🏷️ Release tag missing"
    elif [ -n "${stable}" ] && version_lt "${cur[version]}" "${stable}"; then
        status="⏳ Waiting for patches (${stable})"
    else
        status="✅ Up to date"
    fi

    echo "| ${name} | ${cur[package_version]} | ${released} | ${base:-–} | ${upstream} | ${stable:-EOL} | $(behind_since "${cur[version]}" "${stable}") | ${status} |"
}

print_packages()
{
    local fedora

    echo "## Packages"
    echo ""
    echo "| Target | Packaged | Released | Patches | Distro upstream | kernel.org | Behind since | Status |"
    echo "|---|---|---|---|---|---|---|---|"

    package_row "Arch" arch arch
    package_row "Debian" debian debian

    for fedora in $(find "${REPO_ROOT}/.github/workflows" -name 'fedora-*.yml' -printf '%f\n' | sed -E 's/fedora-([0-9]+)\.yml/\1/' | sort -n); do
        package_row "Fedora ${fedora}" fedora "fedora-${fedora}"
    done

    echo ""
}

# ============================================================================
# Kernel branches
# ============================================================================

print_branches()
{
    local branch
    local series
    local stable
    local tag
    local surface
    local base
    local status

    echo "## Kernel branches"
    echo ""
    echo "| Branch | kernel.org | Branch | Patches | Status |"
    echo "|---|---|---|---|---|"

    while IFS=: read -r branch series; do
        stable="${STABLE[${series}]:-}"
        base="$(patches_base "${series}")"

        tag="$(github_tags "${KERNEL_REPOSITORY}" "surface/v${series}." "surface/v$(regex_escape "${series}")\.[0-9]+" | tail -n1)"
        surface="${tag#surface/v}"

        if [ -z "${stable}" ]; then
            status="⚠️ Series is EOL"
        elif [ -z "${surface}" ] || version_lt "${surface}" "${stable}"; then
            status="⏳ Rebase pending"
        elif [ -z "${base}" ] || version_lt "${base}" "${surface}"; then
            status="⏳ Patch update pending"
        else
            status="✅ Up to date"
        fi

        echo "| [\`${branch}\`](https://github.com/${KERNEL_REPOSITORY}/tree/${branch}) | ${stable:-EOL} | ${surface:-?} | ${base:-–} | ${status} |"
    done < <(grep -vE '^\s*(#|$)' "${MAINTAINED_BRANCHES}")

    echo ""
}

# ============================================================================
# Failed runs and notices
# ============================================================================

print_failures()
{
    local since
    local runs

    since="$(date -u -d "${TODAY} - 7 days" +%Y-%m-%d)"

    if ! runs="$(gh run list -R "${REPOSITORY}" --created ">=${since}" -L 200 \
            --json workflowName,displayTitle,url,createdAt,conclusion 2>/dev/null)"; then
        log "WARNING: Failed to list workflow runs"
        return
    fi

    # Only report failures that have not been superseded by a later run. Runs
    # of the autoupdate workflows are told apart by their title (i.e. branch),
    # package builds only by their workflow.
    runs="$(echo "${runs}" | jq -r '[.[]
        | select(.workflowName | test("^(Sync Kernel with Upstream|Rebase and Build Kernel|Generate Kernel Patches|Arch|Debian|Fedora [0-9]+)$"))
        | . + { key: (if (.workflowName | test("^(Rebase|Generate)")) then .workflowName + .displayTitle else .workflowName end) }]
        | group_by(.key) | map(max_by(.createdAt)) | sort_by(.createdAt) | reverse | .[]
        | select(.conclusion == "failure")
        | "- \(.createdAt[:10]): [\(.workflowName): \(.displayTitle)](\(.url))"')"

    if [ -z "${runs}" ]; then
        return
    fi

    echo "## Failed runs (last 7 days)"
    echo ""
    echo "${runs}"
    echo ""
}

print_notices()
{
    local series
    local notices=()

    for series in $(for s in arch debian fedora; do "${PACKAGE_SCRIPTS}/${s}.sh" current | sed -n 's/^series=//p'; done | sort -uV); do
        if [ -z "${STABLE[${series}]:-}" ]; then
            notices+=("- ⚠️ Packaged series ${series} is end of life on kernel.org.")
        fi

        if version_lt "${series}" "$(version_series "${NEWEST_STABLE}")"; then
            notices+=("- ℹ️ Newer stable series available: ${NEWEST_STABLE} (packages on ${series}).")
        fi
    done

    if [ "${#notices[@]}" -eq 0 ]; then
        return
    fi

    echo "## Notices"
    echo ""
    printf '%s\n' "${notices[@]}"
    echo ""
}

# ============================================================================
# Report
# ============================================================================

load_kernel_org

echo "<!-- package-status -->"
echo "# Package status"
echo ""

print_packages
print_branches
print_notices
print_failures

if [ -n "${GITHUB_RUN_ID:-}" ]; then
    echo "_Updated ${TODAY} by [workflow run](${GITHUB_SERVER_URL:-https://github.com}/${REPOSITORY}/actions/runs/${GITHUB_RUN_ID})._"
else
    echo "_Updated ${TODAY}._"
fi
