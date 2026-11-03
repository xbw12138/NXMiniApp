#include "nxkit/config.hpp"

#include <fstream>

#include "nxkit/paths.hpp"

namespace nxkit {

namespace {

const nlohmann::json* findValue(const nlohmann::json& root, const std::string& key) {
    const nlohmann::json* current = &root;
    size_t start = 0;
    while (true) {
        size_t pos = key.find('.', start);
        std::string part = key.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
        if (!current->is_object()) return nullptr;
        auto it = current->find(part);
        if (it == current->end()) return nullptr;
        current = &(*it);
        if (pos == std::string::npos) return current;
        start = pos + 1;
    }
}

nlohmann::json& ensureValue(nlohmann::json& root, const std::string& key) {
    nlohmann::json* current = &root;
    size_t start = 0;
    while (true) {
        size_t pos = key.find('.', start);
        std::string part = key.substr(start, pos == std::string::npos ? std::string::npos : pos - start);
        if (!current->is_object()) *current = nlohmann::json::object();
        current = &(*current)[part];
        if (pos == std::string::npos) return *current;
        start = pos + 1;
    }
}

std::string parentDirectory(const std::string& path) {
    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) return "";
    return path.substr(0, pos);
}

}  // namespace

Config& Config::instance() {
    static Config config;
    return config;
}

void Config::load(const std::string& path) {
    filePath = path.empty() ? dataPath("config.json") : path;

    std::ifstream file(filePath);
    if (!file.is_open()) {
        root = nlohmann::json::object();
        return;
    }

    nlohmann::json parsed = nlohmann::json::parse(file, nullptr, false);
    root = parsed.is_discarded() || !parsed.is_object() ? nlohmann::json::object() : parsed;
}

void Config::save() {
    if (filePath.empty()) filePath = dataPath("config.json");
    ensureDirectory(parentDirectory(filePath));

    std::ofstream file(filePath, std::ios::trunc);
    if (!file.is_open()) return;
    file << root.dump(2);
}

bool Config::has(const std::string& key) const { return findValue(root, key) != nullptr; }

std::string Config::getString(const std::string& key, const std::string& defaultValue) const {
    const nlohmann::json* value = findValue(root, key);
    if (!value || !value->is_string()) return defaultValue;
    return value->get<std::string>();
}

int Config::getInt(const std::string& key, int defaultValue) const {
    const nlohmann::json* value = findValue(root, key);
    if (!value || !value->is_number_integer()) return defaultValue;
    return value->get<int>();
}

bool Config::getBool(const std::string& key, bool defaultValue) const {
    const nlohmann::json* value = findValue(root, key);
    if (!value || !value->is_boolean()) return defaultValue;
    return value->get<bool>();
}

double Config::getDouble(const std::string& key, double defaultValue) const {
    const nlohmann::json* value = findValue(root, key);
    if (!value || !value->is_number()) return defaultValue;
    return value->get<double>();
}

void Config::setString(const std::string& key, const std::string& value) {
    ensureValue(root, key) = value;
}

void Config::setInt(const std::string& key, int value) { ensureValue(root, key) = value; }

void Config::setBool(const std::string& key, bool value) { ensureValue(root, key) = value; }

void Config::setDouble(const std::string& key, double value) { ensureValue(root, key) = value; }

void Config::setValue(const std::string& key, const nlohmann::json& value) {
    ensureValue(root, key) = value;
}

void Config::remove(const std::string& key) {
    size_t pos = key.find_last_of('.');
    if (pos == std::string::npos) {
        root.erase(key);
        return;
    }
    nlohmann::json* parent = const_cast<nlohmann::json*>(findValue(root, key.substr(0, pos)));
    if (parent && parent->is_object()) parent->erase(key.substr(pos + 1));
}

}  // namespace nxkit
