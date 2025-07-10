#pragma once
#include "matching_engine.h"
#include <memory>
#include <string>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>

namespace hft {

// Use the standard, default client config
using client = websocketpp::client<websocketpp::config::asio_tls_client>;

class WebsocketClient {
public:
  WebsocketClient(MatchingEngine &engine, std::string symbol);
  void run();

private:
  void on_message(websocketpp::connection_hdl hdl, client::message_ptr msg);

  MatchingEngine &engine_;
  client ws_client_;
  std::string uri_;
  std::string symbol_;
};
} // namespace hft