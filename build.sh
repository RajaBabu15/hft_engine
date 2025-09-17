#!/bin/bash
# HFT Trading Engine - Comprehensive Build Script
# This script builds the entire HFT trading system including C++ engine and Python bindings

set -e  # Exit on any error

echo "🚀 HFT Trading Engine - Comprehensive Build Pipeline 🚀"
echo "==========================================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check system requirements
print_status "Checking system requirements..."

# Check for cmake
if ! command -v cmake &> /dev/null; then
    print_error "CMake is required but not installed. Please install CMake 3.16+."
    exit 1
fi

CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
print_success "CMake found: $CMAKE_VERSION"

# Check for Redis (optional but recommended)
if command -v redis-server &> /dev/null; then
    print_success "Redis server found"

    # Check if Redis is running
    if pgrep redis-server > /dev/null; then
        print_success "Redis server is already running"
    else
        print_warning "Redis server not running. Starting Redis in background..."
        redis-server --daemonize yes --port 6379 2>/dev/null || print_warning "Failed to start Redis"
    fi
else
    print_warning "Redis server not found. Some performance features may be disabled."
fi

# Check for Python (for bindings)
if command -v python3 &> /dev/null; then
    PYTHON_VERSION=$(python3 --version | cut -d' ' -f2)
    print_success "Python found: $PYTHON_VERSION"
else
    print_warning "Python3 not found. Python bindings will be skipped."
fi

# Build configuration
BUILD_TYPE="${1:-Release}"
BUILD_DIR="build"
PARALLEL_JOBS=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo "4")

print_status "Build configuration:"
echo "  - Build Type: $BUILD_TYPE"
echo "  - Build Directory: $BUILD_DIR"
echo "  - Parallel Jobs: $PARALLEL_JOBS"
echo ""

# Clean previous build if requested
if [[ "$2" == "clean" ]] || [[ "$1" == "clean" ]]; then
    print_status "Cleaning previous build..."
    rm -rf $BUILD_DIR
    rm -rf dist/
    rm -rf *.egg-info/
    print_success "Clean completed"
fi

# Create build directory
print_status "Setting up build directory..."
mkdir -p $BUILD_DIR

# Configure with CMake
print_status "Configuring with CMake..."
cd $BUILD_DIR

if ! cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
           -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
           ..; then
    print_error "CMake configuration failed"
    exit 1
fi

print_success "CMake configuration completed"

# Build the project
print_status "Building HFT Engine (using $PARALLEL_JOBS cores)..."
if ! make -j$PARALLEL_JOBS; then
    print_error "Build failed"
    exit 1
fi

print_success "C++ build completed successfully"

# Check if binaries were created
if [[ -f "hft_engine" ]]; then
    print_success "Main engine binary: hft_engine"
else
    print_error "Main engine binary not found"
    exit 1
fi

if [[ -f "benchmark" ]]; then
    print_success "Benchmark binary: benchmark"
else
    print_warning "Benchmark binary not found"
fi

cd ..

# Build Python bindings if Python is available
if command -v python3 &> /dev/null; then
    print_status "Building Python bindings..."

    # Check if pybind11 is available
    if python3 -c "import pybind11" 2>/dev/null; then
        print_success "pybind11 found"

        # Build wheel
        if python3 -m build --wheel 2>/dev/null; then
            print_success "Python wheel built successfully"
        else
            print_warning "Wheel build failed, trying pip install -e ."
            if pip3 install -e . 2>/dev/null; then
                print_success "Development install completed"
            else
                print_warning "Python bindings build failed"
            fi
        fi
    else
        print_warning "pybind11 not found. Installing..."
        pip3 install pybind11 2>/dev/null || print_warning "Failed to install pybind11"
    fi
fi

# Verify build artifacts
print_status "Verifying build artifacts..."

ARTIFACTS_FOUND=0

if [[ -f "$BUILD_DIR/hft_engine" ]]; then
    print_success "✓ Main HFT Engine binary"
    ARTIFACTS_FOUND=$((ARTIFACTS_FOUND + 1))
fi

if [[ -f "$BUILD_DIR/benchmark" ]]; then
    print_success "✓ Performance benchmark binary"
    ARTIFACTS_FOUND=$((ARTIFACTS_FOUND + 1))
fi

if [[ -d "dist" ]] && ls dist/*.whl 2>/dev/null; then
    print_success "✓ Python wheel packages"
    ARTIFACTS_FOUND=$((ARTIFACTS_FOUND + 1))
fi

# Check for Python module installation
if python3 -c "import hft_engine_cpp" 2>/dev/null; then
    print_success "✓ Python module installed and importable"
    ARTIFACTS_FOUND=$((ARTIFACTS_FOUND + 1))
fi

echo ""
print_status "Build Summary:"
echo "  - Total artifacts: $ARTIFACTS_FOUND"
echo "  - Main binary: $BUILD_DIR/hft_engine"
echo "  - Benchmark binary: $BUILD_DIR/benchmark"
echo "  - Build type: $BUILD_TYPE"

if [[ $ARTIFACTS_FOUND -ge 2 ]]; then
    echo ""
    print_success "🎉 BUILD COMPLETED SUCCESSFULLY! 🎉"
    print_status "Ready to run:"
    echo "  ./run.sh                    # Run main engine"
    echo "  ./run.sh benchmark         # Run performance benchmark"
    echo "  ./run.sh demo              # Run demonstration"
    echo "  python3 real_data_demo.py  # Run Python demo with real data"
else
    print_warning "Build completed with warnings. Some components may not be available."
fi

echo "==========================================================="

#!/bin/bash

# ============================================================================
# HFT Engine Build Script v2.0.0
# ============================================================================

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
PROJECT_NAME="hft_engine"
BUILD_DIR="build"
CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
VERBOSE="${VERBOSE:-OFF}"

# System detection
SYSTEM=$(uname -s)
ARCH=$(uname -m)
NCPUS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo -e "${BLUE}🚀 HFT Trading Engine Build Script${NC}"
echo -e "${BLUE}=================================${NC}"
echo "System: $SYSTEM $ARCH"
echo "Build Type: $CMAKE_BUILD_TYPE"
echo "CPU Cores: $NCPUS"
echo ""

# Function to print status messages
print_status() {
    echo -e "${GREEN}[BUILD]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to check prerequisites
check_prerequisites() {
    print_status "Checking prerequisites..."

    # Check CMake
    if ! command -v cmake &> /dev/null; then
        print_error "CMake not found. Please install CMake 3.16 or higher."
        exit 1
    fi

    CMAKE_VERSION=$(cmake --version | head -n1 | cut -d' ' -f3)
    print_status "CMake version: $CMAKE_VERSION"

    # Check compiler
    if command -v clang++ &> /dev/null; then
        COMPILER="clang++"
        COMPILER_VERSION=$(clang++ --version | head -n1)
    elif command -v g++ &> /dev/null; then
        COMPILER="g++"
        COMPILER_VERSION=$(g++ --version | head -n1)
    else
        print_error "No suitable C++ compiler found. Please install clang++ or g++."
        exit 1
    fi

    print_status "Compiler: $COMPILER_VERSION"

    # Check Redis (optional for full functionality)
    if command -v redis-server &> /dev/null; then
        print_status "Redis server found: $(redis-server --version | head -n1)"
    else
        print_warning "Redis server not found. Some benchmarks may not work."
    fi

    # Check hiredis library
    if [[ "$SYSTEM" == "Darwin" ]]; then
        if [[ -f "/opt/homebrew/lib/libhiredis.dylib" ]]; then
            print_status "hiredis library found (Homebrew)"
        else
            print_warning "hiredis library not found. Install with: brew install hiredis"
        fi
    else
        # Linux checks
        if ldconfig -p | grep -q hiredis; then
            print_status "hiredis library found"
        else
            print_warning "hiredis library not found. Install with: apt-get install libhiredis-dev"
        fi
    fi
}

# Function to create build directory
setup_build_directory() {
    print_status "Setting up build directory..."

    if [[ -d "$BUILD_DIR" ]]; then
        print_status "Cleaning existing build directory..."
        rm -rf "$BUILD_DIR"
    fi

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
}

# Function to configure with CMake
configure_cmake() {
    print_status "Configuring with CMake..."

    CMAKE_ARGS=(
        -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE"
    )

    # Platform-specific optimizations
    if [[ "$CMAKE_BUILD_TYPE" == "Release" ]]; then
        if [[ "$ARCH" == "arm64" ]] || [[ "$ARCH" == "aarch64" ]]; then
            print_status "Applying ARM64 optimizations"
        elif [[ "$ARCH" == "x86_64" ]]; then
            print_status "Applying x86_64 optimizations"
        fi
    fi

    # Add verbose output if requested
    if [[ "$VERBOSE" == "ON" ]]; then
        CMAKE_ARGS+=(-DCMAKE_VERBOSE_MAKEFILE=ON)
    fi

    cmake "${CMAKE_ARGS[@]}" ..

    if [[ $? -ne 0 ]]; then
        print_error "CMake configuration failed!"
        exit 1
    fi
}

# Function to build the project
build_project() {
    print_status "Building project with $NCPUS cores..."

    MAKE_ARGS=(
        -j"$NCPUS"
    )

    if [[ "$VERBOSE" == "ON" ]]; then
        MAKE_ARGS+=(VERBOSE=1)
    fi

    make "${MAKE_ARGS[@]}"

    if [[ $? -ne 0 ]]; then
        print_error "Build failed!"
        exit 1
    fi
}

# Function to display build results
display_results() {
    print_status "Build completed successfully!"
    echo ""

    print_status "Built executables:"
    if [[ -f "$PROJECT_NAME" ]]; then
        echo "  ✅ $PROJECT_NAME (main engine)"
        ls -lh "$PROJECT_NAME"
    fi

    if [[ -f "benchmark" ]]; then
        echo "  ✅ benchmark (performance testing)"
        ls -lh benchmark
    fi

    echo ""
    print_status "Available targets:"
    echo "  make run           - Run the main HFT engine"
    echo "  make run_benchmark - Run performance benchmarks"
    echo ""

    print_status "Build artifacts location: $(pwd)"
}

# Function to verify build
verify_build() {
    print_status "Verifying build..."

    if [[ ! -f "$PROJECT_NAME" ]]; then
        print_error "Main executable not found!"
        return 1
    fi

    if [[ ! -x "$PROJECT_NAME" ]]; then
        print_error "Main executable is not executable!"
        return 1
    fi

    # Quick smoke test
    if ./"$PROJECT_NAME" --version &>/dev/null; then
        print_status "Executable verification passed"
    else
        print_warning "Executable may have issues (--version failed)"
    fi

    return 0
}

# Function to show usage
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -t, --type TYPE    Build type: Debug, Release, RelWithDebInfo (default: Release)"
    echo "  -v, --verbose      Enable verbose output"
    echo "  -c, --clean        Clean build (remove build directory first)"
    echo "  --verify           Run verification tests after build"
    echo "  -h, --help         Show this help message"
    echo ""
    echo "Environment variables:"
    echo "  CMAKE_BUILD_TYPE   Set build type (default: Release)"
    echo "  VERBOSE           Enable verbose output (default: OFF)"
    echo ""
    echo "Examples:"
    echo "  $0                 # Standard release build"
    echo "  $0 -t Debug        # Debug build"
    echo "  $0 -v             # Verbose build"
    echo "  $0 --clean        # Clean build"
}

# Parse command line arguments
VERIFY_BUILD=false
CLEAN_BUILD=false

while [[ $# -gt 0 ]]; do
    case $1 in
        -t|--type)
            CMAKE_BUILD_TYPE="$2"
            shift 2
            ;;
        -v|--verbose)
            VERBOSE="ON"
            shift
            ;;
        -c|--clean)
            CLEAN_BUILD=true
            shift
            ;;
        --verify)
            VERIFY_BUILD=true
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

# Main build process
main() {
    # Record start time
    START_TIME=$(date +%s)

    # Ensure we're in the project root
    cd "$(dirname "${BASH_SOURCE[0]}")"

    check_prerequisites

    # Clean build if requested
    if [[ "$CLEAN_BUILD" == true ]] && [[ -d "$BUILD_DIR" ]]; then
        print_status "Performing clean build..."
        rm -rf "$BUILD_DIR"
    fi

    setup_build_directory
    configure_cmake
    build_project

    if [[ "$VERIFY_BUILD" == true ]]; then
        verify_build
    fi

    display_results

    # Calculate build time
    END_TIME=$(date +%s)
    BUILD_TIME=$((END_TIME - START_TIME))

    echo ""
    print_status "Build completed in ${BUILD_TIME}s"
    echo ""
    echo -e "${GREEN}🎉 Build successful! Ready to run HFT engine.${NC}"
}

# Run main function
main "$@"
