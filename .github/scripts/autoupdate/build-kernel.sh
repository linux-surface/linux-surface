#!/usr/bin/env bash
#
# Build kernel to validate rebase
#
# This script must be run from within a linux-surface/kernel git repository.
# It will:
# 1. Validate the kernel source directory and version
# 2. Collect and merge kernel configs (base + surface + overrides)
# 3. Build the kernel and modules
# 4. Output metadata for subsequent steps
#
# Environment variables:
#   KERNEL_VERSION: (required) Major.minor version (e.g., "6.18")
#   LINUX_SURFACE_DIR: (required) Path to linux-surface repository

set -euo pipefail

# Enable debug mode if DEBUG is set
if [ "${DEBUG:-}" = "1" ]; then
    set -x
fi

# ============================================================================
# Phase 1: Validation
# ============================================================================

echo "============================================"
echo "Kernel Build Validation Script"
echo "============================================"
echo ""

if [ -z "${KERNEL_VERSION:-}" ]; then
    echo "ERROR: KERNEL_VERSION environment variable is required"
    echo "Example: KERNEL_VERSION=6.18"
    exit 1
fi

if [ -z "${LINUX_SURFACE_DIR:-}" ]; then
    echo "ERROR: LINUX_SURFACE_DIR environment variable is required"
    echo "Example: LINUX_SURFACE_DIR=/path/to/linux-surface"
    exit 1
fi

echo "[1/5] Validating environment..."

# Verify we're in a git repository with kernel source
if ! git rev-parse --git-dir >/dev/null 2>&1; then
    echo "ERROR: Not in a git repository"
    echo "This script must be run from within the kernel repository"
    exit 1
fi

# Verify this looks like a kernel source tree
if [ ! -f "Makefile" ] || [ ! -d "kernel" ] || [ ! -d "drivers" ]; then
    echo "ERROR: Current directory doesn't appear to be a kernel source tree"
    echo "Expected files: Makefile, kernel/, drivers/"
    exit 1
fi

# Validate KERNEL_VERSION format
if ! echo "${KERNEL_VERSION}" | grep -qE '^[0-9]+\.[0-9]+$'; then
    echo "ERROR: Invalid KERNEL_VERSION format: ${KERNEL_VERSION}"
    echo "Expected format: X.Y (e.g., 6.18)"
    exit 1
fi

# Validate LINUX_SURFACE_DIR exists
if [ ! -d "${LINUX_SURFACE_DIR}" ]; then
    echo "ERROR: LINUX_SURFACE_DIR does not exist: ${LINUX_SURFACE_DIR}"
    exit 1
fi

echo "Kernel version: ${KERNEL_VERSION}"
echo "Linux-surface dir: ${LINUX_SURFACE_DIR}"
echo "Validation successful"

# ============================================================================
# Phase 2: Config Setup
# ============================================================================

echo "[2/5] Setting up kernel configs..."

# Define config paths
BASE_CONFIG="${LINUX_SURFACE_DIR}/.github/data/autoupdate/kernel-base.config"
SURFACE_CONFIG="${LINUX_SURFACE_DIR}/configs/surface-${KERNEL_VERSION}.config"
OVERRIDES_CONFIG="${LINUX_SURFACE_DIR}/.github/data/autoupdate/kernel-overrides.config"

# Verify all required configs exist
if [ ! -f "${BASE_CONFIG}" ]; then
    echo "ERROR: Base config not found: ${BASE_CONFIG}"
    exit 1
fi

if [ ! -f "${SURFACE_CONFIG}" ]; then
    echo "ERROR: Surface config not found for version ${KERNEL_VERSION}: ${SURFACE_CONFIG}"
    echo "Available surface configs:"
    ls -1 "${LINUX_SURFACE_DIR}/configs"/surface-*.config 2>/dev/null || echo "(none found)"
    exit 1
fi

if [ ! -f "${OVERRIDES_CONFIG}" ]; then
    echo "ERROR: Overrides config not found: ${OVERRIDES_CONFIG}"
    exit 1
fi

echo "Base config: ${BASE_CONFIG}"
echo "Surface config: ${SURFACE_CONFIG}"
echo "Overrides config: ${OVERRIDES_CONFIG}"

# Copy configs to kernel directory with simplified names
cp "${BASE_CONFIG}" base.config
cp "${SURFACE_CONFIG}" surface.config
cp "${OVERRIDES_CONFIG}" overrides.config

echo "Configs copied successfully"

# ============================================================================
# Phase 3: Config Merge (Three-Stage)
# ============================================================================

echo "[3/5] Merging kernel configs..."

# Merge configs using kernel's merge_config.sh script
# Order is critical: base → surface → overrides
if ! ./scripts/kconfig/merge_config.sh -m base.config surface.config overrides.config; then
    echo ""
    echo "ERROR: Config merge failed"
    echo "Check the output above for details"
    exit 1
fi

echo "Running olddefconfig to validate merged config..."

if ! make olddefconfig; then
    echo ""
    echo "ERROR: olddefconfig failed"
    echo "The merged config may have invalid options"
    exit 1
fi

echo "Config merge successful"
echo "Final config: $(pwd)/.config"

# ============================================================================
# Phase 4: Build Kernel
# ============================================================================

echo "[4/5] Building kernel..."

# Set KBUILD environment variables
export KBUILD_BUILD_HOST=github-actions
export KBUILD_BUILD_USER=linux-surface

# Determine number of parallel jobs
NPROC=$(nproc)
echo "Building with ${NPROC} parallel jobs..."

# Build kernel and modules
# Using 'all' target builds both vmlinux and modules
echo "Running: make -j${NPROC} all"

if ! make -j"${NPROC}" all; then
    echo ""
    echo "============================================"
    echo "BUILD FAILED"
    echo "============================================"
    echo ""
    echo "The kernel build failed. This indicates the rebase introduced"
    echo "compilation errors or config incompatibilities."
    echo ""
    echo "Check the build output above for error details."
    echo ""
    exit 1
fi

echo ""
echo "============================================"
echo "Build successful!"
echo "============================================"
echo ""

# ============================================================================
# Phase 5: Output Metadata
# ============================================================================

echo "[5/5] Setting outputs..."

# Set outputs for GitHub Actions
if [ -n "${GITHUB_OUTPUT:-}" ]; then
    echo "build_success=true" >> "${GITHUB_OUTPUT}"
    echo "config_path=$(pwd)/.config" >> "${GITHUB_OUTPUT}"
fi

echo "Build validation complete!"
echo "Config saved at: $(pwd)/.config"
echo ""

echo "Done!"
