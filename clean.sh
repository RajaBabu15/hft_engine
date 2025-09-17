#!/bin/bash

# ============================================================================
# HFT Engine Clean Script v2.0.0
# ============================================================================

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo -e "${BLUE}🧹 HFT Trading Engine Clean Script${NC}"
echo -e "${BLUE}===================================${NC}"
echo "Project Directory: $PROJECT_DIR"
echo ""

# Function to print status messages
print_status() {
    echo -e "${GREEN}[CLEAN]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to show usage
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --build            Clean build directory only"
    echo "  --python           Clean Python build artifacts only"
    echo "  --cache            Clean cache files only"
    echo "  --all              Clean everything (default)"
    echo "  --dry-run          Show what would be cleaned without actually cleaning"
    echo "  -h, --help         Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0                 # Clean everything"
    echo "  $0 --build         # Clean only build directory"
    echo "  $0 --python        # Clean only Python artifacts"
    echo "  $0 --dry-run       # Show what would be cleaned"
}

# Function to clean build directory
clean_build() {
    print_status "Cleaning build directory..."

    if [[ -d "build" ]]; then
        if [[ "$DRY_RUN" == "true" ]]; then
            echo "  Would remove: build/"
            find build -type f | head -10 | sed 's/^/    /'
            if [[ $(find build -type f | wc -l) -gt 10 ]]; then
                echo "    ... and $(( $(find build -type f | wc -l) - 10 )) more files"
            fi
        else
            rm -rf build/
            echo "  ✅ Removed build directory"
        fi
    else
        echo "  ℹ️  Build directory doesn't exist"
    fi
}

# Function to clean Python artifacts
clean_python() {
    print_status "Cleaning Python build artifacts..."

    # Python bytecode
    if find . -name "*.pyc" -o -name "*.pyo" | grep -q .; then
        if [[ "$DRY_RUN" == "true" ]]; then
            echo "  Would remove Python bytecode files:"
            find . -name "*.pyc" -o -name "*.pyo" | head -5 | sed 's/^/    /'
        else
            find . -name "*.pyc" -delete
            find . -name "*.pyo" -delete
            echo "  ✅ Removed Python bytecode files"
        fi
    fi

    # __pycache__ directories
    if find . -name "__pycache__" -type d | grep -q .; then
        if [[ "$DRY_RUN" == "true" ]]; then
            echo "  Would remove __pycache__ directories:"
            find . -name "__pycache__" -type d | sed 's/^/    /'
        else
            find . -name "__pycache__" -type d -exec rm -rf {} + 2>/dev/null || true
            echo "  ✅ Removed __pycache__ directories"
        fi
    fi

    # Python build directories
    local python_build_dirs=("build" "dist" "*.egg-info" ".eggs")
    for dir in "${python_build_dirs[@]}"; do
        if ls $dir 2>/dev/null | grep -q .; then
            if [[ "$DRY_RUN" == "true" ]]; then
                echo "  Would remove: $dir"
            else
                rm -rf $dir
                echo "  ✅ Removed $dir"
            fi
        fi
    done

    # Python wheel files
    if ls *.whl 2>/dev/null | grep -q .; then
        if [[ "$DRY_RUN" == "true" ]]; then
            echo "  Would remove wheel files:"
            ls *.whl | sed 's/^/    /'
        else
            rm -f *.whl
            echo "  ✅ Removed wheel files"
        fi
    fi
}

# Function to clean cache files
clean_cache() {
    print_status "Cleaning cache files..."

    # Data cache directory
    if [[ -d "data" ]]; then
        if [[ "$DRY_RUN" == "true" ]]; then
            echo "  Would remove: data/"
            find data -type f 2>/dev/null | head -5 | sed 's/^/    /' || true
        else
            rm -rf data/
            echo "  ✅ Removed data cache directory"
        fi
    fi

    # Log files
    local log_patterns=("*.log" "*.log.*" "*.out" "*.err")
    for pattern in "${log_patterns[@]}"; do
        if ls $pattern 2>/dev/null | grep -q .; then
            if [[ "$DRY_RUN" == "true" ]]; then
                echo "  Would remove log files: $pattern"
                ls $pattern 2>/dev/null | sed 's/^/    /' || true
            else
                rm -f $pattern 2>/dev/null || true
                echo "  ✅ Removed log files: $pattern"
            fi
        fi
    done

    # Temporary files
    local temp_patterns=("*.tmp" "*.temp" "*~" ".*.swp" ".*.swo")
    for pattern in "${temp_patterns[@]}"; do
        if ls $pattern 2>/dev/null | grep -q .; then
            if [[ "$DRY_RUN" == "true" ]]; then
                echo "  Would remove temp files: $pattern"
            else
                rm -f $pattern 2>/dev/null || true
                echo "  ✅ Removed temp files: $pattern"
            fi
        fi
    done

    # macOS specific files
    if [[ "$(uname -s)" == "Darwin" ]]; then
        if find . -name ".DS_Store" | grep -q .; then
            if [[ "$DRY_RUN" == "true" ]]; then
                echo "  Would remove .DS_Store files:"
                find . -name ".DS_Store" | sed 's/^/    /'
            else
                find . -name ".DS_Store" -delete
                echo "  ✅ Removed .DS_Store files"
            fi
        fi
    fi

    # Core dumps
    if ls core.* 2>/dev/null | grep -q .; then
        if [[ "$DRY_RUN" == "true" ]]; then
            echo "  Would remove core dumps:"
            ls core.* | sed 's/^/    /'
        else
            rm -f core.*
            echo "  ✅ Removed core dumps"
        fi
    fi
}

# Function to clean IDE/editor files
clean_ide() {
    print_status "Cleaning IDE/editor files..."

    # Visual Studio Code
    if [[ -d ".vscode" ]]; then
        if [[ "$DRY_RUN" == "true" ]]; then
            echo "  Would remove: .vscode/ (keeping in dry-run mode)"
        else
            # Don't actually remove .vscode as it might contain user settings
            print_warning "Skipping .vscode/ directory (may contain user settings)"
        fi
    fi

    # CLion/IntelliJ
    local ide_dirs=(".idea" "cmake-build-debug" "cmake-build-release")
    for dir in "${ide_dirs[@]}"; do
        if [[ -d "$dir" ]]; then
            if [[ "$DRY_RUN" == "true" ]]; then
                echo "  Would remove: $dir/"
            else
                rm -rf "$dir"
                echo "  ✅ Removed $dir/"
            fi
        fi
    done

    # Vim files
    if find . -name "*.swp" -o -name "*.swo" -o -name "*~" | grep -q .; then
        if [[ "$DRY_RUN" == "true" ]]; then
            echo "  Would remove Vim temporary files"
        else
            find . -name "*.swp" -delete 2>/dev/null || true
            find . -name "*.swo" -delete 2>/dev/null || true
            find . -name "*~" -delete 2>/dev/null || true
            echo "  ✅ Removed Vim temporary files"
        fi
    fi
}

# Function to calculate space that would be freed
calculate_space() {
    if command -v du &> /dev/null; then
        print_status "Calculating space usage..."

        local total_size=0

        if [[ -d "build" ]]; then
            local build_size=$(du -sh build 2>/dev/null | cut -f1)
            echo "  Build directory: $build_size"
        fi

        local python_size=$(find . -name "*.pyc" -o -name "*.pyo" -o -name "__pycache__" -type d | xargs du -ch 2>/dev/null | tail -n1 | cut -f1 2>/dev/null || echo "0")
        if [[ "$python_size" != "0" ]]; then
            echo "  Python artifacts: $python_size"
        fi

        local log_size=$(find . -name "*.log" -o -name "*.log.*" | xargs du -ch 2>/dev/null | tail -n1 | cut -f1 2>/dev/null || echo "0")
        if [[ "$log_size" != "0" ]]; then
            echo "  Log files: $log_size"
        fi
    fi
}

# Parse command line arguments
CLEAN_BUILD=false
CLEAN_PYTHON=false
CLEAN_CACHE=false
CLEAN_ALL=true
DRY_RUN=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --build)
            CLEAN_BUILD=true
            CLEAN_ALL=false
            shift
            ;;
        --python)
            CLEAN_PYTHON=true
            CLEAN_ALL=false
            shift
            ;;
        --cache)
            CLEAN_CACHE=true
            CLEAN_ALL=false
            shift
            ;;
        --all)
            CLEAN_ALL=true
            shift
            ;;
        --dry-run)
            DRY_RUN=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Main cleaning process
main() {
    cd "$PROJECT_DIR"

    if [[ "$DRY_RUN" == "true" ]]; then
        print_warning "DRY RUN MODE - No files will be actually deleted"
        echo ""
    fi

    # Calculate space before cleaning
    if [[ "$DRY_RUN" == "false" ]]; then
        calculate_space
        echo ""
    fi

    # Perform cleaning based on options
    if [[ "$CLEAN_ALL" == "true" ]]; then
        clean_build
        clean_python
        clean_cache
        clean_ide
    else
        if [[ "$CLEAN_BUILD" == "true" ]]; then
            clean_build
        fi

        if [[ "$CLEAN_PYTHON" == "true" ]]; then
            clean_python
        fi

        if [[ "$CLEAN_CACHE" == "true" ]]; then
            clean_cache
        fi
    fi

    echo ""
    if [[ "$DRY_RUN" == "true" ]]; then
        print_status "Dry run completed. No files were actually deleted."
    else
        print_status "Cleaning completed successfully!"
    fi

    echo ""
    echo -e "${GREEN}🎉 Clean operation finished!${NC}"
}

# Run main function
main "$@"
