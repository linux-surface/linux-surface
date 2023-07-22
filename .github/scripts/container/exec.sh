#!/usr/bin/env bash

set -euxo pipefail

if [ -z "${GITHUB_REPOSITORY_ID:-}" ]; then
    echo "GITHUB_REPOSITORY_ID is unset!"
    exit 1
fi

if [ -z "${1:-}" ]; then
    echo "Arguments are unset!"
    exit 1
fi

ENVVARS=()
COMMAND=()

while (( "${#}" )); do
    case "$1" in
    -e)
        ENVVARS+=("-e")
        shift

        ENVVARS+=("$1")
        shift
        ;;
    --)
        shift
        while (( "${#}" )); do
            COMMAND+=("$1")
            shift
        done
        ;;
    esac
done

if command -v docker &> /dev/null; then
    DOCKER="docker"
elif command -v podman &> /dev/null; then
    DOCKER="podman"
else
    echo "Could not find docker / podman!"
    exit 1
fi

exec "${DOCKER}" exec "${ENVVARS[@]}" "${GITHUB_REPOSITORY_ID}" "${COMMAND[@]}"