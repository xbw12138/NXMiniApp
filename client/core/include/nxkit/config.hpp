#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace nxkit {

/// Small JSON backed key/value store persisted on disk.
///
/// Keys may be nested with dots, e.g. "api.baseUrl" or "auth.cookie".
class Config {
public:
    static Config& instance();

    /// Loads the config file. An empty path uses <data dir>/config.json.
    void load(const std::string& path = "");

    /// Writes the in-memory values back to disk.
    void save();

    const std::string& path() const { return filePath; }

    nlohmann::json& data() { return root; }
    const nlohmann::json& data() const { return root; }

    bool has(const std::string& key) const;

    std::string getString(const std::string& key, const std::string& defaultValue = "") const;
    int getInt(const std::string& key, int defaultValue = 0) const;
    bool getBool(const std::string& key, bool defaultValue = false) const;
    double getDouble(const std::string& key, double defaultValue = 0) const;

    void setString(const std::string& key, const std::string& value);
    void setInt(const std::string& key, int value);
    void setBool(const std::string& key, bool value);
    void setDouble(const std::string& key, double value);
    void setValue(const std::string& key, const nlohmann::json& value);

    void remove(const std::string& key);

private:
    Config() = default;

    nlohmann::json root = nlohmann::json::object();
    std::string filePath;
};

}  // namespace nxkit
