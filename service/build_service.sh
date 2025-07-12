#!/bin/bash

# Build script for Burwell Service Manager
# This builds independently of the main project

set -e

echo "Building Burwell Service Manager..."

# Clean previous build
rm -f burwell-service.exe

# Build with cross-compiler
x86_64-w64-mingw32-g++ \
    -std=c++17 \
    -O2 \
    -static \
    -DWIN32_LEAN_AND_MEAN \
    burwell_service_cli.cpp \
    service_manager.cpp \
    -o burwell-service.exe \
    -ladvapi32

if [ $? -eq 0 ]; then
    echo "✅ Service manager built successfully!"
    echo "📁 Output: burwell-service.exe"
    
    # Check dependencies
    echo "🔍 Dependencies:"
    x86_64-w64-mingw32-objdump -p burwell-service.exe | grep "DLL Name:" | sort | uniq
    
    # Check file size
    size=$(stat -c%s burwell-service.exe)
    echo "📏 Size: $((size / 1024)) KB"
else
    echo "❌ Build failed!"
    exit 1
fi