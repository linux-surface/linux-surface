#!/usr/bin/env bash

set -euxo pipefail

dnf()
{
    command dnf -y "$@"
}

if [ -z "${GIT_REF:-}" ]; then
	echo "GIT_REF is unset!"
	exit 1
fi

if [ -z "${GITHUB_REPOSITORY:-}" ]; then
	echo "GITHUB_REPOSITORY is unset!"
	exit 1
fi

if [ -z "${SURFACEBOT_TOKEN:-}" ]; then
	echo "SURFACEBOT_TOKEN is unset!"
	exit 1
fi

if [ -z "${BRANCH_STAGING:-}" ]; then
	echo "BRANCH_STAGING is unset!"
	exit 1
fi

FEDORA="${1:-}"

if [ -z "${FEDORA}" ]; then
	echo "Fedora version is unset!"
	exit 1
fi

REPONAME="$(echo "${GITHUB_REPOSITORY}" | cut -d'/' -f2)"
REPO="https://surfacebot:${SURFACEBOT_TOKEN}@github.com/linux-surface/repo.git"

# parse git tag from ref
GIT_TAG="${GIT_REF#refs/tags/}"

# Install dependencies
dnf install git findutils openssl

# clone package repository
git clone -b "${BRANCH_STAGING}" "${REPO}" repo

# copy packages
find "fedora-${FEDORA}-latest" -type f -print0 | xargs -0 -I '{}' cp {} "repo/fedora/f${FEDORA}"
pushd "repo/fedora/f${FEDORA}" || exit 1

# convert packages into references
while read -rd $'\n' FILE; do
    echo "${REPONAME}:${GIT_TAG}/$(basename "${FILE}")" > "${FILE}.blob"
    rm "${FILE}"
done <<< "$(find . -name '*.rpm' -type f)"

RAND="$(openssl rand -hex 16)"
BRANCH="${BRANCH_STAGING}-${RAND}"

# set git identity
git config --global user.name "surfacebot"
git config --global user.email "surfacebot@users.noreply.github.com"

# commit and push
git checkout -b "${BRANCH}"
git add .
git commit -m "Update Fedora ${FEDORA} ${REPONAME} package"
git push --set-upstream origin "${BRANCH}"

popd || exit 1