#!/bin/bash
set -e
cd "$(dirname "$0")/.."   # move to repo root

echo "🔨 Building Linux AppImage (statically linked)..."
docker build -t 3dco-appimage-builder -f packaging/docker/Dockerfile.appimage .
mkdir -p dist
docker run --rm -v $(pwd)/dist:/dist 3dco-appimage-builder
echo "✅ AppImage created in ./dist/3dco.AppImage"
