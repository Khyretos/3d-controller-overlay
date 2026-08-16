#!/bin/bash
set -e
cd "$(dirname "$0")/.."   # move to repo root

echo "🔨 Building Windows executable..."
docker build -t 3dco-windows-builder -f packaging/docker/Dockerfile.windows .
mkdir -p dist/windows
docker run --rm -v $(pwd)/dist:/dist 3dco-windows-builder
echo "✅ Windows executable created in ./dist/windows/"
