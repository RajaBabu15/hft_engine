#include "hft/ui_manager.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace hft {

// Color constants
const std::string UIManager::RESET = "\033[0m";
const std::string UIManager::RED = "\033[31m";
const std::string UIManager::GREEN = "\033[32m";
const std::string UIManager::YELLOW = "\033[33m";
const std::string UIManager::BLUE = "\033[34m";
const std::string UIManager::CYAN = "\033[36m";
const std::string UIManager::WHITE = "\033[37m";

UIManager::UIManager()
    : current_mode_(UIMode::LOGIN), running_(false), initialized_(false),
      connection_status_(false) {}

UIManager::~UIManager() { cleanup(); }

bool UIManager::initialize() {
  if (initialized_) {
    return true;
  }

  try {
    // Initialize terminal settings
    clear_screen();
    initialized_ = true;
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Failed to initialize UI: " << e.what() << std::endl;
    return false;
  }
}

void UIManager::cleanup() {
  if (initialized_) {
    clear_screen();
    initialized_ = false;
  }
}

void UIManager::run() {
  if (!initialize()) {
    return;
  }

  running_ = true;

  while (running_) {
    try {
      switch (current_mode_) {
      case UIMode::LOGIN:
        display_login_screen();
        break;
      case UIMode::MAIN_MENU:
        display_main_menu();
        break;
      case UIMode::ORDER_BOOK_VIEW:
        // This would display live order book - for now just show menu
        display_main_menu();
        break;
      case UIMode::ORDER_MANAGEMENT:
        // This would show order management interface
        display_main_menu();
        break;
      case UIMode::ACCOUNT_INFO:
        // This would show account information
        display_main_menu();
        break;
      case UIMode::SETTINGS:
        // This would show settings
        display_main_menu();
        break;
      }

      // Small delay to prevent excessive CPU usage
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } catch (const std::exception &e) {
      display_error("UI Error: " + std::string(e.what()));
      std::this_thread::sleep_for(std::chrono::seconds(2));
    }
  }
}

void UIManager::stop() { running_ = false; }

void UIManager::set_mode(UIMode mode) {
  current_mode_ = mode;
  clear_screen();
}

UIMode UIManager::get_current_mode() const { return current_mode_; }

void UIManager::display_login_screen() {
  clear_screen();
  print_header("HFT ENGINE - LOGIN");

  std::cout << "\n";
  print_colored("Welcome to the High-Frequency Trading Engine", CYAN);
  std::cout << "\n\n";

  std::cout << "Please enter your API credentials:\n\n";

  auto credentials = get_login_credentials();

  // Call login callback if set
  if (login_callback_) {
    bool success = login_callback_(credentials.first, credentials.second);
    if (success) {
      display_status("Login successful!");
      std::this_thread::sleep_for(std::chrono::seconds(1));
      set_mode(UIMode::MAIN_MENU);
    } else {
      display_error("Login failed! Please check your credentials.");
      std::this_thread::sleep_for(std::chrono::seconds(2));
    }
  } else {
    // No callback set, assume success for demo
    display_status("Demo mode - Login bypassed");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    set_mode(UIMode::MAIN_MENU);
  }
}

void UIManager::display_main_menu() {
  clear_screen();
  print_header("HFT ENGINE - MAIN MENU");

  // Show connection status
  if (connection_status_) {
    print_colored("Status: CONNECTED", GREEN);
  } else {
    print_colored("Status: DISCONNECTED", RED);
  }
  std::cout << "\n\n";

  std::vector<std::string> options = {
      "1. View Order Book", "2. Order Management", "3. Account Information",
      "4. Settings", "5. Exit"};

  draw_menu(options);

  int choice = get_menu_choice(options);

  switch (choice) {
  case 1:
    set_mode(UIMode::ORDER_BOOK_VIEW);
    break;
  case 2:
    set_mode(UIMode::ORDER_MANAGEMENT);
    break;
  case 3:
    set_mode(UIMode::ACCOUNT_INFO);
    break;
  case 4:
    set_mode(UIMode::SETTINGS);
    break;
  case 5:
    stop();
    break;
  default:
    display_error("Invalid choice");
    std::this_thread::sleep_for(std::chrono::seconds(1));
    break;
  }
}

void UIManager::display_order_book(const OrderBook &order_book, int depth) {
  clear_screen();
  print_header("REAL-TIME ORDER BOOK");

  // This would integrate with the existing order book display
  // For now, show placeholder
  std::cout << "Order book display would appear here...\n";
  std::cout << "Press any key to return to main menu...\n";
  wait_for_key();
  set_mode(UIMode::MAIN_MENU);
}

void UIManager::display_order_management(const std::vector<Order> &orders) {
  clear_screen();
  print_header("ORDER MANAGEMENT");

  if (orders.empty()) {
    std::cout << "No active orders.\n\n";
  } else {
    draw_order_table(orders);
  }

  std::vector<std::string> options = {"1. Place New Order", "2. Cancel Order",
                                      "3. Cancel All Orders", "4. Refresh",
                                      "5. Back to Main Menu"};

  draw_menu(options);

  int choice = get_menu_choice(options);

  switch (choice) {
  case 1: {
    Order new_order = get_order_input("BTCUSDT");
    if (order_callback_) {
      order_callback_(new_order.symbol, new_order.side, new_order.type,
                      new_order.price, new_order.quantity);
    }
    break;
  }
  case 2: {
    std::string order_id = get_cancel_order_id();
    if (!order_id.empty() && cancel_callback_) {
      cancel_callback_(order_id);
    }
    break;
  }
  case 3:
    // Cancel all orders logic
    display_status("Cancelling all orders...");
    break;
  case 4:
    // Refresh logic
    break;
  case 5:
    set_mode(UIMode::MAIN_MENU);
    break;
  }
}

void UIManager::display_account_info(double balance,
                                     const std::vector<Trade> &recent_trades) {
  clear_screen();
  print_header("ACCOUNT INFORMATION");

  std::cout << std::fixed << std::setprecision(2);
  std::cout << "Account Balance: $" << balance << " USDT\n\n";

  if (!recent_trades.empty()) {
    std::cout << "Recent Trades:\n";
    std::cout << std::left << std::setw(15) << "Order ID" << std::setw(10)
              << "Side" << std::setw(12) << "Price" << std::setw(12)
              << "Quantity" << "\n";
    print_separator();

    for (const auto &trade : recent_trades) {
      std::cout << std::left << std::setw(15) << trade.order_id << std::setw(10)
                << (trade.side == Side::BUY ? "BUY" : "SELL") << std::setw(12)
                << trade.price << std::setw(12) << trade.quantity << "\n";
    }
  }

  std::cout << "\nPress any key to return to main menu...";
  wait_for_key();
  set_mode(UIMode::MAIN_MENU);
}

void UIManager::display_error(const std::string &message) {
  print_colored("ERROR: " + message, RED);
  std::cout << "\n";
}

void UIManager::display_status(const std::string &message) {
  print_colored("STATUS: " + message, GREEN);
  std::cout << "\n";
}

std::pair<std::string, std::string> UIManager::get_login_credentials() {
  std::string api_key, secret_key;

  std::cout << "API Key: ";
  std::getline(std::cin, api_key);

  std::cout << "Secret Key: ";
  std::getline(std::cin, secret_key);

  return {api_key, secret_key};
}

int UIManager::get_menu_choice(const std::vector<std::string> &options) {
  std::cout << "\nEnter your choice: ";
  int choice;
  std::cin >> choice;

  // Clear input buffer
  std::cin.clear();
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

  return choice;
}

Order UIManager::get_order_input(const Symbol &symbol) {
  clear_screen();
  print_header("PLACE NEW ORDER");

  Order order;
  order.symbol = symbol;

  std::cout << "Symbol: " << symbol << "\n\n";

  // Get side
  std::cout << "Side (1=BUY, 2=SELL): ";
  int side_choice;
  std::cin >> side_choice;
  order.side = (side_choice == 1) ? Side::BUY : Side::SELL;

  // Get order type
  std::cout << "Order Type (1=MARKET, 2=LIMIT): ";
  int type_choice;
  std::cin >> type_choice;
  order.type = (type_choice == 1) ? OrderType::MARKET : OrderType::LIMIT;

  // Get price (if limit order)
  if (order.type == OrderType::LIMIT) {
    order.price = get_numeric_input("Price: ");
  }

  // Get quantity
  order.quantity = get_numeric_input("Quantity: ");

  // Clear input buffer
  std::cin.clear();
  std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

  return order;
}

std::string UIManager::get_cancel_order_id() {
  std::string order_id = get_input("Enter Order ID to cancel: ");
  return order_id;
}

void UIManager::set_login_callback(LoginCallback callback) {
  login_callback_ = callback;
}

void UIManager::set_order_callback(OrderCallback callback) {
  order_callback_ = callback;
}

void UIManager::set_cancel_callback(CancelCallback callback) {
  cancel_callback_ = callback;
}

void UIManager::update_connection_status(bool connected) {
  connection_status_ = connected;
}

void UIManager::update_order_book(const OrderBook &order_book) {
  // Implementation would update order book display
}

void UIManager::update_orders(const std::vector<Order> &orders) {
  // Implementation would update order display
}

void UIManager::update_balance(double balance) {
  // Implementation would update balance display
}

void UIManager::clear_screen() {
#ifdef _WIN32
  system("cls");
#else
  system("clear");
#endif
}

void UIManager::print_header(const std::string &title) {
  print_separator();
  std::cout << "| " << std::left << std::setw(76) << title << " |\n";
  print_separator();
}

void UIManager::print_separator() {
  std::cout << "+" << std::string(78, '-') << "+\n";
}

void UIManager::wait_for_key() {
#ifdef _WIN32
  _getch();
#else
  getchar();
#endif
}

std::string UIManager::get_input(const std::string &prompt) {
  std::cout << prompt;
  std::string input;
  std::getline(std::cin, input);
  return input;
}

double UIManager::get_numeric_input(const std::string &prompt) {
  std::cout << prompt;
  double value;
  std::cin >> value;
  return value;
}

char UIManager::get_char_input() {
#ifdef _WIN32
  return _getch();
#else
  return getchar();
#endif
}

void UIManager::print_colored(const std::string &text,
                              const std::string &color) {
  std::cout << color << text << RESET;
}

void UIManager::draw_border() {
  std::cout << "+" << std::string(78, '-') << "+\n";
}

void UIManager::draw_order_book_table(const OrderBook &order_book, int depth) {
  // Implementation would draw order book table
}

void UIManager::draw_order_table(const std::vector<Order> &orders) {
  std::cout << std::left << std::setw(15) << "Order ID" << std::setw(10)
            << "Symbol" << std::setw(8) << "Side" << std::setw(10) << "Type"
            << std::setw(12) << "Price" << std::setw(12) << "Quantity"
            << std::setw(10) << "Status" << "\n";
  print_separator();

  for (const auto &order : orders) {
    std::cout << std::left << std::setw(15) << order.id << std::setw(10)
              << order.symbol << std::setw(8)
              << (order.side == Side::BUY ? "BUY" : "SELL") << std::setw(10)
              << (order.type == OrderType::MARKET ? "MARKET" : "LIMIT")
              << std::setw(12) << std::fixed << std::setprecision(2)
              << order.price << std::setw(12) << order.quantity << std::setw(10)
              << "NEW" << "\n";
  }
}

void UIManager::draw_menu(const std::vector<std::string> &options) {
  for (const auto &option : options) {
    std::cout << option << "\n";
  }
}

bool UIManager::validate_price_input(const std::string &input) {
  try {
    double price = std::stod(input);
    return price > 0.0;
  } catch (...) {
    return false;
  }
}

bool UIManager::validate_quantity_input(const std::string &input) {
  try {
    double quantity = std::stod(input);
    return quantity > 0.0;
  } catch (...) {
    return false;
  }
}

} // namespace hft
