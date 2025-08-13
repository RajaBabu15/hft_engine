#pragma once

#include <string>

namespace hft {
    class NetworkFeed {
        public:
            bool connect(const std::string &addr) {
                return true; // Simulate a successful connection
            }
            std::string recv_packet() {
                return "8=FIX.4.4|35=D|..."; //Dummy FIX message
            }
    };
};