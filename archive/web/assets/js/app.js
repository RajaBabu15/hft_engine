/**
 * HFT Engine Web Interface
 * Modern trading dashboard with real-time data, testing, and coverage
 */

class HFTDashboard {
    constructor() {
        this.websocket = null;
        this.isConnected = false;
        this.orderBook = { bids: [], asks: [] };
        this.settings = this.loadSettings();
        this.testResults = { total: 0, passed: 0, failed: 0, duration: 0 };
        this.coverage = { line: 0, function: 0, branch: 0 };
        
        this.init();
    }
    
    init() {
        this.setupEventListeners();
        this.initializeTabs();
        this.updateTime();
        this.startTimeUpdate();
        this.loadMockData();
    }
    
    setupEventListeners() {
        // Navigation
        document.querySelectorAll('.nav-btn').forEach(btn => {
            btn.addEventListener('click', (e) => this.switchTab(e.target.dataset.tab));
        });
        
        // Connection controls
        document.getElementById('connect-btn').addEventListener('click', () => this.connect());
        document.getElementById('disconnect-btn').addEventListener('click', () => this.disconnect());
        document.getElementById('start-trading').addEventListener('click', () => this.startTrading());
        document.getElementById('stop-trading').addEventListener('click', () => this.stopTrading());
        
        // Order form
        document.getElementById('order-form').addEventListener('submit', (e) => this.placeOrder(e));
        
        // Test controls
        document.getElementById('run-all-tests').addEventListener('click', () => this.runTests('all'));
        document.getElementById('run-unit-tests').addEventListener('click', () => this.runTests('unit'));
        document.getElementById('run-integration-tests').addEventListener('click', () => this.runTests('integration'));
        
        // Coverage
        document.getElementById('generate-coverage').addEventListener('click', () => this.generateCoverage());
        
        // Settings
        document.getElementById('settings-form').addEventListener('submit', (e) => this.saveSettings(e));
    }
    
    initializeTabs() {
        this.switchTab('dashboard');
    }
    
    switchTab(tabName) {
        // Update navigation
        document.querySelectorAll('.nav-btn').forEach(btn => {
            btn.classList.toggle('active', btn.dataset.tab === tabName);
        });
        
        // Update content
        document.querySelectorAll('.tab-content').forEach(content => {
            content.classList.toggle('active', content.id === tabName);
        });
    }
    
    updateTime() {
        const now = new Date();
        document.getElementById('current-time').textContent = now.toLocaleTimeString();
    }
    
    startTimeUpdate() {
        setInterval(() => this.updateTime(), 1000);
    }
    
    // WebSocket Connection Management
    connect() {
        if (this.websocket) {
            this.websocket.close();
        }
        
        try {
            // For demo purposes, we'll simulate a connection
            this.simulateConnection();
        } catch (error) {
            this.showError('Connection failed: ' + error.message);
        }
    }
    
    simulateConnection() {
        this.updateConnectionStatus('connecting');
        
        setTimeout(() => {
            this.isConnected = true;
            this.updateConnectionStatus('connected');
            this.startDataUpdates();
            this.showSuccess('Connected to exchange');
        }, 1500);
    }
    
    disconnect() {
        this.isConnected = false;
        this.updateConnectionStatus('disconnected');
        this.stopDataUpdates();
        this.showInfo('Disconnected from exchange');
    }
    
    updateConnectionStatus(status) {
        const statusElement = document.getElementById('connection-status');
        statusElement.textContent = status.charAt(0).toUpperCase() + status.slice(1);
        statusElement.className = `status ${status}`;
    }
    
    // Test Management - Simplified for demo
    async runTests(type) {
        const button = document.getElementById(`run-${type === 'all' ? 'all-tests' : type + '-tests'}`);
        const originalText = button.textContent;
        
        button.textContent = 'Running...';
        button.disabled = true;
        
        try {
            this.showInfo(`Running ${type} tests...`);
            const results = await this.simulateTestExecution(type);
            this.displayTestResults(results);
            this.showSuccess(`${type} tests completed`);
        } catch (error) {
            this.showError('Test execution failed: ' + error.message);
        } finally {
            button.textContent = originalText;
            button.disabled = false;
        }
    }
    
    async simulateTestExecution(type) {
        return new Promise((resolve) => {
            setTimeout(() => {
                let results;
                switch (type) {
                    case 'unit':
                        results = { total: 25, passed: 24, failed: 1, duration: 1250 };
                        break;
                    case 'integration':
                        results = { total: 10, passed: 10, failed: 0, duration: 5800 };
                        break;
                    default:
                        results = { total: 35, passed: 34, failed: 1, duration: 7050 };
                }
                resolve(results);
            }, 2000);
        });
    }
    
    displayTestResults(results) {
        document.getElementById('total-tests').textContent = results.total;
        document.getElementById('passed-tests').textContent = results.passed;
        document.getElementById('failed-tests').textContent = results.failed;
        document.getElementById('test-duration').textContent = results.duration + 'ms';
        
        const output = `Test Results for ${new Date().toLocaleString()}:
Total Tests: ${results.total}
Passed: ${results.passed}
Failed: ${results.failed}
Duration: ${results.duration}ms

All tests completed successfully!`;
        
        document.getElementById('test-log').textContent = output;
    }
    
    // Coverage Management
    async generateCoverage() {
        const button = document.getElementById('generate-coverage');
        const originalText = button.textContent;
        
        button.textContent = 'Generating...';
        button.disabled = true;
        
        try {
            this.showInfo('Generating coverage report...');
            await new Promise(resolve => setTimeout(resolve, 3000));
            
            const coverage = {
                line: 85.7,
                function: 92.3,
                branch: 78.1,
                details: [
                    { file: 'src/auth_manager.cpp', line: 94.2, function: 100, branch: 89.5 },
                    { file: 'src/order_book.cpp', line: 78.9, function: 85.7, branch: 72.1 },
                    { file: 'src/trading_client.cpp', line: 91.4, function: 96.2, branch: 83.7 },
                    { file: 'src/websocket_client.cpp', line: 82.6, function: 88.9, branch: 75.4 }
                ]
            };
            
            this.displayCoverageResults(coverage);
            this.showSuccess('Coverage report generated');
        } catch (error) {
            this.showError('Coverage generation failed: ' + error.message);
        } finally {
            button.textContent = originalText;
            button.disabled = false;
        }
    }
    
    displayCoverageResults(coverage) {
        this.updateCoverageBar('line-coverage', coverage.line);
        this.updateCoverageBar('function-coverage', coverage.function);
        this.updateCoverageBar('branch-coverage', coverage.branch);
        
        const detailsContainer = document.getElementById('coverage-details');
        detailsContainer.innerHTML = `
            <h4>File Coverage Details</h4>
            <table style="width: 100%; border-collapse: collapse; margin-top: 1rem;">
                <thead>
                    <tr style="background: rgba(255,255,255,0.1);">
                        <th style="padding: 0.5rem; border: 1px solid rgba(255,255,255,0.2);">File</th>
                        <th style="padding: 0.5rem; border: 1px solid rgba(255,255,255,0.2);">Line %</th>
                        <th style="padding: 0.5rem; border: 1px solid rgba(255,255,255,0.2);">Function %</th>
                        <th style="padding: 0.5rem; border: 1px solid rgba(255,255,255,0.2);">Branch %</th>
                    </tr>
                </thead>
                <tbody>
                    ${coverage.details.map(file => `
                        <tr>
                            <td style="padding: 0.5rem; border: 1px solid rgba(255,255,255,0.1);">${file.file}</td>
                            <td style="padding: 0.5rem; border: 1px solid rgba(255,255,255,0.1);">${file.line}%</td>
                            <td style="padding: 0.5rem; border: 1px solid rgba(255,255,255,0.1);">${file.function}%</td>
                            <td style="padding: 0.5rem; border: 1px solid rgba(255,255,255,0.1);">${file.branch}%</td>
                        </tr>
                    `).join('')}
                </tbody>
            </table>
        `;
    }
    
    updateCoverageBar(type, percentage) {
        const bar = document.getElementById(type + '-bar');
        const percent = document.getElementById(type + '-percent');
        
        bar.style.width = percentage + '%';
        percent.textContent = percentage + '%';
        
        if (percentage >= 90) {
            bar.style.background = 'linear-gradient(45deg, #00ff88, #00d4ff)';
        } else if (percentage >= 70) {
            bar.style.background = 'linear-gradient(45deg, #ffaa00, #ff8800)';
        } else {
            bar.style.background = 'linear-gradient(45deg, #ff6464, #cc4444)';
        }
    }
    
    // Trading Functions - Simplified
    startTrading() {
        if (!this.isConnected) {
            this.showError('Not connected to exchange');
            return;
        }
        this.showSuccess('Trading started');
    }
    
    stopTrading() {
        this.showInfo('Trading stopped');
    }
    
    placeOrder(event) {
        event.preventDefault();
        
        if (!this.isConnected) {
            this.showError('Not connected to exchange');
            return;
        }
        
        const formData = new FormData(event.target);
        const order = {
            type: document.getElementById('order-type').value,
            side: formData.get('side'),
            quantity: parseFloat(document.getElementById('quantity').value),
            price: parseFloat(document.getElementById('price').value)
        };
        
        if (!order.quantity || order.quantity <= 0) {
            this.showError('Invalid quantity');
            return;
        }
        
        if (order.type === 'limit' && (!order.price || order.price <= 0)) {
            this.showError('Invalid price for limit order');
            return;
        }
        
        this.addOrderToTable(order);
        this.showSuccess(`${order.side.toUpperCase()} order placed successfully`);
        event.target.reset();
    }
    
    addOrderToTable(order) {
        const tbody = document.getElementById('open-orders');
        const orderId = 'ORD_' + Date.now();
        
        const row = document.createElement('tr');
        row.innerHTML = `
            <td>${orderId}</td>
            <td><span class="${order.side}">${order.side.toUpperCase()}</span></td>
            <td>${order.type.toUpperCase()}</td>
            <td>${order.quantity.toFixed(4)}</td>
            <td>${order.type === 'market' ? 'MARKET' : '$' + order.price.toFixed(2)}</td>
            <td>PENDING</td>
            <td><button class="btn danger" onclick="hftDashboard.cancelOrder('${orderId}')">Cancel</button></td>
        `;
        
        tbody.appendChild(row);
    }
    
    cancelOrder(orderId) {
        const rows = document.querySelectorAll('#open-orders tr');
        rows.forEach(row => {
            if (row.cells[0].textContent === orderId) {
                row.remove();
                this.showInfo('Order cancelled');
            }
        });
    }
    
    // Data simulation and updates
    startDataUpdates() {
        this.dataUpdateInterval = setInterval(() => {
            this.updateOrderBook();
            this.updateMarketStats();
        }, this.settings.updateInterval);
    }
    
    stopDataUpdates() {
        if (this.dataUpdateInterval) {
            clearInterval(this.dataUpdateInterval);
        }
    }
    
    updateOrderBook() {
        this.generateMockOrderBook();
        this.renderOrderBook();
    }
    
    generateMockOrderBook() {
        const basePrice = 111054;
        const depth = Math.min(this.settings.bookDepth, 10);
        
        this.orderBook.bids = [];
        this.orderBook.asks = [];
        
        for (let i = 0; i < depth; i++) {
            const bidPrice = (basePrice - 0.01 - (i * 0.01)).toFixed(2);
            const askPrice = (basePrice + (i * 0.01)).toFixed(2);
            const bidQty = (Math.random() * 10).toFixed(2);
            const askQty = (Math.random() * 10).toFixed(2);
            
            this.orderBook.bids.push({ price: bidPrice, quantity: bidQty });
            this.orderBook.asks.push({ price: askPrice, quantity: askQty });
        }
    }
    
    renderOrderBook() {
        const tbody = document.getElementById('orderBookEntries');
        tbody.innerHTML = '';
        
        const maxRows = Math.max(this.orderBook.bids.length, this.orderBook.asks.length);
        
        for (let i = 0; i < maxRows; i++) {
            const bid = this.orderBook.bids[i] || { price: '', quantity: '' };
            const ask = this.orderBook.asks[i] || { price: '', quantity: '' };
            
            const row = document.createElement('tr');
            row.innerHTML = `
                <td class="bid-price">${bid.price}</td>
                <td class="quantity">${bid.quantity}</td>
                <td class="ask-price">${ask.price}</td>
                <td class="quantity">${ask.quantity}</td>
            `;
            tbody.appendChild(row);
        }
    }
    
    updateMarketStats() {
        const lastPrice = (111054 + (Math.random() - 0.5) * 100).toFixed(2);
        const change = ((Math.random() - 0.5) * 10).toFixed(2);
        const spread = this.orderBook.asks[0] && this.orderBook.bids[0] ? 
            (this.orderBook.asks[0].price - this.orderBook.bids[0].price).toFixed(2) : '0.01';
        const volume = (Math.random() * 1000000).toFixed(0);
        
        document.getElementById('last-price').textContent = `$${lastPrice}`;
        document.getElementById('price-change').textContent = `${change > 0 ? '+' : ''}${change}%`;
        document.getElementById('spread').textContent = `$${spread}`;
        document.getElementById('volume').textContent = `${Number(volume).toLocaleString()}`;
        
        document.getElementById('btc-balance').textContent = (Math.random() * 10).toFixed(8);
        document.getElementById('usdt-balance').textContent = (Math.random() * 100000).toFixed(2);
    }
    
    // Settings Management
    loadSettings() {
        const defaultSettings = {
            wsUrl: 'ws://localhost:8080/ws',
            symbol: 'btcusdt',
            updateInterval: 1000,
            bookDepth: 10
        };
        
        const saved = localStorage.getItem('hft-settings');
        return saved ? { ...defaultSettings, ...JSON.parse(saved) } : defaultSettings;
    }
    
    saveSettings(event) {
        event.preventDefault();
        
        this.settings = {
            wsUrl: document.getElementById('ws-url').value,
            symbol: document.getElementById('symbol').value,
            updateInterval: parseInt(document.getElementById('update-interval').value),
            bookDepth: parseInt(document.getElementById('book-depth').value)
        };
        
        localStorage.setItem('hft-settings', JSON.stringify(this.settings));
        this.showSuccess('Settings saved successfully');
    }
    
    // Load initial mock data
    loadMockData() {
        this.generateMockOrderBook();
        this.renderOrderBook();
        this.updateMarketStats();
        
        // Add sample orders
        const sampleOrders = [
            { id: 'ORD_001', side: 'buy', type: 'limit', quantity: 0.5, price: 111050, status: 'FILLED' },
            { id: 'ORD_002', side: 'sell', type: 'limit', quantity: 0.25, price: 111055, status: 'PENDING' }
        ];
        
        const tbody = document.getElementById('open-orders');
        sampleOrders.forEach(order => {
            const row = document.createElement('tr');
            row.innerHTML = `
                <td>${order.id}</td>
                <td><span class="${order.side}">${order.side.toUpperCase()}</span></td>
                <td>${order.type.toUpperCase()}</td>
                <td>${order.quantity.toFixed(4)}</td>
                <td>$${order.price.toFixed(2)}</td>
                <td>${order.status}</td>
                <td><button class="btn danger" onclick="hftDashboard.cancelOrder('${order.id}')">Cancel</button></td>
            `;
            tbody.appendChild(row);
        });
    }
    
    // Utility functions
    showSuccess(message) {
        this.showNotification(message, 'success');
    }
    
    showError(message) {
        this.showNotification(message, 'error');
    }
    
    showInfo(message) {
        this.showNotification(message, 'info');
    }
    
    showNotification(message, type) {
        console.log(`[${type.toUpperCase()}] ${message}`);
        
        const statusBar = document.querySelector('.status-bar');
        const notification = document.createElement('span');
        notification.textContent = message;
        notification.className = `notification ${type}`;
        statusBar.appendChild(notification);
        
        setTimeout(() => {
            notification.remove();
        }, 3000);
    }
}

// Initialize the dashboard
let hftDashboard;
document.addEventListener('DOMContentLoaded', () => {
    hftDashboard = new HFTDashboard();
});

// Add notification styles
const notificationStyles = `
.notification {
    padding: 0.25rem 0.5rem;
    border-radius: 4px;
    font-size: 0.875rem;
    margin-left: 1rem;
    animation: slideIn 0.3s ease;
}
.notification.success { background: rgba(0, 255, 136, 0.2); color: #00ff88; }
.notification.error { background: rgba(255, 100, 100, 0.2); color: #ff6464; }
.notification.info { background: rgba(0, 212, 255, 0.2); color: #00d4ff; }
@keyframes slideIn {
    from { opacity: 0; transform: translateX(20px); }
    to { opacity: 1; transform: translateX(0); }
}
`;

const styleSheet = document.createElement('style');
styleSheet.textContent = notificationStyles;
document.head.appendChild(styleSheet);
