#!/bin/bash
set -e
cd "$(dirname "$0")/.."   # move to repo root

echo "🔨 Building macOS Universal App..."
docker build -t 3dco+-macos-builder -f packaging/docker/Dockerfile.macos .
mkdir -p dist/macos
docker run --rm -v $(pwd)/dist:/dist 3dco+-macos-builder
echo "✅ macOS app bundle created in ./dist/macos/"
