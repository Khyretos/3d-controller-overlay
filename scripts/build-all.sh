#!/bin/bash
set -e
cd "$(dirname "$0")"

echo "🚀 Building all platforms..."
./build-appimage.sh
./build-windows.sh
./build-macos.sh
echo "✅ All builds complete!"
