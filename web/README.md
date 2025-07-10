# HFT Engine Web Interface

A modern, responsive web-based trading dashboard for the High-Frequency Trading Engine.

## 🚀 Features

### 📊 Dashboard
- **Real-time Order Book** - Live BTCUSDT order book with bid/ask data
- **Market Statistics** - Last price, 24h change, spread, and volume
- **Connection Management** - Connect/disconnect to exchanges
- **Trading Controls** - Start/stop trading with one click

### 💰 Trading
- **Order Placement** - Market and limit orders with validation
- **Order Management** - View and cancel open orders
- **Portfolio Tracking** - Real-time BTC and USDT balances
- **Risk Management** - Built-in order validation

### 🧪 Testing
- **Test Runner** - Run unit, integration, or all tests
- **Real-time Results** - Live test output and statistics
- **Test Statistics** - Total, passed, failed counts with duration
- **Progressive Output** - See tests run in real-time

### 📈 Coverage
- **Coverage Reports** - Line, function, and branch coverage
- **Visual Progress** - Color-coded progress bars
- **File Details** - Per-file coverage breakdown
- **Threshold Tracking** - Monitor coverage quality

### ⚙️ Settings
- **WebSocket Configuration** - Custom WebSocket URLs
- **Symbol Selection** - Choose trading pairs
- **Update Intervals** - Customize refresh rates
- **Order Book Depth** - Configure display depth

## 🖥️ Usage

### Starting the Web Interface

1. **Build the project first:**
   ```bash
   ./scripts/hft-build build
   ```

2. **Start the web server:**
   ```bash
   python3 web/server.py
   ```

3. **Open in browser:**
   - Navigate to http://localhost:8080
   - The dashboard will load automatically

### Navigation

Use the top navigation bar to switch between sections:
- **📊 Dashboard** - Main trading overview
- **💰 Trading** - Order management and portfolio
- **🧪 Tests** - Run and view test results
- **📈 Coverage** - Code coverage reports
- **⚙️ Settings** - Configuration options

### Testing in the Browser

1. **Click the Tests tab**
2. **Choose test type:**
   - **Run All Tests** - Complete test suite (35 tests)
   - **Unit Tests** - Unit tests only (25 tests)
   - **Integration Tests** - Integration tests only (10 tests)
3. **Watch real-time output** in the test log
4. **View statistics** in the summary section

### Coverage Reports

1. **Click the Coverage tab**
2. **Click "Generate Coverage Report"**
3. **Wait for generation** (simulated 3-second process)
4. **View results:**
   - Progress bars show line, function, and branch coverage
   - File details table shows per-file statistics
   - Color coding indicates quality levels

### Trading Features

1. **Connect to Exchange:**
   - Click "Connect" in the Dashboard
   - Status will change to "Connected"
   - Real-time data updates begin

2. **Place Orders:**
   - Go to Trading tab
   - Fill in order form (type, side, quantity, price)
   - Click "Place Order"
   - Order appears in Open Orders table

3. **Monitor Portfolio:**
   - View real-time BTC and USDT balances
   - Track order status and history

## 🛠️ Technical Details

### Architecture
- **Frontend:** Vanilla JavaScript (ES6+), HTML5, CSS3
- **Backend:** Python HTTP server with API endpoints
- **Styling:** Modern CSS with gradients, animations, and responsive design
- **Data:** Real-time simulation with WebSocket-style updates

### API Endpoints
- `GET /api/tests/run` - Execute test suite
- `GET /api/coverage/generate` - Generate coverage report
- `GET /api/orderbook` - Get order book data
- `GET /api/orders` - Get order history

### Browser Compatibility
- Chrome 80+
- Firefox 75+
- Safari 13+
- Edge 80+

### Responsive Design
- Desktop: Full layout with all features
- Tablet: Responsive grid layout
- Mobile: Single-column layout with touch-friendly controls

## 🎨 Customization

### Themes
The interface uses a modern dark theme with:
- **Primary:** Cyan blue (`#00d4ff`)
- **Success:** Green (`#00ff88`)
- **Danger:** Red (`#ff6464`)
- **Background:** Dark gradient (`#0c0e27` to `#1a1d4a`)

### Settings
Customize the interface through the Settings tab:
- **WebSocket URL:** Change connection endpoint
- **Symbol:** Select different trading pairs
- **Update Interval:** Adjust refresh rate (50-5000ms)
- **Book Depth:** Configure order book display (5-100 levels)

## 🔧 Development

### File Structure
```
web/
├── index.html          # Main HTML structure
├── assets/
│   ├── css/
│   │   └── styles.css  # Complete stylesheet
│   └── js/
│       └── app.js      # JavaScript application
├── server.py           # Python HTTP server
└── README.md           # This file
```

### Adding Features
1. **New UI Elements:** Add to `index.html`
2. **Styling:** Update `assets/css/styles.css`
3. **Functionality:** Extend `assets/js/app.js`
4. **API Endpoints:** Add to `server.py`

### Testing Integration
The web interface can call actual C++ tests:
- Tests run through the build system
- Results parsed from standard output
- Real coverage reports from lcov/gcov

## 🚀 Deployment

### Production Setup
1. **Build optimizations:** Minify CSS/JS
2. **HTTPS:** Use secure connections
3. **WebSocket:** Implement real WebSocket server
4. **Authentication:** Add user management
5. **Real APIs:** Connect to actual exchange APIs

### Docker Deployment
```dockerfile
FROM python:3.9-slim
COPY web/ /app/
WORKDIR /app
EXPOSE 8080
CMD ["python3", "server.py"]
```

## 📊 Screenshots

The interface includes:
- **Modern Dark Theme** with gradient backgrounds
- **Real-time Order Book** with color-coded bids/asks
- **Interactive Charts** and progress bars
- **Responsive Layout** that works on all devices
- **Professional Typography** with monospace for data
- **Smooth Animations** and hover effects

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test in multiple browsers
5. Submit a pull request

## 📄 License

Part of the HFT Engine project by Raja Babu - see main project LICENSE file.
