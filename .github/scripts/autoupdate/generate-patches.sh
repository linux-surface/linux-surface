#!/usr/bin/env bash
#
# Generate kernel patches from linux-surface/kernel repository
#
# This script:
# 1. Clones the linux-surface/kernel repository
# 2. Detects the base kernel version using git describe
# 3. Uses git-format-patchsets to generate patches
# 4. Outputs patches to the appropriate directory
#
# Environment variables:
#   KERNEL_BRANCH: The kernel branch to process (e.g., "v6.18-surface")
#   BASE_VERSION_TAG: (optional) Base version tag to use (e.g., "v6.18.4")
#                     If not specified, auto-detected using git describe
#   FORCE_UPDATE: (optional) Force update even if no changes

set -euo pipefail

# Enable debug mode if DEBUG is set
if [ "${DEBUG:-}" = "1" ]; then
    set -x
fi

# ============================================================================
# Configuration and validation
# ============================================================================

if [ -z "${KERNEL_BRANCH:-}" ]; then
    echo "ERROR: KERNEL_BRANCH environment variable is required"
    echo "Example: KERNEL_BRANCH=v6.18-surface"
    exit 1
fi

# Get absolute paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
WORK_DIR="${REPO_ROOT}/.tmp-patch-gen"

# Configuration
KERNEL_REPO="https://github.com/linux-surface/kernel"
GIT_TOOLS_REPO="https://github.com/qzed/git-tools"

echo "============================================"
echo "Generating patches for kernel branch ${KERNEL_BRANCH}"
echo "============================================"
echo "Kernel repo: ${KERNEL_REPO}"
echo "Branch: ${KERNEL_BRANCH}"
echo ""

# ============================================================================
# Setup working directory
# ============================================================================

echo "[1/6] Setting up working directory..."
rm -rf "${WORK_DIR}"
mkdir -p "${WORK_DIR}"
cd "${WORK_DIR}"

# ============================================================================
# Clone kernel repository
# ============================================================================

echo "[2/6] Cloning kernel repository..."

# Clone with limited history depth to get all our added patches.
# Depth 250 should be sufficient to reach the base tag in most cases.
if ! git clone --depth=250 --branch="${KERNEL_BRANCH}" "${KERNEL_REPO}" kernel; then
    echo "ERROR: Failed to clone kernel repository"
    echo "Check that branch '${KERNEL_BRANCH}' exists"
    exit 1
fi

cd kernel

echo "Successfully cloned ${KERNEL_BRANCH}"

# ============================================================================
# Detect base kernel version
# ============================================================================

echo "[3/6] Detecting base kernel version..."

# Check if BASE_VERSION_TAG was provided as input
if [ -n "${BASE_VERSION_TAG:-}" ]; then
    echo "Using provided base version tag: ${BASE_VERSION_TAG}"

    # Validate format
    if ! echo "${BASE_VERSION_TAG}" | grep -qE '^v[0-9]+\.[0-9]+(\.[0-9]+)?$'; then
        echo "ERROR: Invalid base version tag format: ${BASE_VERSION_TAG}"
        echo "Expected format: vX.Y or vX.Y.Z (e.g., v6.18, v6.18.4)"
        exit 1
    fi

    # Verify the provided tag exists
    if ! git rev-parse "${BASE_VERSION_TAG}" >/dev/null 2>&1; then
        echo "ERROR: Provided base version tag '${BASE_VERSION_TAG}' not found in repository"
        exit 1
    fi
else
    echo "Auto-detecting base version using git describe..."

    # Use git describe to find the base version tag
    # Pattern: v[0-9]*.[0-9]* matches vX.Y or vX.Y.Z (e.g., v6.18, v6.18.4, v7.0, v7.0.1)
    # This requires at least one digit after 'v' and after the first dot
    BASE_VERSION=$(git describe --tags --match='v[0-9]*.[0-9]*' 2>/dev/null || true)

    if [ -z "${BASE_VERSION}" ]; then
        echo "ERROR: Could not detect base kernel version using git describe"
        echo "Make sure the kernel branch has proper version tags"
        exit 1
    fi

    # Extract just the version tag using grep
    # This handles formats like: v6.18, v6.18.4, v6.18.4-123-gabcdef
    BASE_VERSION_TAG=$(echo "${BASE_VERSION}" | grep -oE 'v[0-9]+\.[0-9]+(\.[0-9]+)?' | head -1)

    if [ -z "${BASE_VERSION_TAG}" ]; then
        echo "ERROR: Could not extract version tag from: ${BASE_VERSION}"
        exit 1
    fi

    echo "Detected base version: ${BASE_VERSION}"
fi

# Extract kernel major.minor version from base tag (e.g., v6.18.4 → 6.18, v6.18 → 6.18)
KERNEL_VERSION=$(echo "${BASE_VERSION_TAG}" | sed -E 's/^v([0-9]+\.[0-9]+)(\.[0-9]+)?$/\1/')

if [ -z "${KERNEL_VERSION}" ]; then
    echo "ERROR: Could not extract kernel version from tag: ${BASE_VERSION_TAG}"
    exit 1
fi

echo "Base version tag: ${BASE_VERSION_TAG}"
echo "Kernel version: ${KERNEL_VERSION}"

# Set patches directory based on detected version
PATCHES_DIR="${REPO_ROOT}/patches/${KERNEL_VERSION}"
echo "Patches directory: ${PATCHES_DIR}"
echo ""

# ============================================================================
# Install git-format-patchsets tool
# ============================================================================

echo "[4/6] Installing git-format-patchsets..."

cd "${WORK_DIR}"
git clone --depth=1 "${GIT_TOOLS_REPO}" git-tools

# git-format-patchsets is a bash script, make it executable and add to PATH
chmod +x git-tools/git-format-patchsets
export PATH="${WORK_DIR}/git-tools:${PATH}"

# Verify tool is available
if ! command -v git-format-patchsets >/dev/null 2>&1; then
    echo "ERROR: git-format-patchsets not found in PATH"
    exit 1
fi

echo "Successfully installed git-format-patchsets"

# ============================================================================
# Generate patches
# ============================================================================

echo "[5/6] Generating patches..."

cd "${WORK_DIR}/kernel"

# Generate patches using git-format-patchsets
# Flags:
#   -t: Extract patchset assignments from "Patchset:" tags
#   --no-confirm: Don't ask for confirmation (non-interactive)
# Note: Patches are generated in the current directory (kernel dir)
PATCH_RANGE="${BASE_VERSION_TAG}"

echo "Generating patches from range: ${PATCH_RANGE}"

# Run git-format-patchsets with proper error handling
if ! git-format-patchsets -t --no-confirm "${PATCH_RANGE}"; then
    echo "ERROR: git-format-patchsets failed"
    exit 1
fi

# Count generated patches in current directory
PATCH_COUNT=$(find . -maxdepth 1 -name "*.patch" 2>/dev/null | wc -l)
echo "Generated ${PATCH_COUNT} patch files"

if [ "${PATCH_COUNT}" -eq 0 ]; then
    echo "WARNING: No patches were generated"
    if [ "${FORCE_UPDATE:-false}" != "true" ]; then
        echo "Skipping update (use FORCE_UPDATE=true to force)"
        echo "has_changes=false" >> "${GITHUB_OUTPUT:-/dev/null}"
        exit 0
    fi
fi

# ============================================================================
# Update patches directory
# ============================================================================

echo "[6/6] Updating patches directory..."

# Create/clear the patches directory
mkdir -p "${PATCHES_DIR}"
rm -f "${PATCHES_DIR}"/*.patch

# Copy generated patches from kernel directory
if [ "${PATCH_COUNT}" -gt 0 ]; then
    mv "${WORK_DIR}/kernel"/*.patch "${PATCHES_DIR}/"
    echo "Copied ${PATCH_COUNT} patches to ${PATCHES_DIR}"
fi

# ============================================================================
# Verify and report
# ============================================================================

echo ""
echo "============================================"
echo "Patch generation complete!"
echo "============================================"
echo "Branch: ${KERNEL_BRANCH}"
echo "Base version: ${BASE_VERSION_TAG}"
echo "Kernel version: ${KERNEL_VERSION}"
echo "Patches generated: ${PATCH_COUNT}"
echo "Output directory: ${PATCHES_DIR}"
echo ""

# List generated patches
if [ "${PATCH_COUNT}" -gt 0 ]; then
    echo "Generated patches:"
    ls -1 "${PATCHES_DIR}"/*.patch | sort
    echo ""
fi

# Set output for GitHub Actions
if [ -n "${GITHUB_OUTPUT:-}" ]; then
    echo "has_changes=true" >> "${GITHUB_OUTPUT}"
    echo "patch_count=${PATCH_COUNT}" >> "${GITHUB_OUTPUT}"
    echo "base_version=${BASE_VERSION_TAG}" >> "${GITHUB_OUTPUT}"
    echo "kernel_version=${KERNEL_VERSION}" >> "${GITHUB_OUTPUT}"
fi

# ============================================================================
# Cleanup
# ============================================================================

echo "Cleaning up temporary files..."
cd "${REPO_ROOT}"
rm -rf "${WORK_DIR}"

echo "Done!"
