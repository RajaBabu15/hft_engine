#pragma once
#include <memory>
#include <optional>
#include <string>

namespace hft {
class AuthManager {
public:
  struct Credentials {
    std::string api_key;
    std::string secret_key;
    std::string passphrase; // For some exchanges
  };

  AuthManager();
  ~AuthManager();

  // Load credentials from environment variables or config file
  bool load_credentials();
  bool load_credentials_from_file(const std::string &config_path);
  bool set_credentials(const std::string &api_key,
                       const std::string &secret_key);

  // Validate credentials format
  bool validate_credentials() const;

  // Get credentials (returns nullopt if not loaded)
  std::optional<Credentials> get_credentials() const;

  // Generate HMAC signature for API requests
  std::string generate_signature(const std::string &query_string,
                                 const std::string &secret) const;

  // Generate timestamp for API requests
  std::string get_timestamp() const;

  // Clear credentials from memory
  void clear_credentials();

  // Test connection with current credentials
  bool test_connection() const;

private:
  bool credentials_loaded_;
  std::unique_ptr<Credentials> credentials_;

  // Helper methods
  bool is_valid_api_key(const std::string &key) const;
  bool is_valid_secret(const std::string &secret) const;
  std::string read_from_env(const std::string &var_name) const;
};
} // namespace hft
