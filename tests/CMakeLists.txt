find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(hft_tests
        test_order_book.cpp
)

target_link_libraries(hft_tests
        PRIVATE
        GTest::gtest
        GTest::gtest_main
        hft_core
)

gtest_discover_tests(hft_tests)
