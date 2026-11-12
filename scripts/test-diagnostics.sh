#!/usr/bin/env bash
set -e


script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
tmpdir="$(mktemp -d -t "surface-diagnostics-test.XXXXXX")"

on_exit () {
    rm -rf "${tmpdir}"
}
trap on_exit EXIT

mkdir -p "${tmpdir}/bin" "${tmpdir}/work"

printf '%s\n' '#!/usr/bin/env bash' \
    'echo "2026-01-01T00:00:00+00:00"' > "${tmpdir}/bin/date"
printf '%s\n' '#!/usr/bin/env bash' 'exit 0' > "${tmpdir}/bin/tree"
chmod +x "${tmpdir}/bin/date" "${tmpdir}/bin/tree"

archive="${tmpdir}/diagnostics.tar.gz"

(
    cd "${tmpdir}/work"
    PATH="${tmpdir}/bin:${PATH}" \
        bash "${script_dir}/diagnostics.sh" --output "${archive}" serial
)

test -s "${archive}"
tar -tzf "${archive}" | grep -qx './timestamp'

(
    cd "${tmpdir}/work"
    PATH="${tmpdir}/bin:${PATH}" \
        bash "${script_dir}/diagnostics.sh" --output diagnostics.tar.gz serial
    test -s diagnostics.tar.gz
)
