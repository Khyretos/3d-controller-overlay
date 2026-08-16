#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_ROOT="${REPO_ROOT}/packaging/macos-deps"
DOCKERFILE="${REPO_ROOT}/packaging/docker/Dockerfile.macos-deps"

echo "============================================================"
echo "Building macOS dependencies"
echo "============================================================"
echo "Repository: ${REPO_ROOT}"
echo "Dependency root: ${DEPS_ROOT}"
echo

if [[ ! -f "${DOCKERFILE}" ]]; then
    echo "ERROR: Dockerfile not found:"
    echo "  ${DOCKERFILE}"
    exit 1
fi

mkdir -p "${DEPS_ROOT}"

build_arch() {
    local arch="$1"
    local image="3dco-macos-deps-${arch}"
    local container="3dco-macos-deps-${arch}-extract"
    local output_dir="${DEPS_ROOT}/${arch}"

    echo
    echo "============================================================"
    echo "Building ${arch} dependencies"
    echo "============================================================"
    echo

    rm -rf "${output_dir}"
    mkdir -p "${output_dir}"

    # Build a normal Docker image.
    # The dependency files remain inside /out in the image.
    docker build \
        --progress=plain \
        --build-arg "ARCH=${arch}" \
        -t "${image}" \
        -f "${DOCKERFILE}" \
        "${REPO_ROOT}"

    # Remove an old extraction container if one exists.
    docker rm -f "${container}" >/dev/null 2>&1 || true

    # Create a stopped container from the completed image.
    docker create \
        --name "${container}" \
        "${image}" \
        >/dev/null

    echo
    echo "Copying ${arch} dependency files from Docker image..."

    # Copy the already-built /out contents to the host.
    docker cp \
        "${container}:/out/." \
        "${output_dir}/"

    # Clean up the temporary container.
    docker rm "${container}" >/dev/null

    echo
    echo "Finished ${arch}"
    echo "Output: ${output_dir}"

    echo
    echo "Output size:"
    du -sh "${output_dir}"

    echo
    echo "Libraries:"
    find "${output_dir}/lib" \
        -maxdepth 1 \
        -type f \
        -name '*.a' \
        -printf '  %f\n' \
        2>/dev/null || true
}

build_arch x86_64
build_arch arm64

echo
echo "============================================================"
echo "macOS dependencies complete"
echo "============================================================"
echo
echo "Created:"
echo "  ${DEPS_ROOT}/x86_64"
echo "  ${DEPS_ROOT}/arm64"
echo
echo "Now run:"
echo "  ./scripts/build-macos.sh"