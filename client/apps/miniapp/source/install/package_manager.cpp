//
// miniapp: package download cache and installer.
//

#include "miniapp/install/package_manager.hpp"

#include "nxkit/paths.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

namespace miniapp {
namespace packages {

namespace {

std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    if (dir.back() == '/') return dir + name;
    return dir + "/" + name;
}

std::string parentOf(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

bool writeFile(const std::string& path, const char* data, size_t size) {
    nxkit::ensureDirectory(parentOf(path));
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(data, static_cast<std::streamsize>(size));
    return file.good();
}

void removeTree(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        unlink(path.c_str());
        return;
    }
    while (auto* entry = readdir(dir)) {
        if (std::string(entry->d_name) == "." || std::string(entry->d_name) == "..") continue;
        std::string child = joinPath(path, entry->d_name);
        struct stat info {};
        if (stat(child.c_str(), &info) == 0 && S_ISDIR(info.st_mode)) {
            removeTree(child);
        } else {
            unlink(child.c_str());
        }
    }
    closedir(dir);
    rmdir(path.c_str());
}

uint16_t readU16(const std::string& data, size_t offset) {
    return static_cast<uint16_t>(static_cast<unsigned char>(data[offset])) |
           (static_cast<uint16_t>(static_cast<unsigned char>(data[offset + 1])) << 8);
}

uint32_t readU32(const std::string& data, size_t offset) {
    return static_cast<uint32_t>(static_cast<unsigned char>(data[offset])) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[offset + 1])) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[offset + 2])) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(data[offset + 3])) << 24);
}

bool safeEntryName(std::string& name, std::vector<std::string>& parts) {
    for (char& c : name) {
        if (c == '\\') c = '/';
    }
    if (name.empty() || name[0] == '/') return false;

    parts.clear();
    std::stringstream stream(name);
    std::string part;
    while (std::getline(stream, part, '/')) {
        if (part.empty() || part == ".") continue;
        if (part == "..") return false;
        parts.push_back(part);
    }
    return !parts.empty();
}

/// Unpacks the ZIP_STORED archive produced by the store backend.
bool unpack(const std::string& archive, const std::string& target, std::string& error) {
    size_t position = 0;
    size_t files = 0;

    while (position + 4 <= archive.size()) {
        uint32_t signature = readU32(archive, position);
        if (signature == 0x02014b50 || signature == 0x06054b50) break;
        if (signature != 0x04034b50) {
            error = "不是合法的 .nxpkg";
            return false;
        }
        if (position + 30 > archive.size()) {
            error = "安装包目录损坏";
            return false;
        }

        uint16_t flags = readU16(archive, position + 6);
        uint16_t method = readU16(archive, position + 8);
        uint32_t compressed = readU32(archive, position + 18);
        uint32_t uncompressed = readU32(archive, position + 22);
        uint16_t nameLength = readU16(archive, position + 26);
        uint16_t extraLength = readU16(archive, position + 28);

        if (position + 30 + nameLength + extraLength + compressed > archive.size()) {
            error = "安装包数据不完整";
            return false;
        }
        if ((flags & 0x0008) != 0) {
            error = "不支持带数据描述符的安装包";
            return false;
        }
        if (method != 0 || compressed != uncompressed) {
            error = "安装包必须使用 ZIP_STORED";
            return false;
        }

        std::string name = archive.substr(position + 30, nameLength);
        size_t dataStart = position + 30 + nameLength + extraLength;
        position = dataStart + compressed;

        std::vector<std::string> parts;
        if (!safeEntryName(name, parts)) {
            error = "安装包包含非法路径: " + name;
            return false;
        }

        std::string relative;
        for (const auto& part : parts) {
            if (!relative.empty()) relative += "/";
            relative += part;
        }
        std::string full = joinPath(target, relative);

        bool isDirectory = !name.empty() && name.back() == '/';
        if (isDirectory) {
            nxkit::ensureDirectory(full);
            continue;
        }
        if (!writeFile(full, archive.data() + dataStart, compressed)) {
            error = "写入失败: " + relative;
            return false;
        }
        ++files;
    }

    if (files == 0) {
        error = "安装包为空";
        return false;
    }
    return true;
}

}  // namespace

std::string root() {
    std::string dir = nxkit::dataPath("miniapps/");
    nxkit::ensureDirectory(dir);
    return dir;
}

std::string directory(const std::string& id) {
    return joinPath(root(), id);
}

bool installed(const std::string& id, const std::string& version) {
    std::string dir = directory(id);
    std::ifstream appFile(joinPath(dir, "app.json"), std::ios::binary);
    if (!appFile) return false;
    if (version.empty()) return true;

    std::ifstream versionFile(joinPath(dir, ".version"), std::ios::binary);
    if (!versionFile) return false;
    std::string current;
    std::getline(versionFile, current);
    return current == version;
}

bool install(const std::string& id, const std::string& version, const std::string& archive,
             std::string& error) {
    if (id.empty()) {
        error = "缺少小程序 ID";
        return false;
    }

    std::string finalDir = directory(id);
    std::string tempDir = finalDir + ".tmp";
    removeTree(tempDir);
    nxkit::ensureDirectory(tempDir);

    if (!unpack(archive, tempDir, error)) {
        removeTree(tempDir);
        return false;
    }

    std::ifstream appFile(joinPath(tempDir, "app.json"), std::ios::binary);
    if (!appFile) {
        error = "安装包缺少 app.json";
        removeTree(tempDir);
        return false;
    }
    appFile.close();

    std::ofstream versionFile(joinPath(tempDir, ".version"), std::ios::binary | std::ios::trunc);
    if (!versionFile) {
        error = "无法写入版本标记";
        removeTree(tempDir);
        return false;
    }
    versionFile << version;
    versionFile.close();

    removeTree(finalDir);
    if (rename(tempDir.c_str(), finalDir.c_str()) != 0) {
        error = "安装目录替换失败";
        removeTree(tempDir);
        return false;
    }
    return true;
}

}  // namespace packages
}  // namespace miniapp
