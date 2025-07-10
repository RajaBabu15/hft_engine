#pragma once
#include "types.h"
#include "order_book.h"
#include <vector>
#include <string>
#include <memory>
#include <functional>

namespace hft {
    enum class UIMode {
        LOGIN,
        MAIN_MENU,
        ORDER_BOOK_VIEW,
        ORDER_MANAGEMENT,
        ACCOUNT_INFO,
        SETTINGS
    };

    class UIManager {
    public:
        using LoginCallback = std::function<bool(const std::string&, const std::string&)>;
        using OrderCallback = std::function<void(const Symbol&, Side, OrderType, Price, Quantity)>;
        using CancelCallback = std::function<void(const OrderId&)>;

        UIManager();
        ~UIManager();

        // UI lifecycle
        bool initialize();
        void cleanup();
        void run();
        void stop();

        // Mode management
        void set_mode(UIMode mode);
        UIMode get_current_mode() const;

        // Display methods
        void display_login_screen();
        void display_main_menu();
        void display_order_book(const OrderBook& order_book, int depth = 10);
        void display_order_management(const std::vector<Order>& orders);
        void display_account_info(double balance, const std::vector<Trade>& recent_trades);
        void display_error(const std::string& message);
        void display_status(const std::string& message);

        // Input handling
        std::pair<std::string, std::string> get_login_credentials();
        int get_menu_choice(const std::vector<std::string>& options);
        Order get_order_input(const Symbol& symbol);
        std::string get_cancel_order_id();

        // Callbacks
        void set_login_callback(LoginCallback callback);
        void set_order_callback(OrderCallback callback);
        void set_cancel_callback(CancelCallback callback);

        // UI updates
        void update_connection_status(bool connected);
        void update_order_book(const OrderBook& order_book);
        void update_orders(const std::vector<Order>& orders);
        void update_balance(double balance);

        // Utility methods
        void clear_screen();
        void print_header(const std::string& title);
        void print_separator();
        void wait_for_key();

    private:
        UIMode current_mode_;
        bool running_;
        bool initialized_;

        // Callbacks
        LoginCallback login_callback_;
        OrderCallback order_callback_;
        CancelCallback cancel_callback_;

        // Display state
        bool connection_status_;
        std::string last_error_;
        std::string last_status_;

        // Input/output helpers
        std::string get_input(const std::string& prompt);
        double get_numeric_input(const std::string& prompt);
        char get_char_input();
        void print_colored(const std::string& text, const std::string& color);
        
        // Screen drawing
        void draw_border();
        void draw_order_book_table(const OrderBook& order_book, int depth);
        void draw_order_table(const std::vector<Order>& orders);
        void draw_menu(const std::vector<std::string>& options);
        
        // Validation
        bool validate_price_input(const std::string& input);
        bool validate_quantity_input(const std::string& input);
        
        // Color codes for terminal output
        static const std::string RESET;
        static const std::string RED;
        static const std::string GREEN;
        static const std::string YELLOW;
        static const std::string BLUE;
        static const std::string CYAN;
        static const std::string WHITE;
    };
}
