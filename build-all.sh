#!/bin/bash
set -e

echo "🚀 Building all platforms..."
./build-appimage.sh
./build-windows.sh
./build-macos.sh
echo "✅ All builds complete!"