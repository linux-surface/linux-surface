#!/usr/bin/env bash

set -euxo pipefail

if [ -z "${GITHUB_REPOSITORY_ID:-}" ]; then
    echo "GITHUB_REPOSITORY_ID is unset!"
    exit 1
fi

if [ -z "${GITHUB_WORKSPACE:-}" ]; then
    echo "GITHUB_WORKSPACE is unset!"
    exit 1
fi

IMAGE="${1:-}"

if [ -z "${IMAGE}" ]; then
    echo "Container image is unset!"
    exit 1
fi

if command -v docker &> /dev/null; then
    DOCKER="docker"
elif command -v podman &> /dev/null; then
    DOCKER="podman"
else
    echo "Could not find docker / podman!"
    exit 1
fi

exec "${DOCKER}" run -d --name "${GITHUB_REPOSITORY_ID}" \
    -v "${GITHUB_WORKSPACE}:/working" --workdir "/working" \
    --entrypoint "tail" "${IMAGE}" -f /dev/null