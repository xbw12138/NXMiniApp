#pragma once

#include <map>
#include <string>

namespace nxkit {

/// Persists login state (cookies, tokens, user ids, ...) inside Config.
/// Values are stored under "auth.<key>" so they survive restarts.
class AuthStore {
public:
    static AuthStore& instance();

    std::string get(const std::string& key, const std::string& defaultValue = "") const;
    void set(const std::string& key, const std::string& value);
    void remove(const std::string& key);

    bool isLoggedIn() const;
    void clear();

    /// Convenience accessors used by most apps.
    std::string cookie() const { return get("cookie"); }
    std::string token() const { return get("token"); }

private:
    AuthStore() = default;
};

}  // namespace nxkit
