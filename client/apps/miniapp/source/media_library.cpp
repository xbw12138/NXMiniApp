//
// miniapp: 原生媒体库索引实现。
//

#include "miniapp/media_library.hpp"

#include <borealis/core/logger.hpp>
#include <nxkit/paths.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <utility>

#include "miniapp/json.hpp"
#include "miniapp/smb_manager.hpp"

namespace miniapp {

namespace {

struct DirNode {
    uint32_t mtime = 0;
    std::vector<std::pair<std::string, uint64_t>> files;
    // 同目录中的字幕文件（完整路径），用于给视频配对 sidecar 字幕。
    std::vector<std::string> subtitleFiles;
    // 子目录名（相对当前路径）。
    std::vector<std::string> subs;
};

using DirMap = std::map<std::string, DirNode>;

std::string baseName(const std::string& path) {
    size_t pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

std::string lowerAscii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

bool isVideoFile(const std::string& path) {
    const std::string name = baseName(path);
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    const std::string ext = lowerAscii(name.substr(dot + 1));
    static const char* kExts[] = {"mp4", "mkv",  "avi",  "mov",  "wmv",   "flv", "ts",
                                  "m2ts", "webm", "mpg",  "mpeg", "rmvb", "rm",  "3gp",
                                  "m3u8", "vob",  "iso",  "m4v",  "divx", "asf", "f4v"};
    for (const char* candidate : kExts) {
        if (ext == candidate) return true;
    }
    return false;
}

bool isSubtitleFile(const std::string& path) {
    const std::string name = baseName(path);
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    const std::string ext = lowerAscii(name.substr(dot + 1));
    return ext == "srt" || ext == "ass" || ext == "ssa" || ext == "vtt";
}

/// 去掉目录与扩展名的文件名（小写），用于字幕配对。
std::string stemKey(const std::string& path) {
    std::string name = lowerAscii(baseName(path));
    size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

// 系统/回收站目录永远不含用户影片，直接跳过可省下大量网络往返。
bool isIgnoredDir(const std::string& path) {
    const std::string name = lowerAscii(baseName(path));
    return name == "$recycle.bin" || name == "system volume information" ||
           name == "#recycle" || name == "@eadir" || name == ".recycle";
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) return false;
    out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return true;
}

void writeFile(const std::string& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.good()) return;
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

DirMap loadIndex(const std::string& path, uint64_t& updated) {
    DirMap dirs;
    updated = 0;
    std::string text;
    if (!readFile(path, text) || text.empty()) return dirs;
    JsonValue root = parseJson(text);
    if (!root.isObject()) return dirs;
    if (const JsonValue* upd = root.find("updated")) updated = static_cast<uint64_t>(upd->asNumber());
    const JsonValue* dirsValue = root.find("dirs");
    if (!dirsValue || !dirsValue->isObject()) return dirs;
    for (const auto& kv : dirsValue->asObject()) {
        const JsonValue& node = kv.second;
        if (!node.isObject()) continue;
        DirNode out;
        if (const JsonValue* mt = node.find("mtime")) out.mtime = static_cast<uint32_t>(mt->asNumber());
        if (const JsonValue* files = node.find("files")) {
            for (const auto& item : files->asArray()) {
                if (!item.isArray() || item.size() < 2) continue;
                out.files.emplace_back(item.at(0).asString(),
                                       static_cast<uint64_t>(item.at(1).asNumber()));
            }
        }
        if (const JsonValue* subtitles = node.find("subtitles")) {
            for (const auto& item : subtitles->asArray()) {
                out.subtitleFiles.push_back(item.asString());
            }
        }
        if (const JsonValue* subs = node.find("subs")) {
            for (const auto& sub : subs->asArray()) out.subs.push_back(sub.asString());
        }
        dirs[kv.first] = std::move(out);
    }
    return dirs;
}

std::string dumpIndex(const DirMap& dirs, uint64_t updated) {
    JsonValue root = JsonValue::object();
    root.setMember("version", JsonValue::number(1));
    root.setMember("updated", JsonValue::number(static_cast<double>(updated)));
    JsonValue dirsValue = JsonValue::object();
    for (const auto& kv : dirs) {
        JsonValue node = JsonValue::object();
        node.setMember("mtime", JsonValue::number(static_cast<double>(kv.second.mtime)));
        std::vector<JsonValue> fileValues;
        fileValues.reserve(kv.second.files.size());
        for (const auto& file : kv.second.files) {
            std::vector<JsonValue> pair;
            pair.push_back(JsonValue::string(file.first));
            pair.push_back(JsonValue::number(static_cast<double>(file.second)));
            fileValues.push_back(JsonValue::array(std::move(pair)));
        }
        node.setMember("files", JsonValue::array(std::move(fileValues)));
        std::vector<JsonValue> subtitleValues;
        subtitleValues.reserve(kv.second.subtitleFiles.size());
        for (const auto& subtitle : kv.second.subtitleFiles) {
            subtitleValues.push_back(JsonValue::string(subtitle));
        }
        node.setMember("subtitles", JsonValue::array(std::move(subtitleValues)));
        std::vector<JsonValue> subValues;
        subValues.reserve(kv.second.subs.size());
        for (const auto& sub : kv.second.subs) subValues.push_back(JsonValue::string(sub));
        node.setMember("subs", JsonValue::array(std::move(subValues)));
        dirsValue.setMember(kv.first, std::move(node));
    }
    root.setMember("dirs", std::move(dirsValue));
    return toJsonString(root);
}

uint64_t nowMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

}  // namespace

MediaLibrary& MediaLibrary::instance() {
    static MediaLibrary library;
    return library;
}

std::string MediaLibrary::indexPath(const std::string& sourceId) const {
    std::string safe;
    safe.reserve(sourceId.size());
    for (char c : sourceId) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-') {
            safe.push_back(c);
        }
    }
    if (safe.empty()) safe = "default";
    return nxkit::dataPath("miniapp/media") + "/" + safe + ".json";
}

int MediaLibrary::scanSmb(const std::string& sourceId, const std::string& smbId,
                          const std::string& root, int maxDepth, int maxFiles,
                          std::string& error) {
    if (maxDepth < 1) maxDepth = 1;
    if (maxFiles < 1) maxFiles = 1;

    std::string path = indexPath(sourceId);
    uint64_t updated = 0;
    DirMap cached = loadIndex(path, updated);

    DirMap out;
    struct Node {
        std::string path;
        int depth;
    };
    std::vector<Node> stack;
    stack.push_back({root.empty() ? "/" : root, 0});
    std::set<std::string> visited;
    int scanned = 0;
    int skipped = 0;
    int files = 0;

    while (!stack.empty()) {
        Node node = stack.back();
        stack.pop_back();
        if (!visited.insert(node.path).second) continue;

        std::vector<SmbEntry> entries;
        uint32_t mtime = 0;
        std::string listError;
        bool listed = SmbManager::instance().listDirMeta(smbId, node.path, entries, mtime, listError);
        auto cachedIt = cached.find(node.path);
        // 目录未变化：直接复用旧索引，连列目录都省掉。
        if (cachedIt != cached.end() && mtime != 0 && cachedIt->second.mtime == mtime) {
            DirNode nodeOut = cachedIt->second;
            files += static_cast<int>(nodeOut.files.size());
            for (const auto& sub : nodeOut.subs) {
                if (node.depth + 1 <= maxDepth) stack.push_back({sub, node.depth + 1});
            }
            out[node.path] = std::move(nodeOut);
            ++skipped;
            continue;
        }
        if (!listed) {
            // 列目录失败（网络抖动/会话断开）：保留旧索引，避免一次失败清空整库。
            if (cachedIt != cached.end()) {
                DirNode nodeOut = cachedIt->second;
                files += static_cast<int>(nodeOut.files.size());
                for (const auto& sub : nodeOut.subs) {
                    if (node.depth + 1 <= maxDepth) stack.push_back({sub, node.depth + 1});
                }
                out[node.path] = std::move(nodeOut);
            }
            continue;
        }

        DirNode nodeOut;
        nodeOut.mtime = mtime;
        for (const auto& entry : entries) {
            if (entry.isDir) {
                if (isIgnoredDir(entry.name)) continue;
                if (node.depth + 1 <= maxDepth) {
                    nodeOut.subs.push_back(entry.name);
                    stack.push_back({entry.name, node.depth + 1});
                }
            } else if (isVideoFile(entry.name) &&
                       files + static_cast<int>(nodeOut.files.size()) < maxFiles) {
                nodeOut.files.emplace_back(entry.name, entry.size);
            } else if (isSubtitleFile(entry.name)) {
                nodeOut.subtitleFiles.push_back(entry.name);
            }
        }
        files += static_cast<int>(nodeOut.files.size());
        out[node.path] = std::move(nodeOut);
        ++scanned;
        if (files >= maxFiles) break;
    }

    if (out.empty()) {
        error = "SMB 扫描失败";
        return 0;
    }

    uint64_t stamp = nowMs();
    std::string text = dumpIndex(out, stamp);
    {
        // 扫描可能来自多个页面，串行写盘并用临时文件保证原子性。
        std::lock_guard<std::mutex> lock(mutex_);
        nxkit::ensureDirectory(nxkit::dataPath("miniapp/media"));
        const std::string tmp = path + ".tmp";
        writeFile(tmp, text);
        std::remove(path.c_str());
        std::rename(tmp.c_str(), path.c_str());
    }
    brls::Logger::info("media scan source={} dirs={} skipped={} files={} bytes={}", sourceId,
                       scanned, skipped, files, text.size());
    return files;
}

std::vector<MediaEntry> MediaLibrary::page(const std::string& sourceId, int offset, int limit,
                                           int& total, std::string& error) {
    total = 0;
    std::uint64_t updated = 0;
    DirMap dirs = loadIndex(indexPath(sourceId), updated);
    if (dirs.empty()) {
        error = "媒体库为空，请先扫描";
        return {};
    }
    std::vector<MediaEntry> all;
    for (const auto& kv : dirs) {
        // 同目录内按「去扩展名、小写」配对 sidecar 字幕。
        std::map<std::string, std::string> subtitles;
        for (const auto& subtitle : kv.second.subtitleFiles) {
            subtitles[stemKey(subtitle)] = subtitle;
        }
        for (const auto& file : kv.second.files) {
            MediaEntry entry;
            entry.path = file.first;
            entry.size = file.second;
            auto match = subtitles.find(stemKey(file.first));
            if (match != subtitles.end()) entry.subtitle = match->second;
            all.push_back(std::move(entry));
        }
    }
    std::sort(all.begin(), all.end(),
              [](const MediaEntry& a, const MediaEntry& b) { return a.path < b.path; });
    total = static_cast<int>(all.size());
    if (offset < 0) offset = 0;
    if (limit <= 0) limit = total;
    if (offset >= total) return {};
    int end = std::min(total, offset + limit);
    return std::vector<MediaEntry>(all.begin() + offset, all.begin() + end);
}

}  // namespace miniapp
