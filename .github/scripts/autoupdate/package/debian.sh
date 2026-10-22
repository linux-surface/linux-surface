#!/usr/bin/env bash
#
# Debian package update helper
#
# Usage:
#   debian.sh current          Print the currently packaged version
#   debian.sh latest           Print the latest upstream versions for the series
#   debian.sh check <version>  Check whether upstream sources for <version> exist
#   debian.sh update <version> Update the package to <version>
#
# The update command bumps the kernel version (or, if <version> is already
# packaged, the package revision), updates the base config to the latest
# Ubuntu mainline build available for <version>, and refreshes our packaging
# patches.
# The patches in patches/<series> must be for <version> (see
# patches/<series>/BASE).
#
# Our packaging patches only change the kernel source, so they are refreshed
# against the upstream stable tags: Each patch is applied to the currently
# packaged version, and then applied to <version>. Patches that still apply
# are left untouched. Otherwise, the patch is cherry-picked (i.e. 3-way merged)
# and regenerated. Finally, the surface patches are applied on top, as done
# when building. This doubles as a quick validation of the update.
#
# Environment variables (update):
#   WORK_DIR:     (optional) Directory for the kernel checkout
#   STABLE_REPO:  (optional) Stable kernel repository URL (for testing)
#   MAINLINE_URL: (optional) Ubuntu mainline build URL (for testing)
#
# Upstream sources:
#   - kernel source tag (vX.Y.Z) from kernel.org
#   - Ubuntu mainline build (kernel.ubuntu.com/mainline/vX.Y.Z), providing the
#     base config (config.amd64). These may lag behind the kernel version; we
#     use the latest one available for the series. If kernel.ubuntu.com is not
#     reachable, the current base config is kept.

# shellcheck source-path=SCRIPTDIR source=common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

VERSION_CONF="${REPO_ROOT}/pkg/debian/kernel/version.conf"

MAINLINE_URL="${MAINLINE_URL:-https://kernel.ubuntu.com/mainline}"
STABLE_REPO="${STABLE_REPO:-https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git}"

PACKAGE_DIR="${REPO_ROOT}/pkg/debian/kernel"

conf_var()
{
    sed -nE "s/^$1=\"(.*)\"\$/\\1/p" "${VERSION_CONF}" | head -n1
}

# List the kernel source versions of a series (from kernel.org).
source_versions()
{
    local series="$1"

    remote_tags "${STABLE_REPO}" "v${series}." "v$(regex_escape "${series}")\.[0-9]+" | sed 's/^v//'
}

# List the Ubuntu mainline build versions of a series.
packaging_versions()
{
    local series="$1"
    local listing

    if ! listing="$(curl -sf --retry 3 "${MAINLINE_URL}/")"; then
        log "ERROR: Failed to list Ubuntu mainline builds"
        return 1
    fi

    echo "${listing}" | grep -oE "href=\"v$(regex_escape "${series}")\.[0-9]+/\"" \
        | sed -E 's/href="v(.*)\/"/\1/' | sort -uV
}

# Print the URL of the generic amd64 headers package of an Ubuntu mainline
# build, which contains the kernel config. Fails if the build has no (amd64)
# packages, e.g. because it failed.
headers_url()
{
    local version="$1"
    local url="${MAINLINE_URL}/v${version}/amd64"
    local deb

    deb="$(curl -sf --retry 3 "${url}/" \
        | grep -oE 'href="linux-headers-[^"]*-generic_[^"]*_amd64\.deb"' \
        | sed -E 's/href="(.*)"/\1/' | head -n1)" || true

    if [ -z "${deb}" ]; then
        return 1
    fi

    echo "${url}/${deb}"
}

# Determine the Ubuntu mainline build to take the base config from: the latest
# one with amd64 packages that is not newer than the kernel version. If
# kernel.ubuntu.com is unavailable, keep the current one, as it may lag behind
# anyway.
resolve_packaging()
{
    local version="$1"
    local versions
    local v

    if ! versions="$(packaging_versions "$(version_series "${version}")")"; then
        log "WARNING: Could not list Ubuntu mainline builds, keeping the current one"
        conf_var UBUNTU_MAINLINE_VERSION
        return
    fi

    for v in $(echo "${versions}" | sort -rV); do
        if version_le "${v}" "${version}" && headers_url "${v}" > /dev/null; then
            echo "${v}"
            return
        fi
    done
}

# Extract the kernel config of an Ubuntu mainline build.
#
# Usage: fetch_base_config <version> <output>
fetch_base_config()
{
    local version="$1"
    local output="$2"
    local dir="${WORK_DIR}/ubuntu-${version}"
    local url

    if ! url="$(headers_url "${version}")"; then
        log "ERROR: No amd64 headers package for Ubuntu mainline build ${version}"
        return 1
    fi

    log "Fetching ${url}..."
    rm -rf "${dir}"
    mkdir -p "${dir}"

    curl -sfL --retry 3 -o "${dir}/headers.deb" "${url}" || return 1
    (cd "${dir}" && ar x headers.deb) || return 1
    tar -xf "${dir}"/data.tar.* -C "${dir}" --wildcards '*/.config' || return 1

    cp "$(find "${dir}" -name .config -type f | head -n1)" "${output}"
}

cmd_current()
{
    local version

    version="$(conf_var KERNEL_VERSION)"

    emit series "$(version_series "${version}")"
    emit version "${version}"
    emit release "$(conf_var KERNEL_REVISION)"
    emit package_version "${version}-$(conf_var KERNEL_REVISION)"
    emit packaging "$(conf_var UBUNTU_MAINLINE_VERSION)"
}

cmd_latest()
{
    local series
    local packaging

    series="$(version_series "$(conf_var KERNEL_VERSION)")"

    emit upstream "$(source_versions "${series}" | tail -n1)"

    if packaging="$(packaging_versions "${series}")"; then
        emit packaging "$(echo "${packaging}" | tail -n1)"
    else
        emit packaging "? (kernel.ubuntu.com unavailable)"
    fi
}

cmd_check()
{
    local version
    local series
    local packaging

    version="$(parse_version "${1:-}")"
    series="$(version_series "${version}")"

    if ! source_versions "${series}" | grep -qx "$(regex_escape "${version}")"; then
        emit status pending
        emit reason "kernel.org has no v${version} tag yet"
        return "${EXIT_PENDING}"
    fi

    packaging="$(resolve_packaging "${version}")"

    if [ -z "${packaging}" ]; then
        emit status pending
        emit reason "no Ubuntu mainline build for ${series} yet"
        return "${EXIT_PENDING}"
    fi

    emit status ready
    emit upstream "${version}"
    emit packaging "${packaging}"
}

cmd_update()
{
    local version
    local series
    local current
    local release
    local packaging
    local config_updated=0
    local refspecs=()
    local files=()
    local commits=()
    local refreshed=0
    local patch
    local i

    version="$(parse_version "${1:-}")"
    series="$(version_series "${version}")"
    require_patches_base "${version}"

    current="$(conf_var KERNEL_VERSION)"
    release="$(next_release "${current}" "$(conf_var KERNEL_REVISION)" "${version}")"

    packaging="$(resolve_packaging "${version}")"

    if [ -z "${packaging}" ]; then
        die "No Ubuntu mainline packaging available for ${version}"
    fi

    WORK_DIR="${WORK_DIR:-$(mktemp -d)}"
    mkdir -p "${WORK_DIR}"

    # Update the base config if there is a newer Ubuntu mainline build
    if [ "${packaging}" != "$(conf_var UBUNTU_MAINLINE_VERSION)" ] || [ ! -f "${PACKAGE_DIR}/config.amd64" ]; then
        log "Updating base config from Ubuntu mainline build ${packaging}..."

        if fetch_base_config "${packaging}" "${PACKAGE_DIR}/config.amd64"; then
            config_updated=1
        else
            packaging="$(conf_var UBUNTU_MAINLINE_VERSION)"
            log "WARNING: Could not update the base config, keeping Ubuntu mainline build ${packaging}"
        fi
    fi

    log "Fetching v${current} and v${version}..."
    rm -rf "${WORK_DIR}/linux"
    git init -q "${WORK_DIR}/linux"

    pushd "${WORK_DIR}/linux" > /dev/null

    git config user.name "surfacebot"
    git config user.email "surfacebot@users.noreply.github.com"
    git config maintenance.auto false

    # Both versions are the same when bumping the package revision
    mapfile -t refspecs < <(printf 'refs/tags/v%s:refs/tags/v%s\n' \
        "${current}" "${current}" "${version}" "${version}" | sort -u)

    git remote add origin "${STABLE_REPO}"
    git fetch -q --depth=1 origin "${refspecs[@]}" >&2

    mapfile -t files < <(find "${PACKAGE_DIR}" -maxdepth 1 -name '*.patch' -type f | sort)

    # Apply our packaging patches to the currently packaged version, which
    # gives us commits to 3-way merge from.
    log "Applying packaging patches to v${current}..."
    git checkout -q --detach "v${current}" >&2

    for patch in "${files[@]}"; do
        if ! git am -q "${patch}" >&2; then
            git am --abort >&2 || true
            die "Patch ${patch#"${REPO_ROOT}"/} does not apply to v${current}"
        fi

        commits+=("$(git rev-parse HEAD)")
    done

    log "Applying packaging patches to v${version}..."
    git checkout -q --detach "v${version}" >&2

    for i in "${!files[@]}"; do
        patch="${files[$i]}"

        if git am -q "${patch}" >&2; then
            continue
        fi

        git am --abort >&2

        if ! git cherry-pick "${commits[$i]}" >&2; then
            git cherry-pick --abort >&2 || true
            die "Patch ${patch#"${REPO_ROOT}"/} does not apply to v${version}"
        fi

        log "Patch ${patch#"${REPO_ROOT}"/} needed a 3-way merge"
        git format-patch -1 --stdout HEAD > "${patch}"
        refreshed=1
    done

    # Apply the surface patches on top, as done when building
    log "Applying patches/${series}..."
    for patch in $(find "${REPO_ROOT}/patches/${series}" -maxdepth 1 -name '*.patch' -type f | sort); do
        if ! git apply --index "${patch}" >&2; then
            die "Patch ${patch#"${REPO_ROOT}"/} does not apply on top of the packaging patches"
        fi
    done

    popd > /dev/null

    # Bump the version
    sed -i -E \
        -e "s/^KERNEL_VERSION=\".*\"\$/KERNEL_VERSION=\"${version}\"/" \
        -e "s/^KERNEL_REVISION=\".*\"\$/KERNEL_REVISION=\"${release}\"/" \
        -e "s/^UBUNTU_MAINLINE_VERSION=\".*\"\$/UBUNTU_MAINLINE_VERSION=\"${packaging}\"/" \
        "${VERSION_CONF}"

    emit version "${version}"
    emit release "${release}"
    emit package_version "${version}-${release}"
    emit packaging "${packaging}"
    emit config_updated "${config_updated}"

    if [ "${refreshed}" -eq 1 ]; then
        emit refreshed "${PACKAGE_DIR#"${REPO_ROOT}"/}"
    else
        emit refreshed ""
    fi
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
