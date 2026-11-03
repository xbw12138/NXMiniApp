#include "nxkit/auth_store.hpp"

#include "nxkit/config.hpp"

namespace nxkit {

AuthStore& AuthStore::instance() {
    static AuthStore store;
    return store;
}

std::string AuthStore::get(const std::string& key, const std::string& defaultValue) const {
    return Config::instance().getString("auth." + key, defaultValue);
}

void AuthStore::set(const std::string& key, const std::string& value) {
    Config::instance().setString("auth." + key, value);
    Config::instance().save();
}

void AuthStore::remove(const std::string& key) {
    Config::instance().remove("auth." + key);
    Config::instance().save();
}

bool AuthStore::isLoggedIn() const { return !cookie().empty() || !token().empty(); }

void AuthStore::clear() {
    Config::instance().remove("auth");
    Config::instance().save();
}

}  // namespace nxkit
