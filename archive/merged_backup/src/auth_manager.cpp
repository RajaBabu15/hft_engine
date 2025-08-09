#include "hft/auth_manager.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <sstream>

using json = nlohmann::json;

namespace hft {

AuthManager::AuthManager()
    : credentials_loaded_(false), credentials_(nullptr) {}

AuthManager::~AuthManager() { clear_credentials(); }

bool AuthManager::load_credentials() {
  // Try environment variables first
  std::string api_key = read_from_env("BINANCE_API_KEY");
  std::string secret_key = read_from_env("BINANCE_SECRET_KEY");

  if (!api_key.empty() && !secret_key.empty()) {
    return set_credentials(api_key, secret_key);
  }

  // Fallback to config file
  return load_credentials_from_file("auth_config.json");
}

bool AuthManager::load_credentials_from_file(const std::string &config_path) {
  try {
    std::ifstream file(config_path);
    if (!file.is_open()) {
      std::cerr << "Warning: Could not open auth config file: " << config_path
                << std::endl;
      return false;
    }

    json config;
    file >> config;

    if (config.contains("api_key") && config.contains("secret_key")) {
      std::string api_key = config["api_key"];
      std::string secret_key = config["secret_key"];
      std::string passphrase = config.value("passphrase", "");

      credentials_ = std::make_unique<Credentials>();
      credentials_->api_key = api_key;
      credentials_->secret_key = secret_key;
      credentials_->passphrase = passphrase;

      credentials_loaded_ = validate_credentials();
      return credentials_loaded_;
    }
  } catch (const std::exception &e) {
    std::cerr << "Error loading credentials: " << e.what() << std::endl;
    return false;
  }

  return false;
}

bool AuthManager::set_credentials(const std::string &api_key,
                                  const std::string &secret_key) {
  if (!is_valid_api_key(api_key) || !is_valid_secret(secret_key)) {
    return false;
  }

  credentials_ = std::make_unique<Credentials>();
  credentials_->api_key = api_key;
  credentials_->secret_key = secret_key;
  credentials_->passphrase = "";

  credentials_loaded_ = true;
  return true;
}

bool AuthManager::validate_credentials() const {
  if (!credentials_) {
    return false;
  }

  return is_valid_api_key(credentials_->api_key) &&
         is_valid_secret(credentials_->secret_key);
}

std::optional<AuthManager::Credentials> AuthManager::get_credentials() const {
  if (!credentials_loaded_ || !credentials_) {
    return std::nullopt;
  }

  return *credentials_;
}

std::string AuthManager::generate_signature(const std::string &query_string,
                                            const std::string &secret) const {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  unsigned int digest_len = SHA256_DIGEST_LENGTH;

  HMAC(EVP_sha256(), secret.c_str(), secret.length(),
       reinterpret_cast<const unsigned char *>(query_string.c_str()),
       query_string.length(), digest, &digest_len);

  std::stringstream ss;
  for (unsigned int i = 0; i < digest_len; ++i) {
    ss << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<int>(digest[i]);
  }

  return ss.str();
}

std::string AuthManager::get_timestamp() const {
  auto now = std::chrono::system_clock::now();
  auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                       now.time_since_epoch())
                       .count();
  return std::to_string(timestamp);
}

void AuthManager::clear_credentials() {
  if (credentials_) {
    // Clear sensitive data
    credentials_->api_key.clear();
    credentials_->secret_key.clear();
    credentials_->passphrase.clear();
    credentials_.reset();
  }
  credentials_loaded_ = false;
}

bool AuthManager::test_connection() const {
  if (!credentials_loaded_) {
    return false;
  }

  // For now, just validate format. In real implementation,
  // this would make a test API call to verify credentials
  return validate_credentials();
}

bool AuthManager::is_valid_api_key(const std::string &key) const {
  // Binance API keys are typically 64 characters long and alphanumeric
  if (key.length() < 20 || key.length() > 128) {
    return false;
  }

  // Check if it contains only valid characters
  for (char c : key) {
    if (!std::isalnum(c)) {
      return false;
    }
  }

  return true;
}

bool AuthManager::is_valid_secret(const std::string &secret) const {
  // Similar validation for secret key
  if (secret.length() < 20 || secret.length() > 128) {
    return false;
  }

  for (char c : secret) {
    if (!std::isalnum(c) && c != '+' && c != '/' && c != '=') {
      return false;
    }
  }

  return true;
}

std::string AuthManager::read_from_env(const std::string &var_name) const {
  const char *value = std::getenv(var_name.c_str());
  return value ? std::string(value) : "";
}

} // namespace hft
