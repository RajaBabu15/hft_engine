#!/bin/bash
set -e
rm -rf build/
dist/
*.egg-info/
find . -name "*.pyc" -delete
find . -name "*.pyo" -delete
find . -name "__pycache__" -type d -exec rm -rf {} + 2>/dev/null || true
rm -f *.whl
rm -rf data/
rm -f *.log
rm -f *.log.*
rm -f *.out
rm -f *.err
rm -f *.tmp
rm -f *.temp
rm -f *~
rm -f .*.swp
rm -f .*.swo
find . -name ".DS_Store" -delete
rm -f core.*
rm -rf .idea/
cmake-build-debug/
cmake-build-release/
find . -name "*.swp" -delete 2>/dev/null || true
find . -name "*.swo" -delete 2>/dev/null || true
find . -name "*~" -delete 2>/dev/null || true
