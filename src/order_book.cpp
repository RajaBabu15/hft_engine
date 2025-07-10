#include "hft/order_book.h"
#include <iomanip>

namespace hft {

    void OrderBook::update(const Command& cmd) {
        bids_.clear();
        asks_.clear();
        for (const auto& level : cmd.bids) {
            bids_[level.price] = level.quantity;
        }
        for (const auto& level : cmd.asks) {
            asks_[level.price] = level.quantity;
        }
    }

    void OrderBook::print_book(int depth) const {
        auto bid_it = bids_.cbegin();
        auto ask_it = asks_.cbegin();

        std::cout << "\033[2J\033[1;1H"; // Clear screen
        std::cout << "--- ORDER BOOK ---" << std::endl;
        std::cout << "------------------------------------" << std::endl;
        std::cout << "|       BIDS       |       ASKS       |" << std::endl;
        std::cout << "| Price    | Qty     | Price    | Qty     |" << std::endl;
        std::cout << "------------------------------------" << std::endl;

        for (int i = 0; i < depth; ++i) {
            std::cout << "| ";
            if (bid_it != bids_.end()) {
                std::cout << std::fixed << std::setprecision(2) << std::setw(8) << price_to_double(bid_it->first) << " | "
                          << std::setw(7) << bid_it->second << " |";
                ++bid_it;
            } else {
                std::cout << std::setw(8) << " " << " | " << std::setw(7) << " " << " |";
            }

            if (ask_it != asks_.end()) {
                std::cout << " " << std::fixed << std::setprecision(2) << std::setw(8) << price_to_double(ask_it->first) << " | "
                          << std::setw(7) << ask_it->second << " |";
                ++ask_it;
            } else {
                std::cout << " " << std::setw(8) << " " << " | " << std::setw(7) << " " << " |";
            }
            std::cout << std::endl;
        }
        std::cout << "------------------------------------" << std::endl;
    }
}