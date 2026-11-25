#!/usr/bin/env bash
#
# Rebase kernel branch onto latest stable tag
#
# This script must be run from within a linux-surface/kernel git repository.
# It will:
# 1. Detect the current base version using git describe
# 2. Detect the latest stable tag for the same kernel version
# 3. Check if already up-to-date using git merge-base
# 4. Perform rebase onto the target tag
# 5. Output metadata for subsequent build/tag/push steps
#
# Environment variables:
#   TARGET_TAG: (optional) Explicit target tag (e.g., "v6.18.5")
#               If not specified, auto-detects latest stable for detected version

set -euo pipefail

# Enable debug mode if DEBUG is set
if [ "${DEBUG:-}" = "1" ]; then
    set -x
fi

# ============================================================================
# Phase 1: Setup
# ============================================================================

echo "============================================"
echo "Kernel Branch Rebase Script"
echo "============================================"
echo ""

# Verify we're in a git repository
if ! git rev-parse --git-dir >/dev/null 2>&1; then
    echo "ERROR: Not in a git repository"
    echo "This script must be run from within the kernel repository"
    exit 1
fi

echo "[1/6] Configuring environment..."

# Configure git identity for rebase operations
git config user.name "surfacebot"
git config user.email "surfacebot@users.noreply.github.com"

echo "Environment configured"

# ============================================================================
# Phase 2: Detect Current Base Version
# ============================================================================

echo "[2/6] Detecting current base version..."

# Use git describe to find the current base version tag
# Pattern: v[0-9]*.[0-9]* matches vX.Y or vX.Y.Z (e.g., v6.18, v6.18.4, v7.0, v7.0.1)
CURRENT_BASE_VERSION=$(git describe --tags --match='v[0-9]*.[0-9]*' 2>/dev/null || true)

if [ -z "${CURRENT_BASE_VERSION}" ]; then
    echo "ERROR: Could not detect current base kernel version using git describe"
    echo "Make sure the branch has proper version tags in its history"
    exit 1
fi

echo "Current base version (git describe): ${CURRENT_BASE_VERSION}"

# Extract just the version tag using grep
# This handles formats like: v6.18, v6.18.4, v6.18.4-123-gabcdef
CURRENT_BASE_TAG=$(echo "${CURRENT_BASE_VERSION}" | grep -oE 'v[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1)

if [ -z "${CURRENT_BASE_TAG}" ]; then
    echo "ERROR: Could not extract version tag from: ${CURRENT_BASE_VERSION}"
    exit 1
fi

echo "Current base tag: ${CURRENT_BASE_TAG}"

# Extract kernel major.minor version from base tag (e.g., v6.18.4 → 6.18, v6.18 → 6.18)
KERNEL_VERSION=$(echo "${CURRENT_BASE_TAG}" | sed -E 's/^v([0-9]+\.[0-9]+)(\.[0-9]+)?$/\1/')

if [ -z "${KERNEL_VERSION}" ]; then
    echo "ERROR: Could not extract kernel version from tag: ${CURRENT_BASE_TAG}"
    exit 1
fi

echo "Kernel version: ${KERNEL_VERSION}"

# ============================================================================
# Phase 3: Target Tag Detection
# ============================================================================

echo "[3/6] Detecting target tag..."

# Check if TARGET_TAG was provided as input
if [ -n "${TARGET_TAG:-}" ]; then
    echo "Using provided target tag: ${TARGET_TAG}"

    # Validate format (vX.Y or vX.Y.Z)
    if ! echo "${TARGET_TAG}" | grep -qE '^v[0-9]+\.[0-9]+(\.[0-9]+)?$'; then
        echo "ERROR: Invalid target tag format: ${TARGET_TAG}"
        echo "Expected format: vX.Y or vX.Y.Z (e.g., v6.18, v6.18.4)"
        exit 1
    fi

    # Extract version from provided tag to validate it matches detected kernel version
    TAG_VERSION=$(echo "${TARGET_TAG}" | sed -E 's/^v([0-9]+\.[0-9]+)(\..*)?$/\1/')

    if [ "${TAG_VERSION}" != "${KERNEL_VERSION}" ]; then
        echo "ERROR: Target tag version '${TAG_VERSION}' doesn't match detected kernel version '${KERNEL_VERSION}'"
        exit 1
    fi

    # Verify the provided tag exists
    if ! git rev-parse "${TARGET_TAG}" >/dev/null 2>&1; then
        echo "ERROR: Provided target tag '${TARGET_TAG}' not found in repository"
        exit 1
    fi
else
    echo "Auto-detecting latest stable tag for version ${KERNEL_VERSION}..."

    # Find latest stable tag for same version
    # Pattern: v${KERNEL_VERSION}.* matches v6.18.1, v6.18.4, etc.
    # Filter out RC releases and sort by version
    TARGET_TAG=$(git tag --list "v${KERNEL_VERSION}.*" \
        --sort=-version:refname | \
        grep -v '\-rc' | \
        head -1)

    if [ -z "${TARGET_TAG}" ]; then
        echo "ERROR: No stable tags found for version ${KERNEL_VERSION}"
        echo "Available tags:"
        git tag --list "v${KERNEL_VERSION}.*" | head -10
        exit 1
    fi

    echo "Detected target tag: ${TARGET_TAG}"
fi

# Extract full version from target tag (v6.18.4 → 6.18.4)
FULL_VERSION=$(echo "${TARGET_TAG}" | sed -E 's/^v(.*)$/\1/')

if [ -z "${FULL_VERSION}" ]; then
    echo "ERROR: Could not extract full version from target tag: ${TARGET_TAG}"
    exit 1
fi

echo "Full version: ${FULL_VERSION}"

# ============================================================================
# Phase 4: Ancestor Check (Up-to-Date)
# ============================================================================

echo "[4/6] Checking if already up-to-date..."

# Check if target tag is already an ancestor of HEAD
if git merge-base --is-ancestor "${TARGET_TAG}" HEAD; then
    echo ""
    echo "============================================"
    echo "Already up-to-date!"
    echo "============================================"
    echo "Current branch already contains ${TARGET_TAG}"
    echo "No rebase needed."
    echo ""

    # Set outputs for GitHub Actions
    if [ -n "${GITHUB_OUTPUT:-}" ]; then
        echo "success=true" >> "${GITHUB_OUTPUT}"
        echo "already_uptodate=true" >> "${GITHUB_OUTPUT}"
        echo "target_tag=${TARGET_TAG}" >> "${GITHUB_OUTPUT}"
        echo "kernel_version=${KERNEL_VERSION}" >> "${GITHUB_OUTPUT}"
        echo "full_version=${FULL_VERSION}" >> "${GITHUB_OUTPUT}"
    fi

    exit 0
fi

echo "Branch needs rebasing onto ${TARGET_TAG}"

# ============================================================================
# Phase 6: Perform Rebase
# ============================================================================

echo "[5/6] Performing rebase..."

echo "Rebasing onto ${TARGET_TAG}..."

# Perform the rebase
if ! git rebase "${TARGET_TAG}"; then
    echo ""
    echo "============================================"
    echo "REBASE FAILED - CONFLICTS DETECTED"
    echo "============================================"
    echo ""
    echo "The rebase encountered conflicts. Manual intervention required."
    echo ""
    echo "Conflicted files:"
    git status --short | grep '^[UAD][UAD]' || echo "(see git status for details)"
    echo ""

    # Abort the rebase
    git rebase --abort

    echo "Rebase has been aborted."
    echo ""
    echo "To resolve manually:"
    echo "  1. Clone the repository locally"
    echo "  2. git checkout <branch>"
    echo "  3. git rebase ${TARGET_TAG}"
    echo "  4. Resolve conflicts"
    echo "  5. git rebase --continue"
    echo "  6. Force-push the result"
    echo ""

    exit 1
fi

echo "Rebase completed successfully!"

# ============================================================================
# Phase 6: Output Metadata
# ============================================================================

echo "[6/6] Setting outputs..."

# Set outputs for GitHub Actions
if [ -n "${GITHUB_OUTPUT:-}" ]; then
    echo "success=true" >> "${GITHUB_OUTPUT}"
    echo "already_uptodate=false" >> "${GITHUB_OUTPUT}"
    echo "target_tag=${TARGET_TAG}" >> "${GITHUB_OUTPUT}"
    echo "kernel_version=${KERNEL_VERSION}" >> "${GITHUB_OUTPUT}"
    echo "full_version=${FULL_VERSION}" >> "${GITHUB_OUTPUT}"
fi

echo ""
echo "============================================"
echo "Rebase complete!"
echo "============================================"
echo "Target tag: ${TARGET_TAG}"
echo "Kernel version: ${KERNEL_VERSION}"
echo "Full version: ${FULL_VERSION}"
