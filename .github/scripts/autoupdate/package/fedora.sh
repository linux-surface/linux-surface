#!/usr/bin/env bash
#
# Fedora package update helper
#
# The package sources are shared between all Fedora versions we build for.
#
# Usage:
#   fedora.sh current          Print the currently packaged version
#   fedora.sh latest           Print the latest upstream versions for the series
#   fedora.sh check <version>  Check whether upstream sources for <version> exist
#   fedora.sh update <version> Update the package to <version>
#
# The update command bumps the kernel-ark tag (or, if <version> is already
# packaged, the package release) and refreshes our build-system patches. The
# patches in patches/<series> must be for <version> (see patches/<series>/BASE).
#
# All patches are applied to the new kernel-ark tag in the order used for
# building. Patches that still apply are left untouched. If a patch only
# applies with a 3-way merge, all patches in its directory are regenerated.
# This doubles as a quick validation of the update.
#
# Environment variables (update):
#   WORK_DIR:    (optional) Directory for the kernel-ark checkout
#   ARK_REPO:    (optional) kernel-ark repository URL (for testing)
#   STABLE_REPO: (optional) Stable kernel repository URL (for testing)
#
# Upstream sources:
#   - kernel-ark tag (gitlab.com/cki-project/kernel-ark)

# shellcheck source-path=SCRIPTDIR source=common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

BUILD_SCRIPT="${REPO_ROOT}/pkg/fedora/kernel-surface/build-linux-surface.py"

ARK_REPO="${ARK_REPO:-https://gitlab.com/cki-project/kernel-ark.git}"
STABLE_REPO="${STABLE_REPO:-https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git}"

PACKAGE_DIR="${REPO_ROOT}/pkg/fedora/kernel-surface"

build_var()
{
    sed -nE "s/^$1 = \"(.*)\"\$/\\1/p" "${BUILD_SCRIPT}" | head -n1
}

current_version()
{
    build_var PACKAGE_TAG | cut -d- -f2
}

cmd_current()
{
    local version

    version="$(current_version)"

    emit series "$(version_series "${version}")"
    emit version "${version}"
    emit release "$(build_var PACKAGE_RELEASE)"
    emit package_version "${version}-$(build_var PACKAGE_RELEASE)"
}

cmd_latest()
{
    local series
    local tag

    series="$(version_series "$(current_version)")"

    tag="$(remote_tags "${ARK_REPO}" "kernel-${series}." "kernel-$(regex_escape "${series}")\.[0-9]+-0" | tail -n1)"

    emit upstream "$(echo "${tag}" | cut -d- -f2)"
}

cmd_check()
{
    local version
    local tag

    version="$(parse_version "${1:-}")"
    tag="kernel-${version}-0"

    if [ -z "$(remote_tags "${ARK_REPO}" "${tag}" "$(regex_escape "${tag}")")" ]; then
        emit status pending
        emit reason "kernel-ark has no ${tag} tag yet"
        return "${EXIT_PENDING}"
    fi

    emit status ready
    emit upstream "${version}"
    emit ark_tag "${tag}"
}

# Apply all patches of a directory to the kernel-ark checkout in the current
# directory, regenerating them if any of them needs a 3-way merge.
#
# Usage: apply_patch_dir <dir> <tag>
# Prints the directory (relative to the repository root) if it was refreshed.
apply_patch_dir()
{
    local dir="$1"
    local tag="$2"
    local base
    local patch
    local refresh=0
    local files=()
    local new=()
    local i

    mapfile -t files < <(find "${dir}" -maxdepth 1 -name '*.patch' -type f | sort)

    if [ "${#files[@]}" -eq 0 ]; then
        return
    fi

    base="$(git rev-parse HEAD)"

    for patch in "${files[@]}"; do
        if git am -q "${patch}" >&2; then
            continue
        fi

        git am --abort >&2

        if ! git am -q -3 "${patch}" >&2; then
            git am --abort >&2 || true
            die "Patch ${patch#"${REPO_ROOT}"/} does not apply to ${tag}"
        fi

        log "Patch ${patch#"${REPO_ROOT}"/} needed a 3-way merge"
        refresh=1
    done

    if [ "${refresh}" -eq 0 ]; then
        return
    fi

    # Regenerate the patches, keeping the existing file names
    rm -rf "${WORK_DIR}/format-patch"
    git format-patch -q -o "${WORK_DIR}/format-patch" "${base}..HEAD" >&2
    mapfile -t new < <(find "${WORK_DIR}/format-patch" -name '*.patch' -type f | sort)

    if [ "${#new[@]}" -ne "${#files[@]}" ]; then
        die "Regenerating patches in ${dir} produced ${#new[@]} instead of ${#files[@]} patches"
    fi

    for i in "${!files[@]}"; do
        mv "${new[$i]}" "${files[$i]}"
    done

    echo "${dir#"${REPO_ROOT}"/}"
}

cmd_update()
{
    local version
    local series
    local base
    local current
    local release
    local old_tag
    local tag
    local surface_patch
    local patch
    local refreshed=()

    version="$(parse_version "${1:-}")"
    series="$(version_series "${version}")"
    base="$(patches_base "${series}")"

    if [ "${base}" != "${version}" ]; then
        die "patches/${series} are for ${base:-an unknown version}, not ${version}"
    fi

    current="$(current_version)"
    release="$(build_var PACKAGE_RELEASE)"

    if [ "${current}" = "${version}" ]; then
        release=$((release + 1))
    elif version_le "${version}" "${current}"; then
        die "Refusing to downgrade from ${current} to ${version}"
    else
        release=1
    fi

    old_tag="$(build_var PACKAGE_TAG)"
    tag="kernel-${version}-0"

    WORK_DIR="${WORK_DIR:-$(mktemp -d)}"
    mkdir -p "${WORK_DIR}"

    # Fetch the old and new tag without history. The old tag is needed for
    # 3-way merges of our build-system patches, blobs are fetched on demand.
    #
    # Some surface patches only apply to kernel-ark with a 3-way merge, which
    # needs the upstream version of the files they change. So also fetch the
    # upstream stable tag (kernel.org doesn't support partial clones).
    log "Fetching ${old_tag}, ${tag}, and v${version}..."
    rm -rf "${WORK_DIR}/kernel-ark"
    git init -q "${WORK_DIR}/kernel-ark"

    pushd "${WORK_DIR}/kernel-ark" > /dev/null

    git config user.name "surfacebot"
    git config user.email "surfacebot@users.noreply.github.com"
    git config maintenance.auto false

    git remote add origin "${ARK_REPO}"
    git fetch -q --depth=1 --filter=blob:none origin \
        "refs/tags/${old_tag}:refs/tags/${old_tag}" \
        "refs/tags/${tag}:refs/tags/${tag}" >&2

    git remote add stable "${STABLE_REPO}"
    git fetch -q --depth=1 stable "refs/tags/v${version}:refs/tags/v${version}" >&2
    # Replay our build-system patches on the old tag. 3-way merges need the
    # blobs the patches were generated against, which only exist after the
    # preceding patches of the series have been applied. The patches only
    # touch redhat/, so a sparse checkout of that is sufficient.
    log "Replaying build-system patches on ${old_tag}..."
    git worktree add -q --no-checkout --detach "${WORK_DIR}/replay" "${old_tag}" >&2

    pushd "${WORK_DIR}/replay" > /dev/null
    git sparse-checkout set --no-cone 'redhat/' >&2
    git checkout -q --detach "${old_tag}" >&2

    for patch in $(find "${PACKAGE_DIR}/patches" -maxdepth 1 -name '*.patch' -type f | sort) \
                 $(find "${PACKAGE_DIR}/secureboot" -maxdepth 1 -name '*.patch' -type f | sort); do
        if ! git am -q "${patch}" >&2; then
            git am --abort >&2 || true
            log "WARNING: ${patch#"${REPO_ROOT}"/} does not apply to ${old_tag}, 3-way merges may fail"
            break
        fi
    done
    popd > /dev/null

    git checkout -q --detach "${tag}" >&2

    # Apply the surface patches. These are generated from our kernel branches
    # and can't be fixed here.
    log "Applying patches/${series}..."
    for surface_patch in $(find "${REPO_ROOT}/patches/${series}" -maxdepth 1 -name '*.patch' -type f | sort); do
        if ! git am -q -3 "${surface_patch}" >&2; then
            git am --abort >&2 || true
            die "Patch ${surface_patch#"${REPO_ROOT}"/} does not apply to ${tag}"
        fi
    done

    # Apply our build-system patches, in the order used by build-linux-surface.py
    log "Applying build-system patches..."
    : > "${WORK_DIR}/refreshed"
    apply_patch_dir "${PACKAGE_DIR}/patches" "${tag}" >> "${WORK_DIR}/refreshed"
    apply_patch_dir "${PACKAGE_DIR}/secureboot" "${tag}" >> "${WORK_DIR}/refreshed"
    mapfile -t refreshed < "${WORK_DIR}/refreshed"

    popd > /dev/null

    # Bump the version
    sed -i -E \
        -e "s/^PACKAGE_TAG = \".*\"\$/PACKAGE_TAG = \"${tag}\"/" \
        -e "s/^PACKAGE_RELEASE = \".*\"\$/PACKAGE_RELEASE = \"${release}\"/" \
        "${BUILD_SCRIPT}"

    emit version "${version}"
    emit release "${release}"
    emit package_version "${version}-${release}"
    emit ark_tag "${tag}"
    emit refreshed "$(IFS=,; echo "${refreshed[*]}")"
}

case "${1:-}" in
current)
    cmd_current
    ;;
latest)
    cmd_latest
    ;;
check)
    cmd_check "${2:-}"
    ;;
update)
    cmd_update "${2:-}"
    ;;
*)
    die "Usage: $0 {current|latest|check <version>|update <version>}"
    ;;
esac
