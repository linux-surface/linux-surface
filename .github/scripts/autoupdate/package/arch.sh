#!/usr/bin/env bash
#
# Arch Linux package update helper
#
# Usage:
#   arch.sh current           Print the currently packaged version
#   arch.sh latest [<series>] Print the latest upstream versions for the series
#                             (default: the currently packaged one)
#   arch.sh check <version>   Check whether upstream sources for <version> exist
#   arch.sh update <version>  Update the package to <version>
#
# The update command bumps pkgver to the latest Arch release of <version> (or,
# if that is already packaged, pkgrel), takes config.x86_64 from the Arch
# packaging, links the surface patches and config of the series, and updates
# the checksums. The patches in patches/<series> must be for <version> (see
# patches/<series>/BASE). Changes to the upstream
# PKGBUILD (other than version and checksums) are reported for review.
#
# As validation, the Arch patch and our patches are applied to the kernel
# source, as done by prepare() in the PKGBUILD.
#
# Environment variables (update):
#   WORK_DIR: (optional) Directory for downloads and the kernel source
#
# Upstream sources:
#   - kernel.org release tarball
#   - Arch kernel patch (release asset on github.com/archlinux/linux)
#   - Arch packaging (gitlab.archlinux.org), providing config.x86_64

# shellcheck source-path=SCRIPTDIR source=common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

PKGBUILD="${REPO_ROOT}/pkg/arch/kernel/PKGBUILD"

ARCH_KERNEL_REPO="archlinux/linux"
ARCH_PACKAGING_REPO="https://gitlab.archlinux.org/archlinux/packaging/packages/linux.git"
ARCH_PACKAGING_RAW="https://gitlab.archlinux.org/archlinux/packaging/packages/linux/-/raw"

PACKAGE_DIR="${REPO_ROOT}/pkg/arch/kernel"

pkgbuild_var()
{
    sed -nE "s/^$1=(.*)\$/\\1/p" "${PKGBUILD}" | head -n1
}

current_version()
{
    pkgbuild_var pkgver | sed -E 's/\.arch[0-9]+$//'
}

cmd_current()
{
    local version

    version="$(current_version)"

    emit series "$(version_series "${version}")"
    emit version "${version}"
    emit release "$(pkgbuild_var pkgrel)"
    emit package_version "$(pkgbuild_var pkgver)-$(pkgbuild_var pkgrel)"
}

cmd_latest()
{
    local series
    local tag

    series="${1:-$(version_series "$(current_version)")}"

    tag="$(github_tags "${ARCH_KERNEL_REPO}" "v${series}." "v$(regex_escape "${series}")\.[0-9]+-arch[0-9]+" | tail -n1)"

    emit upstream "$(echo "${tag#v}" | tr - .)"
}

cmd_check()
{
    local version
    local major
    local tarball
    local tag
    local asset
    local pkgver
    local pkgtag

    version="$(parse_version "${1:-}")"
    major="${version%%.*}"

    # Kernel tarball
    tarball="https://cdn.kernel.org/pub/linux/kernel/v${major}.x/linux-${version}.tar.xz"

    if ! url_exists "${tarball}"; then
        emit status pending
        emit reason "kernel.org tarball for ${version} not available"
        return "${EXIT_PENDING}"
    fi

    # Arch kernel patch (prefer the latest archN release)
    tag="$(github_tags "${ARCH_KERNEL_REPO}" "v${version}-arch" "v$(regex_escape "${version}")-arch[0-9]+" | tail -n1)"

    if [ -z "${tag}" ]; then
        emit status pending
        emit reason "archlinux/linux has no v${version}-archN tag yet"
        return "${EXIT_PENDING}"
    fi

    asset="https://github.com/${ARCH_KERNEL_REPO}/releases/download/${tag}/linux-${tag}.patch.zst"

    if ! url_exists "${asset}"; then
        emit status pending
        emit reason "archlinux/linux release ${tag} has no patch asset yet"
        return "${EXIT_PENDING}"
    fi

    # Arch packaging (for config.x86_64), e.g. v7.2.11-arch1 -> 7.2.11.arch1-N
    pkgver="$(echo "${tag#v}" | tr - .)"
    pkgtag="$(remote_tags "${ARCH_PACKAGING_REPO}" "${pkgver}-" "$(regex_escape "${pkgver}")-[0-9]+" | tail -n1)"

    if [ -z "${pkgtag}" ]; then
        emit status pending
        emit reason "Arch packaging has no ${pkgver}-N tag yet"
        return "${EXIT_PENDING}"
    fi

    emit status ready
    emit upstream "${pkgver}"
    emit arch_tag "${tag}"
    emit packaging_tag "${pkgtag}"
}

# Remove version and checksums from a PKGBUILD, to compare the rest.
normalize_pkgbuild()
{
    awk '
        /^[a-z0-9_]*sums(_[a-z0-9_]+)?=\(/ { if ($0 !~ /\)/) skip = 1; next }
        skip { if ($0 ~ /\)/) skip = 0; next }
        /^pkg(ver|rel)=/ { next }
        { print }
    '
}

# Print the expanded entries of a source array of the PKGBUILD.
pkgbuild_sources()
{
    (cd "${PACKAGE_DIR}" && bash -c "source ./PKGBUILD > /dev/null 2>&1; printf '%s\n' \"\${$1[@]}\"")
}

# Print the checksums (of the given type) for the entries of a source array.
# Remote sources are expected in ${WORK_DIR}/sources.
#
# Usage: source_sums <array> <b2|sha256>
source_sums()
{
    local entry
    local name
    local file

    while read -r entry; do
        name="${entry%%::*}"
        name="${name##*/}"

        if [[ "${name}" = *.sig || "${name}" = *.sign ]]; then
            echo "SKIP"
            continue
        fi

        if [[ "${entry}" = *://* ]]; then
            file="${WORK_DIR}/sources/${name}"
        else
            file="${PACKAGE_DIR}/${name}"
        fi

        "$2sum" "${file}" | cut -d' ' -f1
    done < <(pkgbuild_sources "$1")
}

# Replace a (checksum) array in the PKGBUILD, keeping its formatting.
#
# Usage: replace_array <name> <values...>
replace_array()
{
    local name="$1"
    local indent

    shift
    indent="$(printf '%*s' "$((${#name} + 2))" '')"

    awk -v name="${name}" -v indent="${indent}" -v values="$*" '
        $0 ~ "^" name "=\\(" {
            n = split(values, v, " ")
            for (i = 1; i <= n; i++) {
                printf "%s'"'"'%s'"'"'%s\n", (i == 1 ? name "=(" : indent), v[i], (i == n ? ")" : "")
            }
            if ($0 !~ /\)/) skip = 1
            next
        }
        skip { if ($0 ~ /\)/) skip = 0; next }
        { print }
    ' "${PKGBUILD}" > "${PKGBUILD}.new"

    mv "${PKGBUILD}.new" "${PKGBUILD}"
}

# Link the surface patches and config of a series into the package directory
# and list the patches in the source array of the PKGBUILD. This is done on
# every update, as patches may be added or removed with any kernel version.
#
# Usage: link_surface_sources <series>
link_surface_sources()
{
    local series="$1"
    local config="configs/surface-${series}.config"
    local patches=()
    local patch

    if [ ! -f "${REPO_ROOT}/${config}" ]; then
        die "${config} does not exist"
    fi

    mapfile -t patches < <(find "${REPO_ROOT}/patches/${series}" -maxdepth 1 -name '*.patch' -type f -printf '%f\n' | sort)

    if [ "${#patches[@]}" -eq 0 ]; then
        die "patches/${series} contains no patches"
    fi

    log "Linking ${config} and ${#patches[@]} patches from patches/${series}..."
    ln -sfn "../../../${config}" "${PACKAGE_DIR}/surface.config"

    find "${PACKAGE_DIR}" -maxdepth 1 -name '*.patch' -type l -delete
    for patch in "${patches[@]}"; do
        ln -s "../../../patches/${series}/${patch}" "${PACKAGE_DIR}/${patch}"
    done

    # Replace the local patch entries of the source array
    awk -v patches="${patches[*]}" '
        /^source=\(/ { insource = 1 }
        insource && /^[[:space:]]+[^[:space:]#:\/]+\.patch[[:space:]]*$/ { next }
        insource && /^\)/ {
            n = split(patches, p, " ")
            for (i = 1; i <= n; i++) {
                print "  " p[i]
            }
            insource = 0
        }
        { print }
    ' "${PKGBUILD}" > "${PKGBUILD}.new"

    mv "${PKGBUILD}.new" "${PKGBUILD}"
}

cmd_update()
{
    local version
    local -A check=()
    local out
    local rc=0
    local key
    local value
    local entry
    local pkgver
    local pkgrel
    local old_pkgtag
    local srcdir
    local array
    local type
    local sums=()
    local patch
    local upstream_changed=0

    version="$(parse_version "${1:-}")"
    require_patches_base "${version}"

    if ! version_le "$(current_version)" "${version}"; then
        die "Refusing to downgrade from $(current_version) to ${version}"
    fi

    out="$(cmd_check "${version}")" || rc=$?
    while IFS='=' read -r key value; do
        [ -n "${key}" ] && check["${key}"]="${value}"
    done <<< "${out}"

    if [ "${rc}" -ne 0 ]; then
        die "Upstream sources for ${version} are not available: ${check[reason]:-check failed}"
    fi

    # New pkgver resets pkgrel, otherwise bump it
    pkgver="${check[upstream]}"
    if [ "${pkgver}" = "$(pkgbuild_var pkgver)" ]; then
        pkgrel=$(($(pkgbuild_var pkgrel) + 1))
    else
        pkgrel=1
    fi

    WORK_DIR="${WORK_DIR:-$(mktemp -d)}"
    mkdir -p "${WORK_DIR}/sources"

    # Compare the upstream PKGBUILD, ignoring version and checksums
    old_pkgtag="$(remote_tags "${ARCH_PACKAGING_REPO}" "$(pkgbuild_var pkgver)-" \
        "$(regex_escape "$(pkgbuild_var pkgver)")-[0-9]+" | tail -n1)"

    log "Fetching Arch packaging ${check[packaging_tag]}..."
    curl -sfL --retry 3 -o "${WORK_DIR}/PKGBUILD.new" "${ARCH_PACKAGING_RAW}/${check[packaging_tag]}/PKGBUILD"
    curl -sfL --retry 3 -o "${PACKAGE_DIR}/config.x86_64" "${ARCH_PACKAGING_RAW}/${check[packaging_tag]}/config.x86_64"

    : > "${WORK_DIR}/upstream-pkgbuild.diff"
    if [ -n "${old_pkgtag}" ] && [ "${old_pkgtag}" != "${check[packaging_tag]}" ]; then
        curl -sfL --retry 3 -o "${WORK_DIR}/PKGBUILD.old" "${ARCH_PACKAGING_RAW}/${old_pkgtag}/PKGBUILD"

        if ! diff -u --label "PKGBUILD (${old_pkgtag})" --label "PKGBUILD (${check[packaging_tag]})" \
                <(normalize_pkgbuild < "${WORK_DIR}/PKGBUILD.old") \
                <(normalize_pkgbuild < "${WORK_DIR}/PKGBUILD.new") > "${WORK_DIR}/upstream-pkgbuild.diff"; then
            log "Upstream PKGBUILD changed between ${old_pkgtag} and ${check[packaging_tag]}"
            upstream_changed=1
        fi
    fi

    # Bump the version
    sed -i -E \
        -e "s/^pkgver=.*\$/pkgver=${pkgver}/" \
        -e "s/^pkgrel=.*\$/pkgrel=${pkgrel}/" \
        "${PKGBUILD}"

    link_surface_sources "$(version_series "${version}")"

    # Download the remote sources (except signatures)
    while read -r entry; do
        if [[ "${entry}" = *://* && "${entry}" != *.sig && "${entry}" != *.sign ]]; then
            log "Downloading ${entry}..."
            curl -sfL --retry 3 -o "${WORK_DIR}/sources/${entry##*/}" "${entry}"
        fi
    done < <(pkgbuild_sources source)

    # Update the checksums
    for array in b2sums sha256sums b2sums_x86_64 sha256sums_x86_64; do
        if ! grep -q "^${array}=(" "${PKGBUILD}"; then
            continue
        fi

        type="${array%%sums*}"
        if [[ "${array}" = *_x86_64 ]]; then
            mapfile -t sums < <(source_sums source_x86_64 "${type}")
        else
            mapfile -t sums < <(source_sums source "${type}")
        fi

        replace_array "${array}" "${sums[@]}"
    done

    # Validate: apply the patches as done by prepare()
    log "Applying patches to linux-${version}..."
    srcdir="${WORK_DIR}/src"
    rm -rf "${srcdir}"
    mkdir -p "${srcdir}"
    tar -xf "${WORK_DIR}/sources/linux-${version}.tar.xz" -C "${srcdir}"

    while read -r entry; do
        patch="${entry##*/}"

        if [[ "${patch}" = *.patch.zst ]]; then
            zstd -dcq "${WORK_DIR}/sources/${patch}" > "${WORK_DIR}/sources/${patch%.zst}"
            patch="${WORK_DIR}/sources/${patch%.zst}"
        elif [[ "${patch}" = *.patch ]]; then
            patch="${PACKAGE_DIR}/${patch}"
        else
            continue
        fi

        if ! (cd "${srcdir}/linux-${version}" && patch -Np1 -s < "${patch}") >&2; then
            die "Patch ${patch##*/} does not apply to linux-${version}"
        fi
    done < <(pkgbuild_sources source)

    emit version "${version}"
    emit release "${pkgrel}"
    emit package_version "${version}-${pkgrel}"
    emit pkgver "${pkgver}-${pkgrel}"
    emit upstream_changed "${upstream_changed}"
    emit upstream_diff "${WORK_DIR}/upstream-pkgbuild.diff"
    emit refreshed ""
}

case "${1:-}" in
current)
    cmd_current
    ;;
latest)
    cmd_latest "${2:-}"
    ;;
check)
    cmd_check "${2:-}"
    ;;
update)
    cmd_update "${2:-}"
    ;;
*)
    die "Usage: $0 {current|latest [<series>]|check <version>|update <version>}"
    ;;
esac
