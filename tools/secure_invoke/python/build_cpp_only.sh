#!/bin/bash
"""
Standalone build script for secure_invoke C++ library only.

Use this when you want to build just the library without the Python package.
For Python package building, use: python -m build or pip install .
"""

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}SecureInvoke C++ Library Build Script${NC}"
echo "====================================="

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"

echo "Script directory: ${SCRIPT_DIR}"
echo "Project root: ${PROJECT_ROOT}"

# Function to print status
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Build C++ library using Bazel
print_status "Building C++ library with Bazel..."
cd "${PROJECT_ROOT}"

if [ ! -f "./builders/tools/bazel-debian" ]; then
    print_error "Bazel tool not found. Make sure you're in the correct project directory."
    exit 1
fi

# Build the shared library
print_status "Running Bazel build..."
./builders/tools/bazel-debian build //tools/secure_invoke:libsecure_invoke.so

if [ $? -ne 0 ]; then
    print_error "Bazel build failed"
    exit 1
fi

# Copy built library to tools directory for testing
print_status "Copying built library for local testing..."
SRC_LIB="${PROJECT_ROOT}/bazel-bin/tools/secure_invoke/libsecure_invoke.so"
DST_LIB="${PROJECT_ROOT}/tools/secure_invoke/libsecure_invoke.so"

if [ -f "${SRC_LIB}" ]; then
    cp "${SRC_LIB}" "${DST_LIB}"
    print_status "Library copied for testing: ${DST_LIB}"
else
    print_error "Built library not found: ${SRC_LIB}"
    exit 1
fi

print_status "C++ library build completed successfully!"
echo ""
print_status "Library location: ${DST_LIB}"
print_status "To build Python package: cd ${SCRIPT_DIR} && python -m build"
print_status "To install Python package: cd ${SCRIPT_DIR} && pip install ."
