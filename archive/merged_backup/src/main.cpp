#include "hft/matching_engine.h"
#include "hft/order_book.h"
#include "hft/types.h"
#include <iostream>
#include <chrono>
#include <thread>

int main() {
  std::cout << "🚀 HFT Engine Starting..." << std::endl;
  
  // Create matching engine with depth 10
  hft::MatchingEngine engine(10);
  
  std::cout << "📊 Order Book initialized with depth: 10" << std::endl;
  
  // Start the engine
  engine.run();
  
  std::cout << "✅ Matching Engine is running..." << std::endl;
  std::cout << "📈 Ready to process market data..." << std::endl;
  
  // Simulate market data updates for demonstration
  std::cout << "\n🔧 Simulating market data updates..." << std::endl;
  
  // Create sample market data update 1
  hft::Command market_data1;
  market_data1.type = hft::CommandType::MARKET_DATA;
  market_data1.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::high_resolution_clock::now().time_since_epoch()).count();
  
  // Add some bid levels
  market_data1.bids.push_back({50000.0, 1.5});
  market_data1.bids.push_back({49950.0, 2.0});
  market_data1.bids.push_back({49900.0, 3.0});
  
  // Add some ask levels
  market_data1.asks.push_back({50100.0, 2.0});
  market_data1.asks.push_back({50150.0, 1.5});
  market_data1.asks.push_back({50200.0, 2.5});
  
  // Post market data to the engine
  engine.post_command(std::move(market_data1));
  std::cout << "📥 Posted market data update 1" << std::endl;
  
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  
  // Create sample market data update 2
  hft::Command market_data2;
  market_data2.type = hft::CommandType::MARKET_DATA;
  market_data2.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::high_resolution_clock::now().time_since_epoch()).count();
  
  // Update bid levels
  market_data2.bids.push_back({50050.0, 1.0});
  market_data2.bids.push_back({50000.0, 2.0});
  market_data2.bids.push_back({49950.0, 1.5});
  
  // Update ask levels
  market_data2.asks.push_back({50100.0, 1.0});
  market_data2.asks.push_back({50150.0, 2.0});
  market_data2.asks.push_back({50200.0, 1.5});
  
  // Post market data to the engine
  engine.post_command(std::move(market_data2));
  std::cout << "📥 Posted market data update 2" << std::endl;
  
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  
  // Create sample market data update 3
  hft::Command market_data3;
  market_data3.type = hft::CommandType::MARKET_DATA;
  market_data3.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::high_resolution_clock::now().time_since_epoch()).count();
  
  // Final bid levels
  market_data3.bids.push_back({50025.0, 2.5});
  market_data3.bids.push_back({50000.0, 1.0});
  market_data3.bids.push_back({49975.0, 3.0});
  
  // Final ask levels
  market_data3.asks.push_back({50075.0, 1.5});
  market_data3.asks.push_back({50100.0, 2.0});
  market_data3.asks.push_back({50125.0, 1.0});
  
  // Post market data to the engine
  engine.post_command(std::move(market_data3));
  std::cout << "📥 Posted market data update 3" << std::endl;
  
  std::cout << "\n⏳ Processing market data..." << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(2));
  
  std::cout << "\n🎯 HFT Engine Demo Complete!" << std::endl;
  std::cout << "📊 Order book has been updated with market data" << std::endl;
  std::cout << "🔧 Engine processed market data successfully" << std::endl;
  
  // Stop the engine
  engine.stop();
  
  std::cout << "\n✅ HFT Engine stopped gracefully" << std::endl;
  return 0;
}