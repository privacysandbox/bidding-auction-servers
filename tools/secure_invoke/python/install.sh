#!/bin/bash
"""
Simple installation script for secure-invoke-crypto package.

This script just runs pip install, which triggers setup.py to build everything.
"""

set -e

echo "Installing secure-invoke-crypto package..."
echo "========================================"

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cd "${SCRIPT_DIR}"

# Check if we're in development mode
if [ "$1" = "--dev" ] || [ "$1" = "-e" ]; then
    echo "Installing in development mode..."
    pip install -e .
else
    echo "Installing package..."
    pip install .
fi

echo ""
echo "Installation complete!"
echo ""
echo "Test the installation:"
echo "  secure-invoke-demo"
echo ""
echo "Or run end-to-end test:"
echo "  secure-invoke-test --encrypt-only"
