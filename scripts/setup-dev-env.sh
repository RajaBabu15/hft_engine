#!/bin/bash

# HFT Engine Development Environment Setup
# Sets up the development environment and installs dependencies
# Author: Raja Babu
# Date: 2025-07-10

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

log_info() {
    echo -e "${BLUE}[SETUP]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SETUP]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[SETUP]${NC} $1"
}

log_error() {
    echo -e "${RED}[SETUP]${NC} $1"
}

print_banner() {
    echo "=================================================================="
    echo "               HFT ENGINE DEVELOPMENT SETUP                      "
    echo "=================================================================="
    echo "Setting up development environment..."
    echo "=================================================================="
}

detect_os() {
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        echo "linux"
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        echo "macos"
    elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
        echo "windows"
    else
        echo "unknown"
    fi
}

install_dependencies_ubuntu() {
    log_info "Installing dependencies for Ubuntu/Debian..."
    
    sudo apt update
    sudo apt install -y \
        build-essential \
        cmake \
        git \
        libssl-dev \
        libboost-all-dev \
        nlohmann-json3-dev \
        pkg-config \
        lcov \
        gcovr \
        valgrind \
        cppcheck \
        clang-format
    
    log_success "Ubuntu dependencies installed"
}

install_dependencies_macos() {
    log_info "Installing dependencies for macOS..."
    
    if ! command -v brew &> /dev/null; then
        log_error "Homebrew not found. Please install Homebrew first:"
        log_info "https://brew.sh/"
        exit 1
    fi
    
    brew install \
        cmake \
        boost \
        openssl \
        nlohmann-json \
        lcov \
        cppcheck \
        clang-format
    
    log_success "macOS dependencies installed"
}

setup_git_hooks() {
    log_info "Setting up Git hooks..."
    
    # Create .git/hooks directory if it doesn't exist
    mkdir -p "$PROJECT_ROOT/.git/hooks"
    
    # Copy pre-commit hook
    if [ -f "$SCRIPT_DIR/pre-commit" ]; then
        cp "$SCRIPT_DIR/pre-commit" "$PROJECT_ROOT/.git/hooks/pre-commit"
        chmod +x "$PROJECT_ROOT/.git/hooks/pre-commit"
        log_success "Pre-commit hook installed"
    else
        log_warning "Pre-commit hook script not found"
    fi
    
    # Make build script executable
    if [ -f "$SCRIPT_DIR/hft-build" ]; then
        chmod +x "$SCRIPT_DIR/hft-build"
        log_success "hft-build script made executable"
    fi
}

setup_external_dependencies() {
    log_info "Setting up external dependencies..."
    
    # Create external directory
    mkdir -p "$PROJECT_ROOT/external"
    
    # Clone WebSocket++ if not already present
    if [ ! -d "$PROJECT_ROOT/external/websocketpp" ]; then
        log_info "Cloning WebSocket++ library..."
        git clone https://github.com/zaphoyd/websocketpp.git "$PROJECT_ROOT/external/websocketpp"
        log_success "WebSocket++ cloned successfully"
    else
        log_info "WebSocket++ already exists, updating..."
        cd "$PROJECT_ROOT/external/websocketpp"
        git pull origin develop
        cd "$PROJECT_ROOT"
    fi
}

setup_vscode_config() {
    log_info "Setting up VS Code configuration..."
    
    mkdir -p "$PROJECT_ROOT/.vscode"
    
    # Create settings.json
    cat > "$PROJECT_ROOT/.vscode/settings.json" << 'EOF'
{
    "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools",
    "C_Cpp.default.cppStandard": "c++17",
    "C_Cpp.default.compilerPath": "/usr/bin/g++",
    "cmake.buildDirectory": "${workspaceFolder}/build",
    "cmake.generator": "Unix Makefiles",
    "files.associations": {
        "*.h": "cpp",
        "*.hpp": "cpp",
        "*.cpp": "cpp"
    },
    "editor.formatOnSave": true,
    "C_Cpp.clang_format_style": "Google"
}
EOF
    
    # Create launch.json for debugging
    cat > "$PROJECT_ROOT/.vscode/launch.json" << 'EOF'
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug HFT Engine",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/hft_engine",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}/build",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ],
            "preLaunchTask": "Build HFT Engine"
        },
        {
            "name": "Debug Tests",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/hft_tests",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}/build",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ],
            "preLaunchTask": "Build HFT Engine"
        }
    ]
}
EOF
    
    # Create tasks.json
    cat > "$PROJECT_ROOT/.vscode/tasks.json" << 'EOF'
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Build HFT Engine",
            "type": "shell",
            "command": "${workspaceFolder}/scripts/hft-build",
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "presentation": {
                "echo": true,
                "reveal": "always",
                "focus": false,
                "panel": "shared"
            },
            "problemMatcher": "$gcc"
        },
        {
            "label": "Build with Coverage",
            "type": "shell",
            "command": "${workspaceFolder}/scripts/hft-build",
            "args": ["--coverage"],
            "group": "build",
            "presentation": {
                "echo": true,
                "reveal": "always",
                "focus": false,
                "panel": "shared"
            }
        },
        {
            "label": "Clean Build",
            "type": "shell",
            "command": "${workspaceFolder}/scripts/hft-build",
            "args": ["--clean"],
            "group": "build"
        },
        {
            "label": "Run Tests",
            "type": "shell",
            "command": "${workspaceFolder}/scripts/hft-build",
            "args": ["--test-only"],
            "group": "test"
        }
    ]
}
EOF
    
    log_success "VS Code configuration created"
}

create_sample_config() {
    log_info "Creating sample configuration files..."
    
    # Create sample auth config (without real credentials)
    cat > "$PROJECT_ROOT/auth_config.sample.json" << 'EOF'
{
    "api_key": "your_binance_api_key_here",
    "secret_key": "your_binance_secret_key_here",
    "passphrase": ""
}
EOF
    
    log_success "Sample configuration files created"
    log_warning "Remember to copy auth_config.sample.json to auth_config.json and add real credentials"
}

verify_installation() {
    log_info "Verifying installation..."
    
    local errors=0
    
    # Check required tools
    local tools=("cmake" "make" "g++" "git")
    for tool in "${tools[@]}"; do
        if command -v "$tool" &> /dev/null; then
            log_success "$tool found"
        else
            log_error "$tool not found"
            errors=$((errors + 1))
        fi
    done
    
    # Check libraries
    if pkg-config --exists openssl; then
        log_success "OpenSSL found"
    else
        log_error "OpenSSL not found"
        errors=$((errors + 1))
    fi
    
    if [ $errors -eq 0 ]; then
        log_success "All dependencies verified"
        return 0
    else
        log_error "$errors dependencies missing"
        return 1
    fi
}

main() {
    print_banner
    
    local os=$(detect_os)
    log_info "Detected OS: $os"
    
    case $os in
        "linux")
            install_dependencies_ubuntu
            ;;
        "macos")
            install_dependencies_macos
            ;;
        "windows")
            log_warning "Windows detected. Please use WSL or install dependencies manually"
            ;;
        *)
            log_error "Unsupported OS: $os"
            exit 1
            ;;
    esac
    
    setup_external_dependencies
    setup_git_hooks
    setup_vscode_config
    create_sample_config
    
    if verify_installation; then
        log_success "Development environment setup completed!"
        echo ""
        echo "=================================================================="
        echo "                          NEXT STEPS                             "
        echo "=================================================================="
        echo "1. Copy auth_config.sample.json to auth_config.json"
        echo "2. Add your Binance API credentials to auth_config.json"
        echo "3. Run './scripts/hft-build' to build the project"
        echo "4. Run './scripts/hft-build --coverage' to run with coverage"
        echo "=================================================================="
    else
        log_error "Setup completed with errors. Please fix the issues above."
        exit 1
    fi
}

main "$@"
