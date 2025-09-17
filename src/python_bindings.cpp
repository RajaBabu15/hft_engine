#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/chrono.h>
#include <pybind11/functional.h>

#include "hft/core/types.hpp"
#include "hft/core/clock.hpp"
#include "hft/core/lock_free_queue.hpp"
// #include "hft/core/object_pool.hpp"
#include "hft/order/order.hpp"
#include "hft/order/order_book.hpp"
#include "hft/order/price_level.hpp"

namespace py = pybind11;

PYBIND11_MODULE(hft_engine_cpp, m) {
    m.doc() = "HFT Trading Engine Python Bindings - Production Version";

    // ========================================================================
    // Core Types and Enums
    // ========================================================================
    
    py::enum_<hft::core::Side>(m, "Side")
        .value("BUY", hft::core::Side::BUY)
        .value("SELL", hft::core::Side::SELL)
        .export_values();

    py::enum_<hft::core::OrderType>(m, "OrderType")
        .value("MARKET", hft::core::OrderType::MARKET)
        .value("LIMIT", hft::core::OrderType::LIMIT)
        .value("IOC", hft::core::OrderType::IOC)
        .value("FOK", hft::core::OrderType::FOK)
        .export_values();

    py::enum_<hft::core::OrderStatus>(m, "OrderStatus")
        .value("PENDING", hft::core::OrderStatus::PENDING)
        .value("FILLED", hft::core::OrderStatus::FILLED)
        .value("PARTIALLY_FILLED", hft::core::OrderStatus::PARTIALLY_FILLED)
        .value("CANCELLED", hft::core::OrderStatus::CANCELLED)
        .value("REJECTED", hft::core::OrderStatus::REJECTED)
        .export_values();

    // ========================================================================
    // High Resolution Clock - Enhanced
    // ========================================================================
    
    py::class_<hft::core::HighResolutionClock>(m, "HighResolutionClock")
        .def_static("now", &hft::core::HighResolutionClock::now, 
                    "Get current time with high resolution")
        .def_static("rdtsc", &hft::core::HighResolutionClock::rdtsc,
                    "Get raw CPU timestamp counter value")
        .def_static("cycles_to_nanoseconds", &hft::core::HighResolutionClock::cycles_to_nanoseconds,
                    "Convert CPU cycles to nanoseconds");
                    
    // Add a helper to measure elapsed time
    m.def("measure_latency_ns", []() {
        auto start = hft::core::HighResolutionClock::now();
        return [start]() {
            auto end = hft::core::HighResolutionClock::now();
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            return duration.count();
        };
    }, "Creates a timing function that returns elapsed nanoseconds when called");

    // ========================================================================
    // Lock-Free Queue with multiple data types
    // ========================================================================
    
    py::class_<hft::core::LockFreeQueue<int, 1024>>(m, "IntLockFreeQueue")
        .def(py::init<>())
        .def("push", [](hft::core::LockFreeQueue<int, 1024>& queue, int value) -> bool {
            return queue.enqueue(std::move(value));
        })
        .def("pop", [](hft::core::LockFreeQueue<int, 1024>& queue) -> py::object {
            int value;
            if (queue.dequeue(value)) {
                return py::cast(value);
            }
            return py::none();
        })
        .def("size", &hft::core::LockFreeQueue<int, 1024>::size)
        .def("empty", &hft::core::LockFreeQueue<int, 1024>::empty);
        
    py::class_<hft::core::LockFreeQueue<double, 1024>>(m, "DoubleLockFreeQueue")
        .def(py::init<>())
        .def("push", [](hft::core::LockFreeQueue<double, 1024>& queue, double value) -> bool {
            return queue.enqueue(std::move(value));
        })
        .def("pop", [](hft::core::LockFreeQueue<double, 1024>& queue) -> py::object {
            double value;
            if (queue.dequeue(value)) {
                return py::cast(value);
            }
            return py::none();
        })
        .def("size", &hft::core::LockFreeQueue<double, 1024>::size)
        .def("empty", &hft::core::LockFreeQueue<double, 1024>::empty);
    
    // For backward compatibility
    m.attr("LockFreeQueue") = m.attr("IntLockFreeQueue");

    // ========================================================================
    // Order
    // ========================================================================
    
    py::class_<hft::order::Order>(m, "Order")
        .def(py::init<>())
        .def(py::init<hft::core::OrderID, const hft::core::Symbol&, hft::core::Side, 
                      hft::core::OrderType, hft::core::Price, hft::core::Quantity>())
        .def_readwrite("id", &hft::order::Order::id)
        .def_readwrite("symbol", &hft::order::Order::symbol)
        .def_readwrite("side", &hft::order::Order::side)
        .def_readwrite("type", &hft::order::Order::type)
        .def_readwrite("price", &hft::order::Order::price)
        .def_readwrite("quantity", &hft::order::Order::quantity)
        .def_readwrite("filled_quantity", &hft::order::Order::filled_quantity)
        .def_readwrite("status", &hft::order::Order::status)
        .def_readwrite("timestamp", &hft::order::Order::timestamp)
        .def("remaining_quantity", &hft::order::Order::remaining_quantity)
        .def("is_complete", &hft::order::Order::is_complete)
        .def("reset", &hft::order::Order::reset);

    // ========================================================================
    // Order Book
    // ========================================================================
    
    py::class_<hft::order::OrderBook>(m, "OrderBook")
        .def(py::init<const hft::core::Symbol&>())
        .def("add_order", &hft::order::OrderBook::add_order)
        .def("cancel_order", &hft::order::OrderBook::cancel_order)
        .def("get_best_bid", &hft::order::OrderBook::get_best_bid)
        .def("get_best_ask", &hft::order::OrderBook::get_best_ask)
        .def("get_mid_price", &hft::order::OrderBook::get_mid_price)
        .def("get_bid_quantity", &hft::order::OrderBook::get_bid_quantity)
        .def("get_ask_quantity", &hft::order::OrderBook::get_ask_quantity)
        .def("get_symbol", &hft::order::OrderBook::get_symbol, 
             py::return_value_policy::reference_internal)
        .def("get_bids", &hft::order::OrderBook::get_bids, py::arg("depth") = 10)
        .def("get_asks", &hft::order::OrderBook::get_asks, py::arg("depth") = 10);

    // ========================================================================
    // Price Level
    // ========================================================================
    
    py::class_<hft::order::PriceLevel>(m, "PriceLevel")
        .def(py::init<>())
        .def(py::init<hft::core::Price>())
        .def_readwrite("price", &hft::order::PriceLevel::price)
        .def_readwrite("total_quantity", &hft::order::PriceLevel::total_quantity)
        .def_readwrite("order_queue", &hft::order::PriceLevel::order_queue)
        .def("add_order", &hft::order::PriceLevel::add_order)
        .def("remove_order", &hft::order::PriceLevel::remove_order)
        .def("empty", &hft::order::PriceLevel::empty)
        .def("front_order", &hft::order::PriceLevel::front_order);
    
    // ========================================================================
    // Utility Classes
    // ========================================================================
    
    py::class_<std::vector<hft::core::Symbol>>(m, "SymbolList")
        .def(py::init<>())
        .def("push_back", (void (std::vector<hft::core::Symbol>::*)(const hft::core::Symbol&)) &std::vector<hft::core::Symbol>::push_back)
        .def("__len__", [](const std::vector<hft::core::Symbol> &v) { return v.size(); })
        .def("__iter__", [](std::vector<hft::core::Symbol> &v) {
            return py::make_iterator(v.begin(), v.end());
        }, py::keep_alive<0, 1>());
        
    // ========================================================================
    // Version and metadata
    // ========================================================================
    
    m.attr("__version__") = "2.0.0";
    m.attr("__author__") = "HFT Engine Team";
    py::dict build_info;
    build_info["timestamp"] = py::cast(std::string(__DATE__ " " __TIME__));
    build_info["compiler"] = py::cast(std::string(
    #ifdef __clang__
        "Clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__)
    #elif defined(__GNUC__)
        "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__)
    #elif defined(_MSC_VER)
        "MSVC " + std::to_string(_MSC_VER)
    #else
        "Unknown"
    #endif
    ));
    build_info["platform"] = py::cast(std::string(
    #ifdef __APPLE__
        "macOS"
    #elif defined(_WIN32) || defined(_WIN64)
        "Windows"
    #elif defined(__linux__)
        "Linux"
    #else
        "Unknown"
    #endif
    ));
    m.attr("build_info") = build_info;
}