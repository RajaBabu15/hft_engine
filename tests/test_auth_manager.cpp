#include <gtest/gtest.h>
#include "hft/auth_manager.h"
#include <fstream>
#include <cstdlib>
#include <thread>
#include <chrono>

class AuthManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        auth_manager = std::make_unique<hft::AuthManager>();
        
        // Clean environment variables
#ifdef _WIN32
        _putenv("BINANCE_API_KEY=");
        _putenv("BINANCE_SECRET_KEY=");
#else
        unsetenv("BINANCE_API_KEY");
        unsetenv("BINANCE_SECRET_KEY");
#endif
        
        // Create test config file
        test_config_file = "test_auth_config.json";
        std::ofstream file(test_config_file);
        file << R"({
            "api_key": "testApiKey12345678901234567890123456789012345678901234567890",
            "secret_key": "testSecretKey123456789012345678901234567890123456789012345"
        })";
        file.close();
        
        invalid_config_file = "invalid_auth_config.json";
        std::ofstream invalid_file(invalid_config_file);
        invalid_file << R"({
            "api_key": "short",
            "secret_key": "alsoshort"
        })";
        invalid_file.close();
    }

    void TearDown() override {
        // Clean up test files
        std::remove(test_config_file.c_str());
        std::remove(invalid_config_file.c_str());
    }

    std::unique_ptr<hft::AuthManager> auth_manager;
    std::string test_config_file;
    std::string invalid_config_file;
};

TEST_F(AuthManagerTest, InitialState) {
    EXPECT_FALSE(auth_manager->validate_credentials());
    EXPECT_FALSE(auth_manager->get_credentials().has_value());
    EXPECT_FALSE(auth_manager->test_connection());
}

TEST_F(AuthManagerTest, SetValidCredentials) {
    std::string valid_api_key = "testApiKey12345678901234567890123456789012345678901234567890";
    std::string valid_secret = "testSecretKey123456789012345678901234567890123456789012345";
    
    EXPECT_TRUE(auth_manager->set_credentials(valid_api_key, valid_secret));
    EXPECT_TRUE(auth_manager->validate_credentials());
    
    auto credentials = auth_manager->get_credentials();
    ASSERT_TRUE(credentials.has_value());
    EXPECT_EQ(credentials->api_key, valid_api_key);
    EXPECT_EQ(credentials->secret_key, valid_secret);
}

TEST_F(AuthManagerTest, SetInvalidCredentials) {
    // Test short API key
    EXPECT_FALSE(auth_manager->set_credentials("short", "validSecretKey123456789012345678901234567890123456789012345"));
    
    // Test short secret key
    EXPECT_FALSE(auth_manager->set_credentials("validApiKey12345678901234567890123456789012345678901234567890", "short"));
    
    // Test invalid characters
    EXPECT_FALSE(auth_manager->set_credentials("invalid@key#", "validSecretKey123456789012345678901234567890123456789012345"));
}

TEST_F(AuthManagerTest, LoadFromValidConfigFile) {
    EXPECT_TRUE(auth_manager->load_credentials_from_file(test_config_file));
    EXPECT_TRUE(auth_manager->validate_credentials());
    
    auto credentials = auth_manager->get_credentials();
    ASSERT_TRUE(credentials.has_value());
    EXPECT_EQ(credentials->api_key, "testApiKey12345678901234567890123456789012345678901234567890");
    EXPECT_EQ(credentials->secret_key, "testSecretKey123456789012345678901234567890123456789012345");
}

TEST_F(AuthManagerTest, LoadFromInvalidConfigFile) {
    EXPECT_FALSE(auth_manager->load_credentials_from_file(invalid_config_file));
    EXPECT_FALSE(auth_manager->validate_credentials());
}

TEST_F(AuthManagerTest, LoadFromNonexistentFile) {
    EXPECT_FALSE(auth_manager->load_credentials_from_file("nonexistent.json"));
    EXPECT_FALSE(auth_manager->validate_credentials());
}

TEST_F(AuthManagerTest, GenerateSignature) {
    std::string query_string = "symbol=BTCUSDT&side=BUY&type=LIMIT&timeInForce=GTC&quantity=1&price=9000&recvWindow=5000&timestamp=1499827319559";
    std::string secret = "NhqPtmdSJYdKjVHjA7PZj4Mge3R5YNiP1e3UZjInClVN65XAbvqqM6A7H5fATj0j";
    
    std::string signature = auth_manager->generate_signature(query_string, secret);
    EXPECT_FALSE(signature.empty());
    EXPECT_EQ(signature.length(), 64); // SHA256 hex string length
    
    // Test consistency
    std::string signature2 = auth_manager->generate_signature(query_string, secret);
    EXPECT_EQ(signature, signature2);
}

TEST_F(AuthManagerTest, GetTimestamp) {
    std::string timestamp1 = auth_manager->get_timestamp();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    std::string timestamp2 = auth_manager->get_timestamp();
    
    EXPECT_FALSE(timestamp1.empty());
    EXPECT_FALSE(timestamp2.empty());
    EXPECT_NE(timestamp1, timestamp2);
    
    // Verify timestamp is numeric
    EXPECT_NO_THROW(std::stoull(timestamp1));
    EXPECT_NO_THROW(std::stoull(timestamp2));
}

TEST_F(AuthManagerTest, ClearCredentials) {
    // Set credentials first
    auth_manager->set_credentials(
        "testApiKey12345678901234567890123456789012345678901234567890",
        "testSecretKey123456789012345678901234567890123456789012345"
    );
    EXPECT_TRUE(auth_manager->validate_credentials());
    
    // Clear credentials
    auth_manager->clear_credentials();
    EXPECT_FALSE(auth_manager->validate_credentials());
    EXPECT_FALSE(auth_manager->get_credentials().has_value());
}

TEST_F(AuthManagerTest, TestConnection) {
    // Without credentials
    EXPECT_FALSE(auth_manager->test_connection());
    
    // With valid credentials
    auth_manager->set_credentials(
        "testApiKey12345678901234567890123456789012345678901234567890",
        "testSecretKey123456789012345678901234567890123456789012345"
    );
    EXPECT_TRUE(auth_manager->test_connection());
}

TEST_F(AuthManagerTest, LoadFromEnvironmentVariables) {
    // Set environment variables
#ifdef _WIN32
    _putenv("BINANCE_API_KEY=envApiKey12345678901234567890123456789012345678901234567890");
    _putenv("BINANCE_SECRET_KEY=envSecretKey123456789012345678901234567890123456789012345");
#else
    setenv("BINANCE_API_KEY", "envApiKey12345678901234567890123456789012345678901234567890", 1);
    setenv("BINANCE_SECRET_KEY", "envSecretKey123456789012345678901234567890123456789012345", 1);
#endif
    
    EXPECT_TRUE(auth_manager->load_credentials());
    EXPECT_TRUE(auth_manager->validate_credentials());
    
    auto credentials = auth_manager->get_credentials();
    ASSERT_TRUE(credentials.has_value());
    EXPECT_EQ(credentials->api_key, "envApiKey12345678901234567890123456789012345678901234567890");
    EXPECT_EQ(credentials->secret_key, "envSecretKey123456789012345678901234567890123456789012345");
    
    // Clean up
    unsetenv("BINANCE_API_KEY");
    unsetenv("BINANCE_SECRET_KEY");
}

TEST_F(AuthManagerTest, LoadCredentialsFallback) {
    // No environment variables, should fallback to config file
    EXPECT_TRUE(auth_manager->load_credentials_from_file(test_config_file));
    
    // Create auth_config.json for the load_credentials() method
    std::ofstream file("auth_config.json");
    file << R"({
        "api_key": "configApiKey12345678901234567890123456789012345678901234567890",
        "secret_key": "configSecretKey123456789012345678901234567890123456789012345"
    })";
    file.close();
    
    // Clear current credentials
    auth_manager->clear_credentials();
    
    // Load should fallback to config file
    EXPECT_TRUE(auth_manager->load_credentials());
    
    // Clean up
    std::remove("auth_config.json");
}
