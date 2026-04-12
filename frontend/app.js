// frontend/app.js
const API_BASE = 'http://localhost:8080/api';
let currentUser = null;
let marketChart = null;
let portfolioChart = null;

// Authentication
function showAuthTab(tab) {
    document.getElementById('loginForm').style.display = tab === 'login' ? 'block' : 'none';
    document.getElementById('registerForm').style.display = tab === 'register' ? 'block' : 'none';

    // Update tab styles
    document.querySelectorAll('#authTabs button').forEach(btn => {
        btn.classList.remove('active');
    });
    event.target.classList.add('active');
}

async function login() {
    const username = document.getElementById('loginUsername').value;
    const password = document.getElementById('loginPassword').value;
    const messageDiv = document.getElementById('loginMessage');

    if (!username || !password) {
        showMessage(messageDiv, 'Please enter username and password', 'danger');
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/login`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ username, password })
        });

        const data = await response.json();

        if (data.success) {
            currentUser = {
                id: data.user_id,
                username: data.username,
                balance: data.balance
            };

            showMessage(messageDiv, 'Login successful!', 'success');
            updateUserInfo();
            showSection('dashboard');
            loadDashboardData();
        } else {
            showMessage(messageDiv, data.error, 'danger');
        }
    } catch (error) {
        showMessage(messageDiv, 'Login failed: ' + error.message, 'danger');
    }
}

async function register() {
    const username = document.getElementById('regUsername').value;
    const email = document.getElementById('regEmail').value;
    const password = document.getElementById('regPassword').value;
    const balance = document.getElementById('regBalance').value;
    const messageDiv = document.getElementById('registerMessage');

    if (!username || !email || !password) {
        showMessage(messageDiv, 'Please fill all fields', 'danger');
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/register`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ username, password, email, initial_balance: parseFloat(balance) })
        });

        const data = await response.json();

        if (data.success) {
            showMessage(messageDiv, 'Registration successful! Please login.', 'success');
            showAuthTab('login');
            document.getElementById('loginUsername').value = username;
            document.getElementById('loginPassword').value = password;
        } else {
            showMessage(messageDiv, data.error, 'danger');
        }
    } catch (error) {
        showMessage(messageDiv, 'Registration failed: ' + error.message, 'danger');
    }
}

function logout() {
    currentUser = null;
    updateUserInfo();
    showSection('login');
}

function updateUserInfo() {
    const loginLink = document.getElementById('loginLink');
    const userInfo = document.getElementById('userInfo');
    const logoutBtn = document.getElementById('logoutBtn');

    if (currentUser) {
        loginLink.style.display = 'none';
        userInfo.style.display = 'block';
        userInfo.textContent = `Welcome, ${currentUser.username}`;
        logoutBtn.style.display = 'block';
    } else {
        loginLink.style.display = 'block';
        userInfo.style.display = 'none';
        logoutBtn.style.display = 'none';
    }
}

// Navigation
function showSection(sectionId) {
    if (!currentUser && sectionId !== 'login') {
        showSection('login');
        return;
    }

    // Hide all sections
    document.querySelectorAll('[id$="Section"]').forEach(div => {
        div.style.display = 'none';
    });

    // Show selected section
    document.getElementById(sectionId + 'Section').style.display = 'block';

    // Load section data
    switch (sectionId) {
        case 'dashboard':
            loadDashboardData();
            break;
        case 'trading':
            loadTradingData();
            break;
        case 'portfolio':
            loadPortfolioData();
            break;
        case 'leaderboard':
            loadLeaderboardData();
            break;
    }
}

// Dashboard
async function loadDashboardData() {
    if (!currentUser) return;

    try {
        // Load account summary
        const portfolioRes = await fetch(`${API_BASE}/portfolio?user_id=${currentUser.id}`);
        const portfolioData = await portfolioRes.json();

        if (portfolioData.success) {
            const summaryDiv = document.getElementById('accountSummary');
            summaryDiv.innerHTML = `
                <h5>${currentUser.username}</h5>
                <p>Cash: $${portfolioData.cash_balance.toFixed(2)}</p>
                <p>Portfolio Value: $${portfolioData.total_value.toFixed(2)}</p>
                <p>Total: $${(portfolioData.cash_balance + portfolioData.total_value).toFixed(2)}</p>
            `;
        }

        // Load market data
        const symbolsRes = await fetch(`${API_BASE}/symbols`);
        const symbolsData = await symbolsRes.json();

        if (symbolsData.success && marketChart) {
            marketChart.destroy();
        }

        if (symbolsData.success) {
            const labels = symbolsData.symbols.map(s => s.symbol);
            const prices = symbolsData.symbols.map(s => s.price);

            const ctx = document.getElementById('marketChart').getContext('2d');
            marketChart = new Chart(ctx, {
                type: 'line',
                data: {
                    labels: labels,
                    datasets: [{
                        label: 'Stock Prices',
                        data: prices,
                        borderColor: '#3498db',
                        backgroundColor: 'rgba(52, 152, 219, 0.1)',
                        tension: 0.1
                    }]
                },
                options: {
                    responsive: true,
                    scales: {
                        y: { beginAtZero: false }
                    }
                }
            });
        }

        // Load recent trades
        const tradesRes = await fetch(`${API_BASE}/orderbook?symbol=AAPL`);
        const tradesData = await tradesRes.json();

        if (tradesData.success) {
            // Would display trades here
        }

    } catch (error) {
        console.error('Failed to load dashboard data:', error);
    }
}

// Trading
async function loadTradingData() {
    // Load symbols for autocomplete
    try {
        const symbolsRes = await fetch(`${API_BASE}/symbols`);
        const symbolsData = await symbolsRes.json();

        if (symbolsData.success) {
            const datalist = document.getElementById('symbolsList');
            datalist.innerHTML = symbolsData.symbols.map(s =>
                `<option value="${s.symbol}">${s.symbol} - $${s.price.toFixed(2)}</option>`
            ).join('');
        }
    } catch (error) {
        console.error('Failed to load symbols:', error);
    }

    // Load initial order book
    await loadOrderBook('AAPL');
}

async function loadOrderBook(symbol) {
    try {
        const response = await fetch(`${API_BASE}/orderbook?symbol=${symbol}`);
        const data = await response.json();

        if (data.success) {
            document.getElementById('currentSymbol').textContent = symbol;
            document.getElementById('orderSymbol').value = symbol;

            // Update spread
            document.getElementById('spreadValue').textContent =
                data.spread ? `$${data.spread.toFixed(2)}` : 'N/A';

            // Note: In full implementation, we would display bids and asks
            // For now, just show placeholders
            document.getElementById('bidsTable').innerHTML =
                `<div class="text-muted">Bids data would appear here</div>`;
            document.getElementById('asksTable').innerHTML =
                `<div class="text-muted">Asks data would appear here</div>`;
        }
    } catch (error) {
        console.error('Failed to load order book:', error);
    }
}

async function placeOrder() {
    if (!currentUser) {
        showMessage(document.getElementById('orderMessage'), 'Please login first', 'danger');
        return;
    }

    const symbol = document.getElementById('orderSymbol').value;
    const side = parseInt(document.getElementById('orderSide').value);
    const type = parseInt(document.getElementById('orderType').value);
    const price = parseFloat(document.getElementById('orderPrice').value);
    const quantity = parseInt(document.getElementById('orderQuantity').value);
    const messageDiv = document.getElementById('orderMessage');

    if (!symbol || !quantity || (type === 2 && !price)) {
        showMessage(messageDiv, 'Please fill all required fields', 'danger');
        return;
    }

    try {
        const response = await fetch(`${API_BASE}/order`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                user_id: currentUser.id,
                symbol,
                side,
                type,
                price,
                quantity
            })
        });

        const data = await response.json();

        if (data.success) {
            showMessage(messageDiv, `Order placed successfully! Order ID: ${data.order_id}`, 'success');
            // Clear form
            document.getElementById('orderPrice').value = '';
            document.getElementById('orderQuantity').value = '';
            // Refresh order book
            loadOrderBook(symbol);
            // Refresh portfolio
            loadPortfolioData();
        } else {
            showMessage(messageDiv, data.error, 'danger');
        }
    } catch (error) {
        showMessage(messageDiv, 'Order placement failed: ' + error.message, 'danger');
    }
}

// Portfolio
async function loadPortfolioData() {
    if (!currentUser) return;

    try {
        const response = await fetch(`${API_BASE}/portfolio?user_id=${currentUser.id}`);
        const data = await response.json();

        if (data.success) {
            // Update cash balance
            document.getElementById('cashBalance').textContent =
                `$${data.cash_balance.toFixed(2)}`;

            // Update holdings table
            if (data.portfolio && data.portfolio.length > 0) {
                const holdingsHtml = data.portfolio.map(holding => `
                    <div class="card mb-2 trade-row">
                        <div class="card-body">
                            <div class="row">
                                <div class="col-md-3">
                                    <strong>${holding.symbol}</strong>
                                </div>
                                <div class="col-md-2">
                                    ${holding.quantity} shares
                                </div>
                                <div class="col-md-2">
                                    Avg: $${holding.avg_price ? holding.avg_price.toFixed(2) : 'N/A'}
                                </div>
                                <div class="col-md-2">
                                    Current: $${holding.current_price.toFixed(2)}
                                </div>
                                <div class="col-md-3">
                                    P&L: 
                                    <span class="${holding.pnl >= 0 ? 'positive' : 'negative'}">
                                        $${holding.pnl ? holding.pnl.toFixed(2) : '0.00'} 
                                        (${holding.pnl_percent ? holding.pnl_percent.toFixed(2) : '0.00'}%)
                                    </span>
                                </div>
                            </div>
                        </div>
                    </div>
                `).join('');

                document.getElementById('holdingsTable').innerHTML = holdingsHtml;
            } else {
                document.getElementById('holdingsTable').innerHTML =
                    '<div class="text-muted">No holdings yet</div>';
            }

            // Update portfolio chart
            if (portfolioChart) {
                portfolioChart.destroy();
            }

            if (data.portfolio && data.portfolio.length > 0) {
                const ctx = document.getElementById('portfolioChart').getContext('2d');
                const labels = data.portfolio.map(h => h.symbol);
                const values = data.portfolio.map(h => h.value);

                portfolioChart = new Chart(ctx, {
                    type: 'pie',
                    data: {
                        labels: labels,
                        datasets: [{
                            data: values,
                            backgroundColor: [
                                '#3498db', '#2ecc71', '#e74c3c', '#f39c12',
                                '#9b59b6', '#1abc9c', '#d35400', '#34495e'
                            ]
                        }]
                    },
                    options: {
                        responsive: true,
                        plugins: {
                            legend: { position: 'bottom' }
                        }
                    }
                });
            }
        }
    } catch (error) {
        console.error('Failed to load portfolio data:', error);
    }
}

// Leaderboard
async function loadLeaderboardData() {
    try {
        const response = await fetch(`${API_BASE}/leaderboard?limit=20`);
        const data = await response.json();

        if (data.success) {
            const tableBody = document.getElementById('leaderboardTable');

            if (data.leaderboard && data.leaderboard.length > 0) {
                const rows = data.leaderboard.map(entry => `
                    <tr ${entry.user_id === currentUser?.id ? 'class="table-info"' : ''}>
                        <td>${entry.rank}</td>
                        <td>${entry.username || `User ${entry.user_id}`}</td>
                        <td>$${entry.portfolio_value.toFixed(2)}</td>
                        <td class="${entry.daily_change >= 0 ? 'positive' : 'negative'}">
                            ${entry.daily_change >= 0 ? '+' : ''}${entry.daily_change.toFixed(2)}%
                        </td>
                        <td class="${entry.weekly_change >= 0 ? 'positive' : 'negative'}">
                            ${entry.weekly_change >= 0 ? '+' : ''}${entry.weekly_change.toFixed(2)}%
                        </td>
                    </tr>
                `).join('');

                tableBody.innerHTML = rows;
            } else {
                tableBody.innerHTML = '<tr><td colspan="5">No data available</td></tr>';
            }
        }
    } catch (error) {
        console.error('Failed to load leaderboard data:', error);
    }
}

// Search
async function searchSymbols(query) {
    if (query.length < 2) return;

    try {
        const response = await fetch(`${API_BASE}/search?q=${encodeURIComponent(query)}`);
        const data = await response.json();

        if (data.success) {
            // Update symbols list
            const datalist = document.getElementById('symbolsList');
            datalist.innerHTML = data.results.map(s =>
                `<option value="${s.symbol}">${s.symbol} - $${s.price.toFixed(2)}</option>`
            ).join('');
        }
    } catch (error) {
        console.error('Search failed:', error);
    }
}

// Helper functions
function showMessage(element, message, type) {
    element.innerHTML = `
        <div class="alert alert-${type} alert-dismissible fade show" role="alert">
            ${message}
            <button type="button" class="btn-close" data-bs-dismiss="alert"></button>
        </div>
    `;

    // Auto-dismiss after 5 seconds
    setTimeout(() => {
        if (element.firstChild) {
            element.firstChild.remove();
        }
    }, 5000);
}

// Initialize
document.addEventListener('DOMContentLoaded', function () {
    // Set up symbol search on input
    const symbolInput = document.getElementById('orderSymbol');
    if (symbolInput) {
        symbolInput.addEventListener('input', function () {
            if (this.value.length >= 2) {
                searchSymbols(this.value);
            }
        });
    }

    // Set up order type change
    const orderType = document.getElementById('orderType');
    if (orderType) {
        orderType.addEventListener('change', function () {
            const priceInput = document.getElementById('orderPrice');
            priceInput.disabled = this.value === '1'; // MARKET order
            priceInput.required = this.value === '2'; // LIMIT order
        });
    }
});

// Auto-refresh data
setInterval(() => {
    if (currentUser) {
        const currentSection = document.querySelector('[id$="Section"][style*="block"]');
        if (currentSection) {
            const sectionId = currentSection.id.replace('Section', '');
            switch (sectionId) {
                case 'dashboard':
                    loadDashboardData();
                    break;
                case 'trading':
                    loadOrderBook(document.getElementById('orderSymbol').value || 'AAPL');
                    break;
                case 'portfolio':
                    loadPortfolioData();
                    break;
                case 'leaderboard':
                    loadLeaderboardData();
                    break;
            }
        }
    }
}, 10000); // Refresh every 10 seconds