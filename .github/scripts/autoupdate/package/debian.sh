#!/usr/bin/env bash
#
# Debian package update helper
#
# Usage:
#   debian.sh current          Print the currently packaged version
#   debian.sh latest           Print the latest upstream versions for the series
#   debian.sh check <version>  Check whether upstream sources for <version> exist
#
# Upstream sources (both from the Ubuntu mainline-crack repository):
#   - kernel source tag (vX.Y.Z)
#   - Ubuntu mainline packaging (cod/mainline/vX.Y.Z), which may lag behind the
#     kernel version; we use the latest one available for the series

# shellcheck source-path=SCRIPTDIR source=common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

VERSION_CONF="${REPO_ROOT}/pkg/debian/kernel/version.conf"

MAINLINE_REPO="https://git.launchpad.net/~ubuntu-kernel-test/ubuntu/+source/linux/+git/mainline-crack"
MAINLINE_PREFIX="cod/mainline/"

conf_var()
{
    sed -nE "s/^$1=\"(.*)\"\$/\\1/p" "${VERSION_CONF}" | head -n1
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
    local tags

    series="$(version_series "$(conf_var KERNEL_VERSION)")"
    tags="$(remote_tags "${MAINLINE_REPO}" "" "(${MAINLINE_PREFIX})?v$(regex_escape "${series}")\.[0-9]+")"

    emit upstream "$(echo "${tags}" | grep -v "^${MAINLINE_PREFIX}" | sed 's/^v//' | sort -V | tail -n1)"
    emit packaging "$(echo "${tags}" | grep "^${MAINLINE_PREFIX}" | sed "s|^${MAINLINE_PREFIX}v||" | sort -V | tail -n1)"
}

cmd_check()
{
    local version
    local series
    local tags
    local packaging

    version="$(parse_version "${1:-}")"
    series="$(version_series "${version}")"

    tags="$(remote_tags "${MAINLINE_REPO}" "" "(${MAINLINE_PREFIX})?v$(regex_escape "${series}")\.[0-9]+")"

    if ! echo "${tags}" | grep -qx "v$(regex_escape "${version}")"; then
        emit status pending
        emit reason "mainline-crack has no v${version} tag yet"
        return "${EXIT_PENDING}"
    fi

    # Latest packaging version that is not newer than the kernel version.
    packaging=""

    for v in $(echo "${tags}" | grep "^${MAINLINE_PREFIX}" | sed "s|^${MAINLINE_PREFIX}v||"); do
        if version_le "${v}" "${version}"; then
            packaging="${v}"
        fi
    done

    if [ -z "${packaging}" ]; then
        emit status pending
        emit reason "mainline-crack has no ${MAINLINE_PREFIX}v${series}.x packaging tag yet"
        return "${EXIT_PENDING}"
    fi

    emit status ready
    emit upstream "${version}"
    emit packaging "${packaging}"
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
*)
    die "Usage: $0 {current|latest|check <version>}"
    ;;
esac
