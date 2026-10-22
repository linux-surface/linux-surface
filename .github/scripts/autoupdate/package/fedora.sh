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
#
# Upstream sources:
#   - kernel-ark tag (gitlab.com/cki-project/kernel-ark)

# shellcheck source-path=SCRIPTDIR source=common.sh
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

BUILD_SCRIPT="${REPO_ROOT}/pkg/fedora/kernel-surface/build-linux-surface.py"

ARK_REPO="https://gitlab.com/cki-project/kernel-ark.git"

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
