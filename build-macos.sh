#!/bin/bash
set -e

echo "🔨 Building macOS Universal App..."
docker build -t 3dco-macos-builder -f Dockerfile.macos .
mkdir -p dist/macos
docker run --rm -v $(pwd)/dist:/dist 3dco-macos-builder
echo "✅ macOS app bundle created in ./dist/macos/"