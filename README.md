# HFT Engine: High-Frequency Trading Data Core

[![Build Status](https://img.shields.io/github/actions/workflow/status/RajaBabu15/hft_engine/build.yml?branch=main&style=for-the-badge)](https://github.com/RajaBabu15/hft_engine/actions)
[![Code Coverage](https://img.shields.io/codecov/c/github/RajaBabu15/hft_engine?style=for-the-badge&token=YOUR_CODECOV_TOKEN)](https://codecov.io/gh/RajaBabu15/hft_engine)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=for-the-badge)](https://opensource.org/licenses/MIT)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-17-blue.svg?style=for-the-badge)](https://isocpp.org/std/the-standard)

A professional-grade, ultra-low-latency C++ engine for receiving real-time cryptocurrency market data from Binance. Built for performance, it serves as the foundational core for algorithmic trading bots, market analysis tools, and backtesting infrastructure.

**⚠️ Important Note**: This is currently a **read-only** market data system. It does **not** execute trades but is architected for seamless extension into a full trading solution.

---

## 📖 Table of Contents

- [✨ Key Features](#-key-features)
- [🏗️ Architecture Overview](#️-architecture-overview)
- [🚀 Getting Started](#-getting-started)
- [🛠️ Unified Build System](#️-unified-build-system)
- [🌐 Web Interface](#-web-interface)
- [🧪 Testing & Code Quality](#-testing--code-quality)
- [🐳 Docker Support](#-docker-support)
- [🛣️ Future Extensions](#️-future-extensions)
- [🤝 Contributing](#-contributing)
- [📜 License](#-license)

---

## ✨ Key Features

- **Real-Time Data Stream:** Connects to Binance's WebSocket API for live, low-latency order book updates.
- **High-Performance Order Book:** Maintains an accurate, in-memory order book for the specified trading pair.
- **Ultra-Low Latency Design:**
    - **Asynchronous I/O:** Network operations are decoupled from data processing threads.
    - **Lock-Free Concurrency:** Utilizes `boost::lockfree::queue` for high-throughput, contention-free message passing.
    - **CPU Cache Optimization:** Employs `alignas(64)` on core data structures to prevent false sharing on multi-core systems.
- **Robust Build & Dev Environment:**
    - Unified build script (`hft-build`) for easy compilation, testing, and coverage.
    - Automated development environment setup (`setup-dev-env.sh`).
- **Comprehensive Testing:** High test coverage with GoogleTest for core components.
- **Extensible Architecture:** Designed with future trading capabilities in mind, including components for authentication, order management, and a TUI.
- **Containerized:** Includes a multi-stage `Dockerfile` for reproducible builds and easy deployment.

## 🏗️ Architecture Overview

The engine is built on a decoupled, multi-threaded architecture to ensure maximum performance.

![Data Flow Diagram](https://i.imgur.com/example-flow-chart.png)  <!-- Suggested: Create and add a simple data flow diagram -->

1.  **`WebsocketClient`**: Establishes a secure WebSocket connection and receives market data.
2.  **`MatchingEngine`**: The core processor. It fetches data from a lock-free queue and dispatches it to the appropriate handler.
3.  **`OrderBook`**: Manages the state of the bids and asks, providing a real-time view of market depth.
4.  **`UIManager`**: A text-based UI for interacting with the engine (future-ready).
5.  **`AuthManager` & `TradingClient`**: Placeholder components for handling authenticated API calls and trade execution in future versions.

| Component | File(s) | Purpose |
| :--- | :--- | :--- |
| **WebSocket Client** | `websocket_client.h/cpp` | Real-time connection to Binance API. |
| **Matching Engine** | `matching_engine.h/cpp` | High-performance message processor using a lock-free queue. |
| **Order Book** | `order_book.h/cpp` | Manages and displays market depth data. |
| **Trading Client**| `trading_client.h/cpp` | Manages trading functions (placing/canceling orders). *[Mocked]* |
| **Auth Manager** | `auth_manager.h/cpp` | Handles API key authentication and request signing. *[Future Use]* |
| **UI Manager** | `ui_manager.h/cpp` | Provides a menu-driven terminal user interface. *[Future Use]* |
| **Command System** | `command.h` | Defines cache-aligned internal communication structures. |
| **Configuration** | `config.json` | Sets the trading pair and order book depth. |

## 🚀 Getting Started

Follow these steps to get the engine up and running quickly on a Linux-based system (including WSL).

### Prerequisites

- A C++17 compatible compiler (GCC 11+ or Clang 14+)
- Git
- Docker (for containerized builds)

### Quick Start with Setup Script

Our setup script will install all necessary dependencies and configure the development environment.

1.  **Clone the Repository:**
    ```bash
    git clone https://github.com/RajaBabu15/hft_engine.git
    cd hft_engine
    ```

2.  **Setup Development Environment:**
    Use the unified build script to automatically install all dependencies:
    ```bash
    # Make script executable
    chmod +x ./scripts/hft-build
    
    # Setup development environment (installs cmake, boost, openssl, gtest, etc.)
    ./scripts/hft-build setup
    ```

3.  **Build and Test the Engine:**
    Use the unified build script to compile and run the tests.
    ```bash
    ./scripts/hft-build build
    ```

4.  **Run the Engine:**
    The executable will be located in the `build` directory.
    ```bash
    ./build/hft_engine
    ```

You should see the live order book printed to your terminal.

### 🪟 Windows WSL Setup

For Windows users, we recommend using WSL (Windows Subsystem for Linux) for the best development experience:

1.  **Enable WSL:**
    ```powershell
    # Run in PowerShell as Administrator
    wsl --install
    ```

2.  **Access Your Project in WSL:**
    ```bash
    # From WSL terminal, navigate to your Windows project directory
    cd /mnt/c/path/to/your/hft_engine
    ```

3.  **Run Commands in WSL:**
    ```bash
    # All hft-build commands work seamlessly in WSL
    ./scripts/hft-build setup
    ./scripts/hft-build build
    ```

4.  **Alternative: Direct WSL Commands from PowerShell:**
    ```powershell
    # Run commands directly from PowerShell
    wsl bash -c "cd /mnt/c/Users/YourName/path/to/hft_engine && ./scripts/hft-build build"
    ```

## 🛠️ Unified Build System

This project uses a powerful unified build script located at `./scripts/hft-build` that provides all development operations in a single tool.

### ✨ Script Features

- **🚀 Zero-Configuration Setup:** Automatically detects and installs all required dependencies
- **🔄 Cross-Platform:** Works seamlessly on Linux, macOS, and Windows WSL
- **📊 Smart Dependency Management:** Checks for missing tools and guides installation
- **🔍 Comprehensive Testing:** Integrated unit testing with GoogleTest framework
- **📈 Code Coverage:** Generates detailed HTML and XML coverage reports
- **🛮 Code Quality:** Automated formatting and static analysis with clang-format and cppcheck
- **💾 Backup Management:** Creates timestamped project backups with optional compression
- **🎯 Memory Safety:** Built-in Valgrind integration for memory leak detection
- **🔗 Git Integration:** Automatic git hooks installation and optional commit automation
- **🎨 Colored Output:** Beautiful, informative console output with progress indicators

### 📋 Available Commands

| Command | Description | Usage |
|---------|-------------|-------|
| `build` | Build the project (default command) | `./scripts/hft-build build [OPTIONS]` |
| `test` | Run tests only | `./scripts/hft-build test` |
| `setup` | Setup development environment | `./scripts/hft-build setup` |
| `clean` | Clean build artifacts | `./scripts/hft-build clean` |
| `format` | Format code using clang-format | `./scripts/hft-build format` |
| `lint` | Run code linting with cppcheck | `./scripts/hft-build lint` |
| `backup` | Create project backup | `./scripts/hft-build backup [OPTIONS]` |
| `backup-raw` | Create raw content backup | `./scripts/hft-build backup-raw` |
| `install-hooks` | Install git hooks | `./scripts/hft-build install-hooks` |

### 🔧 Build Options

| Option | Description | Example |
|--------|-------------|--------|
| `--clean` | Clean build directory before building | `./scripts/hft-build build --clean` |
| `--coverage` | Generate coverage report | `./scripts/hft-build build --coverage` |
| `--no-tests` | Skip running tests after build | `./scripts/hft-build build --no-tests` |
| `--release` | Build in release mode (default: debug) | `./scripts/hft-build build --release` |
| `--commit` | Commit changes if all checks pass | `./scripts/hft-build build --commit` |

### 📦 Backup Options

| Option | Description | Example |
|--------|-------------|--------|
| `--output DIR` | Output directory for backup | `./scripts/hft-build backup --output my-backup` |
| `--name NAME` | Project name for backup | `./scripts/hft-build backup --name hft-engine-v2` |

### 🚀 Common Usage Examples

**Environment Setup (First Time):**
```bash
# Setup development environment with all dependencies
./scripts/hft-build setup
```

**Basic Development Workflow:**
```bash
# Standard build and test
./scripts/hft-build build

# Build in release mode
./scripts/hft-build build --release

# Clean build with coverage
./scripts/hft-build build --clean --coverage

# Build without running tests
./scripts/hft-build build --no-tests
```

**Code Quality:**
```bash
# Format all C++ code
./scripts/hft-build format

# Run static code analysis
./scripts/hft-build lint

# Run tests only
./scripts/hft-build test
```

**Maintenance:**
```bash
# Clean all build artifacts
./scripts/hft-build clean

# Create project backup
./scripts/hft-build backup

# Create raw content backup (no git history)
./scripts/hft-build backup-raw

# Install git hooks for development
./scripts/hft-build install-hooks
```

**Windows WSL Usage:**
```bash
# All commands work in WSL environment
wsl bash -c "cd /mnt/c/path/to/hft_engine && ./scripts/hft-build build"
```

### 📊 Coverage Reports

When using `--coverage`, the script generates detailed coverage reports:
- **Text summary:** Displayed in terminal
- **HTML report:** Available in `coverage/html/index.html`
- **XML report:** Available for CI/CD integration

```bash
# Generate coverage report
./scripts/hft-build build --coverage

# View HTML report (after build)
open coverage/html/index.html  # macOS
xdg-open coverage/html/index.html  # Linux
```

### 🔍 Getting Help

For complete command reference:
```bash
./scripts/hft-build --help
```

For command-specific help:
```bash
./scripts/hft-build build --help
./scripts/hft-build backup --help
```

### 🔧 Troubleshooting

**Common Issues and Solutions:**

| Issue | Solution |
|-------|----------|
| `Permission denied: ./scripts/hft-build` | Run `chmod +x ./scripts/hft-build` |
| `Dependencies missing` | Run `./scripts/hft-build setup` first |
| `Build fails with compiler errors` | Ensure you have a C++17 compatible compiler |
| `Tests segfault` | Run `./scripts/hft-build build --no-tests` to build without testing |
| `Coverage report empty` | Ensure tests ran successfully before generating coverage |
| `WSL path issues` | Use `/mnt/c/...` paths in WSL, not Windows paths |

**Debug Build Issues:**
```bash
# Clean everything and rebuild
./scripts/hft-build clean
./scripts/hft-build build --clean

# Check dependencies
./scripts/hft-build setup

# Build with verbose output
CMAKE_VERBOSE_MAKEFILE=ON ./scripts/hft-build build
```

**Performance Issues:**
```bash
# Use release build for better performance
./scripts/hft-build build --release

# Skip unnecessary checks
./scripts/hft-build build --no-tests
```

## 🌐 Web Interface

The HFT Engine includes a modern, responsive web-based trading dashboard that provides real-time monitoring, testing, and coverage reporting capabilities.

### ✨ Features

- **📊 Real-time Dashboard** - Live order book, market stats, and connection management
- **💰 Trading Interface** - Order placement, portfolio tracking, and order management
- **🧪 Browser-based Testing** - Run unit and integration tests directly in the browser
- **📈 Coverage Reports** - Visual coverage reports with file-level details
- **⚙️ Configuration** - Customizable settings for WebSocket, symbols, and update intervals

### 🚀 Quick Start

1. **Build the project first:**
   ```bash
   ./scripts/hft-build build
   ```

2. **Start the web server:**
   ```bash
   python3 web/server.py
   ```

3. **Open in browser:**
   Navigate to http://localhost:8080

### 🖥️ Interface Sections

| Section | Description | Features |
|---------|-------------|----------|
| **📊 Dashboard** | Main trading overview | Order book, market stats, connection controls |
| **💰 Trading** | Order management | Place orders, view portfolio, cancel orders |
| **🧪 Tests** | Test execution | Run unit/integration tests, view results |
| **📈 Coverage** | Code coverage | Line/function/branch coverage with visual reports |
| **⚙️ Settings** | Configuration | WebSocket URL, symbols, update intervals |

### 🎯 Browser Testing

Run your C++ tests directly in the browser:

1. Click the **Tests** tab
2. Choose test type:
   - **Run All Tests** - Complete test suite (35 tests)
   - **Unit Tests** - Unit tests only (25 tests)  
   - **Integration Tests** - Integration tests only (10 tests)
3. Watch real-time output and statistics

### 📊 Coverage Visualization

Generate and view code coverage reports:

1. Click the **Coverage** tab
2. Click **"Generate Coverage Report"**
3. View interactive progress bars and file details
4. Color-coded quality indicators (Green ≥90%, Orange ≥70%, Red <70%)

### 🎨 Modern UI Features

- **Dark Theme** - Professional dark gradient design
- **Responsive Layout** - Works on desktop, tablet, and mobile
- **Real-time Updates** - Live data with smooth animations
- **Interactive Elements** - Hover effects and smooth transitions
- **Typography** - Monospace fonts for trading data

### 🔧 Technical Details

- **Frontend:** Vanilla JavaScript (ES6+), HTML5, CSS3
- **Backend:** Python HTTP server with REST API endpoints
- **Real-time Data:** WebSocket-style updates with 100ms intervals
- **Browser Support:** Chrome 80+, Firefox 75+, Safari 13+, Edge 80+

### 📁 Web Files Structure

```
web/
├── index.html          # Main dashboard interface
├── assets/
│   ├── css/
│   │   └── styles.css  # Modern CSS with gradients
│   └── js/
│       └── app.js      # JavaScript application
├── server.py           # Python HTTP/API server
└── README.md           # Web interface documentation
```

For detailed web interface documentation, see [web/README.md](web/README.md).

## 🧪 Testing & Code Quality

We prioritize high-quality, reliable code.

-   **Unit Testing:** The core logic is tested using the **GoogleTest** framework. Tests are located in the `/tests` directory. You can run them using the build script:
    ```bash
    ./scripts/hft-build test
    ```
-   **Code Coverage:** Coverage reports are generated using `gcov` and `lcov`. Run a coverage build and find the HTML report in the `coverage/` directory.
    ```bash
    ./scripts/hft-build build --coverage
    ```
-   **Static Analysis:** Code is linted with `cppcheck` to catch potential issues early.
    ```bash
    ./scripts/hft-build lint
    ```

## 🐳 Docker Support

For a fully isolated and reproducible environment, you can use the provided `Dockerfile`.

1.  **Build the Docker Image:**
    ```bash
    docker build -t hft-engine .
    ```

2.  **Run the Container:**
    ```bash
    docker run -it --rm hft-engine
    ```

## 🛣️ Future Extensions

The engine is designed to be extended into a complete trading system. This would involve:
1.  **Live Trading Integration:** Implementing the `TradingClient` to make authenticated REST API calls to Binance for placing and managing orders.
2.  **Strategy Implementation:** Building trading algorithms (e.g., market making, arbitrage) that consume the live `OrderBook` data.
3.  **Advanced UI/Dashboard:** Enhancing the `UIManager` or connecting to a graphical frontend.
4.  **Risk Management:** Implementing robust risk controls, such as position limits, kill switches, and exposure monitoring.

## 🤝 Contributing

Contributions are welcome! If you'd like to help improve the engine, please follow these steps:

1.  Fork the repository.
2.  Create a new branch (`git checkout -b feature/your-feature-name`).
3.  Make your changes.
4.  Ensure all tests pass (`./scripts/hft-build test`).
5.  Submit a pull request with a clear description of your changes.

## 📜 License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.
