#include "hft/matching_engine.h"
#include "hft/websocket_client.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

int main() {
    std::ifstream config_file("config.json");
    if (!config_file.is_open()) {
        std::cerr << "Error: config.json not found!" << std::endl;
        return 1;
    }
    auto config = nlohmann::json::parse(config_file);

    hft::MatchingEngine engine(config["book_depth"]);
    engine.run();

    hft::WebsocketClient client(engine, config["symbol"]);

    std::cout << "Connecting to Binance for symbol: " << config["symbol"] << std::endl;
    std::cout << "Press Ctrl+C to exit." << std::endl;

    client.run(); // This will block

    engine.stop();
    return 0;
}