#!/bin/bash

# Burwell Build Script for MSYS2 MinGW
# This script builds the project and sets up the runtime environment
# Requirements: Run this from MSYS2 MinGW 64-bit terminal with required packages installed

set -e  # Exit on any error

echo "[INFO] Burwell Build Script"
echo "=========================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Get script directory (project root)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo -e "${BLUE}🔵 Working directory: $(pwd)${NC}"

# Step 1: Clean and build
echo -e "\n${YELLOW}🔵 Cleaning build directory...${NC}"
if [ -d "build" ]; then
    cd build
    rm -rf *
    echo "✅ Build directory cleaned"
else
    mkdir -p build
    cd build
    echo "✅ Created build directory"
fi

# Step 2: Configure with CMake
echo -e "\n${YELLOW}🔵 Configuring with CMake...${NC}"
# Configure for Windows native build without MSYS2 runtime dependencies
# Detect build environment and choose appropriate generator
if command -v mingw32-make >/dev/null 2>&1; then
    # MSYS2/MinGW environment
    GENERATOR="MinGW Makefiles"
    MAKE_COMMAND="mingw32-make"
else
    # Unix environment with cross-compilation
    GENERATOR="Unix Makefiles"
    MAKE_COMMAND="make"
    export CC=x86_64-w64-mingw32-gcc
    export CXX=x86_64-w64-mingw32-g++
fi

cmake .. -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-D_WIN32_WINNT=0x0601" \
    -DCMAKE_EXE_LINKER_FLAGS="-static -Wl,--subsystem,console"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✅ CMake configuration successful${NC}"
else
    echo -e "${RED}❌ CMake configuration failed${NC}"
    exit 1
fi

# Step 3: Build the project
echo -e "\n${YELLOW}🔵 Building project...${NC}"
$MAKE_COMMAND -j4

if [ $? -eq 0 ]; then
    echo -e "${GREEN}✅ Build successful${NC}"
else
    echo -e "${RED}❌ Build failed${NC}"
    exit 1
fi

# Step 4: Create bin directory if it doesn't exist
echo -e "\n${YELLOW}🔵 Setting up runtime environment...${NC}"
mkdir -p bin
cd bin

# Step 5: Copy/link configuration folders and files
echo "🔵 Copying configuration files and directories..."

# Define source paths (relative to project root)
CONFIG_SOURCES=(
    "config"
    "tasks"
)

# Copy each configuration item
for item in "${CONFIG_SOURCES[@]}"; do
    SOURCE_PATH="../../${item}"
    
    if [ -e "$SOURCE_PATH" ]; then
        if [ -d "$SOURCE_PATH" ]; then
            echo "🔵 Copying directory: ${item}/"
            cp -r "$SOURCE_PATH" .
        else
            echo "📄 Copying file: ${item}"
            cp "$SOURCE_PATH" .
        fi
        echo -e "${GREEN}   ✅ ${item} copied successfully${NC}"
    else
        echo -e "${YELLOW}   ⚠️  ${item} not found, skipping${NC}"
    fi
done

# Step 6: Create logs directory
echo "🔵 Creating logs directory..."
mkdir -p logs
echo -e "${GREEN}   ✅ logs/ directory created${NC}"

# Step 7: Verify executable exists
if [ -f "burwell.exe" ]; then
    echo -e "\n${GREEN}✅ Build completed successfully!${NC}"
    echo -e "${BLUE}🔵 Executable location: $(pwd)/burwell.exe${NC}"
    
    # List all files in bin directory
    echo -e "\n${BLUE}🔵 Runtime environment contents:${NC}"
    ls -la
    
    echo -e "\n${YELLOW}🔵 Usage:${NC}"
    echo "   cd build/bin"
    echo "   ./burwell.exe"
    echo ""
    echo -e "${YELLOW}🔵 To modify configuration:${NC}"
    echo "   - Edit config/cpl/commands.json for command parameters"
    echo "   - Edit config/llm_providers/openrouter.json for OpenRouter settings"
    echo "   - Edit config/llm_providers/*.json for other LLM providers" 
    echo "   - Edit config/burwell.json for main configuration"
    echo ""
    echo -e "${YELLOW}🔵 API Key Setup:${NC}"
    echo "   Set OPENROUTER_API_KEY environment variable for OpenRouter"
    echo "   export OPENROUTER_API_KEY=your_api_key_here"
    echo ""
    
else
    echo -e "${RED}❌ Build failed - burwell.exe not found${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Ready to run Burwell!${NC}"