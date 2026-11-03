#pragma once

#include <string>

namespace nxkit {

/// Tiny key/value cache stored as files under <data dir>/cache.
/// Each entry stores its creation time so it can expire.
class DiskCache {
public:
    static DiskCache& instance();

    void setDirectory(const std::string& directory);
    const std::string& directory() const { return dir; }

    /// Default lifetime used when get() is called with ttlSeconds < 0.
    void setDefaultTtl(int seconds) { defaultTtlSeconds = seconds; }
    int defaultTtl() const { return defaultTtlSeconds; }

    /// Returns false when the entry is missing or expired.
    bool get(const std::string& key, std::string& out, int ttlSeconds = -1);

    void put(const std::string& key, const std::string& value);
    void remove(const std::string& key);
    void clear();

private:
    DiskCache();

    std::string fileFor(const std::string& key) const;

    std::string dir;
    int defaultTtlSeconds = 300;
};

}  // namespace nxkit
