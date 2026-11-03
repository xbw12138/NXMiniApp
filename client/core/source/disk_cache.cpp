#include "nxkit/disk_cache.hpp"

#include <dirent.h>
#include <unistd.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <functional>
#include <sstream>

#include "nxkit/paths.hpp"

namespace nxkit {

DiskCache::DiskCache() : dir(dataPath("cache")) {}

DiskCache& DiskCache::instance() {
    static DiskCache cache;
    return cache;
}

void DiskCache::setDirectory(const std::string& directory) {
    dir = directory;
    ensureDirectory(dir);
}

std::string DiskCache::fileFor(const std::string& key) const {
    std::ostringstream name;
    name << std::hex << std::hash<std::string>{}(key);
    return dir + "/" + name.str() + ".cache";
}

bool DiskCache::get(const std::string& key, std::string& out, int ttlSeconds) {
    std::ifstream file(fileFor(key), std::ios::binary);
    if (!file.is_open()) return false;

    std::string line;
    if (!std::getline(file, line)) return false;

    long storedTime = 0;
    try {
        storedTime = std::stol(line);
    } catch (...) {
        return false;
    }

    int ttl = ttlSeconds < 0 ? defaultTtlSeconds : ttlSeconds;
    if (ttl > 0 && static_cast<long>(std::time(nullptr)) - storedTime > ttl) {
        remove(key);
        return false;
    }

    std::ostringstream body;
    body << file.rdbuf();
    out = body.str();
    return true;
}

void DiskCache::put(const std::string& key, const std::string& value) {
    ensureDirectory(dir);
    std::ofstream file(fileFor(key), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return;
    file << static_cast<long>(std::time(nullptr)) << "\n" << value;
}

void DiskCache::remove(const std::string& key) { std::remove(fileFor(key).c_str()); }

void DiskCache::clear() {
    DIR* handle = opendir(dir.c_str());
    if (!handle) return;

    while (dirent* entry = readdir(handle)) {
        std::string name = entry->d_name;
        if (name.size() < 6 || name.substr(name.size() - 6) != ".cache") continue;
        std::remove((dir + "/" + name).c_str());
    }
    closedir(handle);
}

}  // namespace nxkit
