#include "hft/websocket_client.h"
#include <nlohmann/json.hpp>

namespace hft {

    WebsocketClient::WebsocketClient(MatchingEngine& engine, std::string symbol)
        : engine_(engine), symbol_(symbol) {

        uri_ = "wss://stream.binance.com:9443/ws/" + symbol_ + "@depth20@100ms";

        ws_client_.init_asio();
        ws_client_.set_tls_init_handler([](websocketpp::connection_hdl){
            return std::make_shared<boost::asio::ssl::context>(boost::asio::ssl::context::sslv23);
        });

        ws_client_.set_message_handler(
            std::bind(&WebsocketClient::on_message, this,
                      std::placeholders::_1, std::placeholders::_2)
        );
    }

    void WebsocketClient::run() {
        websocketpp::lib::error_code ec;
        client::connection_ptr con = ws_client_.get_connection(uri_, ec);
        if (ec) {
            std::cout << "Could not create connection: " << ec.message() << std::endl;
            return;
        }
        ws_client_.connect(con);
        ws_client_.run();
    }

    void WebsocketClient::on_message(websocketpp::connection_hdl hdl, client::message_ptr msg) {
        auto json = nlohmann::json::parse(msg->get_payload());

        Command cmd;
        cmd.type = CommandType::MARKET_DATA;
        cmd.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

        for (const auto& bid : json["bids"]) {
            cmd.bids.push_back({
                price_to_int(std::stod(bid[0].get<std::string>())),
                (Quantity)std::stod(bid[1].get<std::string>())
            });
        }

        for (const auto& ask : json["asks"]) {
            cmd.asks.push_back({
                price_to_int(std::stod(ask[0].get<std::string>())),
                (Quantity)std::stod(ask[1].get<std::string>())
            });
        }

        while (!engine_.post_command(std::move(cmd))) {
            // Spin if the queue is full
        }
    }
}