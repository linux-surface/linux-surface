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
# 3. Update checkboxes for packages behind our patches (see below).
# 4. Recently failed autoupdate runs and notices (newer series, EOL).
#
# Automatic updates: Each package (script) has a checkbox to update it
# automatically whenever it is behind the base version of our patches, i.e.
# as soon as upstream is ready after a patch update has been merged. Checked
# boxes are read back from the issue (see status-requests.sh) and passed in via
# UPDATE_REQUESTS. Updates to start are written to UPDATE_DISPATCH (to be
# started by the workflow). If an update failed or its pull request was closed
# without merging, the box is unchecked, so that it is not retried until it is
# checked again.
#
# Environment variables:
#   GITHUB_REPOSITORY: (optional) Repository to check tags and runs of
#   GH_TOKEN:          (optional) GitHub token, to avoid API rate limits
#   UPDATE_REQUESTS:   (optional) Space-separated requests: 'auto:<script>'
#                      for packages to update automatically, 'retry:<script>'
#                      for packages whose box has just been checked
#   UPDATE_DISPATCH:   (optional) File to write the updates to start to, as
#                      '<script> <version>' lines

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

# Per package script: names of the targets, version to update to, and whether
# upstream is ready for it
declare -A PACKAGE_NAMES=()
declare -A UPDATE_VERSION=()
declare -A UPDATE_READY=()

UPDATE_STATE=""
UPDATE_AUTO=0

PULL_REQUESTS=""
CLOSED_PULL_REQUESTS=""
UPDATE_RUNS=""

# Load the latest kernel.org release of each maintained series. A new release
# (e.g. 7.3) is listed as mainline until its first stable release, so include
# mainline releases, but not release candidates.
load_kernel_org()
{
    local json
    local filter='.releases[] | select(.moniker == "stable" or .moniker == "longterm" or (.moniker == "mainline" and (.version | test("-rc") | not)))'

    if ! json="$(curl -sf --retry 3 https://www.kernel.org/releases.json)"; then
        die "Failed to fetch kernel.org releases"
    fi

    while read -r version; do
        STABLE["$(version_series "${version}")"]="${version}"
    done < <(echo "${json}" | jq -r "${filter} | select(.iseol | not) | .version")

    NEWEST_STABLE="$(echo "${json}" | jq -r "${filter} | select(.moniker != \"longterm\") | .version" | sort -V | tail -n1)"
}

# Load the open pull requests created by the autoupdate workflows.
load_pull_requests()
{
    if ! PULL_REQUESTS="$(gh pr list -R "${REPOSITORY}" --state open -L 100 \
            --json number,title,headRefName,isDraft \
            --jq '[.[] | select(.headRefName | startswith("autoupdate/"))] | sort_by(.number)' 2>/dev/null)"; then
        log "WARNING: Failed to list pull requests"
        PULL_REQUESTS="[]"
    fi

    if ! CLOSED_PULL_REQUESTS="$(gh pr list -R "${REPOSITORY}" --state closed -L 100 \
            --json number,headRefName,mergedAt \
            --jq '[.[] | select((.headRefName | startswith("autoupdate/")) and .mergedAt == null)]' 2>/dev/null)"; then
        log "WARNING: Failed to list closed pull requests"
        CLOSED_PULL_REQUESTS="[]"
    fi
}

# Print the number of a pull request updating a package (script) to a version
# that has been closed without merging, if any.
closed_pull_request()
{
    echo "${CLOSED_PULL_REQUESTS}" | jq -r --arg prefix "autoupdate/$1-$2-" \
        '[.[] | select(.headRefName | startswith($prefix))] | .[0].number // empty'
}

# Load the recent update workflow runs.
load_update_runs()
{
    if ! UPDATE_RUNS="$(gh run list -R "${REPOSITORY}" -w update-package.yml -L 50 \
            --json status,conclusion,displayTitle,url 2>/dev/null)"; then
        log "WARNING: Failed to list update runs"
        UPDATE_RUNS="[]"
    fi
}

# Print the URL of a queued or running update of a package (script), if any.
update_run()
{
    echo "${UPDATE_RUNS}" | jq -r --arg title "Update $1 package" \
        '[.[] | select(.status != "completed" and (.displayTitle | startswith($title)))] | .[0].url // empty'
}

# Print the URL of the latest update run of a package (script) to a version,
# if it failed.
failed_update_run()
{
    echo "${UPDATE_RUNS}" | jq -r --arg title "Update $1 package to $2" \
        '[.[] | select(.status == "completed" and .displayTitle == $title)] | .[0]
        | select(. != null and .conclusion != "success") | .url'
}

# Print references to the open autoupdate pull requests whose branch starts
# with the given prefix (e.g. " (#123, #124)").
pull_request_refs()
{
    local refs

    refs="$(echo "${PULL_REQUESTS}" | jq -r --arg prefix "$1" \
        '[.[] | select(.headRefName | startswith($prefix)) | "#\(.number)"] | join(", ")')"

    if [ -n "${refs}" ]; then
        echo " (${refs})"
    fi
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

    PACKAGE_NAMES[${script}]="${PACKAGE_NAMES[${script}]:+${PACKAGE_NAMES[${script}]}, }${name}"

    load_kv cur "${PACKAGE_SCRIPTS}/${script}.sh" current
    # Don't let a temporarily unreachable upstream break the whole report
    load_kv latest "${PACKAGE_SCRIPTS}/${script}.sh" latest || true

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
        UPDATE_VERSION[${script}]="${base}"

        if load_kv check "${PACKAGE_SCRIPTS}/${script}.sh" check "${base}"; then
            status="🟢 Ready to update to ${base}"
            UPDATE_READY[${script}]=1
        else
            status="⏳ Waiting for upstream: ${check[reason]:-check failed}"
            UPDATE_READY[${script}]=0
        fi
    elif [[ "${released}" == ❌* ]]; then
        status="🏷️ Release tag missing"
    elif [ -n "${stable}" ] && version_lt "${cur[version]}" "${stable}"; then
        status="⏳ Waiting for patches (${stable})"
    else
        status="✅ Up to date"
    fi

    status="${status}$(pull_request_refs "autoupdate/${script}-")"

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
# Updates
# ============================================================================

is_auto()
{
    [[ " ${UPDATE_REQUESTS:-} " == *" auto:$1 "* ]]
}

is_retry()
{
    [[ " ${UPDATE_REQUESTS:-} " == *" retry:$1 "* ]]
}

# Determine the update state of a package (script) and whether it is (still)
# to be updated automatically, and start the update if it is and upstream is
# ready. Sets UPDATE_STATE and UPDATE_AUTO.
update_state()
{
    local script="$1"
    local version="${UPDATE_VERSION[${script}]:-}"
    local run
    local pr

    UPDATE_AUTO=0
    if is_auto "${script}"; then
        UPDATE_AUTO=1
    fi

    if [ -z "${version}" ]; then
        UPDATE_STATE="✅ up to date"
        return
    fi

    pr="$(pull_request_refs "autoupdate/${script}-")"

    if [ -n "${pr}" ]; then
        UPDATE_STATE="update pull request open${pr}"
        return
    fi

    # Don't start an update twice
    run="$(update_run "${script}")"

    if [ -n "${run}" ]; then
        UPDATE_STATE="🔄 [update to ${version} running](${run})"
        return
    fi

    # Don't retry failed or rejected updates, but stop updating the package
    # automatically, unless its box has just been checked (again)
    if ! is_retry "${script}"; then
        run="$(failed_update_run "${script}" "${version}")"
        pr="$(closed_pull_request "${script}" "${version}")"

        if [ -n "${run}" ]; then
            UPDATE_STATE="⚠️ [update to ${version} failed](${run}), check to retry"
            UPDATE_AUTO=0
            return
        elif [ -n "${pr}" ]; then
            UPDATE_STATE="⚠️ update to ${version} skipped, #${pr} was closed, check to update anyway"
            UPDATE_AUTO=0
            return
        fi
    fi

    if [ "${UPDATE_AUTO}" = 1 ] && [ "${UPDATE_READY[${script}]}" = 1 ]; then
        if [ -n "${UPDATE_DISPATCH:-}" ]; then
            echo "${script} ${version}" >> "${UPDATE_DISPATCH}"
        fi

        UPDATE_STATE="🔄 update to ${version} started"
    elif [ "${UPDATE_AUTO}" = 1 ]; then
        UPDATE_STATE="⏳ update to ${version} when upstream is ready"
    elif [ "${UPDATE_READY[${script}]}" = 1 ]; then
        UPDATE_STATE="🟢 update to ${version} ready"
    else
        UPDATE_STATE="⏳ update to ${version} waiting for upstream"
    fi
}

print_updates()
{
    local script
    local box

    echo "## Automatic updates"
    echo ""
    echo "Check a package to update it automatically whenever it is behind our patches, i.e. as soon as upstream is ready after a patch update has been merged (maintainers only). If an update fails or its pull request is closed without merging, the package is unchecked."
    echo ""

    for script in $(printf '%s\n' "${!PACKAGE_NAMES[@]}" | sort); do
        update_state "${script}"

        if [ "${UPDATE_AUTO}" = 1 ]; then
            box="x"
        else
            box=" "
        fi

        echo "- [${box}] ${PACKAGE_NAMES[${script}]}: ${UPDATE_STATE} <!-- auto:${script} -->"
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

        tag="$(github_tags "${KERNEL_REPOSITORY}" "surface/v${series}" "surface/v$(regex_escape "${series}")(\.[0-9]+)?" | tail -n1)"
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

        status="${status}$(pull_request_refs "autoupdate/patches-v${series}-")"

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
        | select(.workflowName | test("^(Sync Kernel with Upstream|Rebase and Build Kernel|Generate Kernel Patches|Update Package|Arch|Debian|Fedora [0-9]+)$"))
        | . + { key: (if (.workflowName | test("^(Rebase|Generate|Update)")) then .workflowName + .displayTitle else .workflowName end) }]
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

print_pull_requests()
{
    local prs

    prs="$(echo "${PULL_REQUESTS}" | jq -r '.[] | "- #\(.number)\(if .isDraft then " (draft)" else "" end)"')"

    if [ -z "${prs}" ]; then
        return
    fi

    echo "## Open update pull requests"
    echo ""
    echo "${prs}"
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
load_pull_requests
load_update_runs

echo "<!-- package-status -->"
echo "# Package status"
echo ""

print_packages
print_updates
print_branches
print_pull_requests
print_notices
print_failures

if [ -n "${GITHUB_RUN_ID:-}" ]; then
    echo "_Updated ${TODAY} by [workflow run](${GITHUB_SERVER_URL:-https://github.com}/${REPOSITORY}/actions/runs/${GITHUB_RUN_ID})._"
else
    echo "_Updated ${TODAY}._"
fi
