#!/usr/bin/env bash

# Build script for Zap compiler
# Creates build directory and compiles the project using Meson

set -e # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

if [[ "$1" == "--help" || "$1" == "-h" ]]; then
  echo "Usage: ./build.sh [args...]"
  echo ""
  echo "Builds the Zap compiler using Meson."
  echo "Any additional arguments are passed directly to 'meson compile'."
  echo "Examples:"
  echo "  ./build.sh          (Standard build)"
  echo "  ./build.sh --clean  (Clean the build directory)"
  echo "  ./build.sh zapc     (Build only the zapc target)"
  exit 0
fi

echo -e "${YELLOW}Building Zap compiler...${NC}"

# Configure the build directory if it hasn't been set up yet.
# Checking for build.ninja is safer than checking the directory,
# which prevents failures if an empty 'build' folder was created manually.
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
  echo -e "${YELLOW}Setting up build directory...${NC}"
  meson setup "$BUILD_DIR" "$SCRIPT_DIR" --buildtype=release
fi

# Build the project
echo -e "${YELLOW}Compiling...${NC}"
meson compile -C "$BUILD_DIR" "$@"

# Check if build was successful
if [ -f "$BUILD_DIR/zapc" ]; then
  echo -e "${GREEN}Build successful!${NC}"
  echo -e "${GREEN}Executable: $BUILD_DIR/zapc${NC}"
else
  echo -e "${RED}Build failed! (zapc executable not found)${NC}"
  exit 1
fi