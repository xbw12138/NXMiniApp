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
    // 同目录中的图片（完整路径）：同名的给单个视频当封面，标准命名的给整个目录当封面。
    std::vector<std::string> imageFiles;
    std::string poster;
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

bool isImageFile(const std::string& path) {
    const std::string name = baseName(path);
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return false;
    const std::string ext = lowerAscii(name.substr(dot + 1));
    return ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "webp" || ext == "bmp";
}

// 目录级封面的常见命名：folder.jpg / poster.jpg / cover.png …
bool isPosterName(const std::string& path) {
    std::string name = lowerAscii(baseName(path));
    size_t dot = name.find_last_of('.');
    const std::string stem = dot == std::string::npos ? name : name.substr(0, dot);
    return stem == "folder" || stem == "poster" || stem == "cover" || stem == "fanart" ||
           stem == "thumb" || stem == "landscape" || stem == "backdrop" || stem == "banner";
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

// 扫描根目录统一成 "/a/b" 形式（空值表示共享根）。
std::string normalizeRoot(const std::string& root) {
    std::string p = root;
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    if (p.empty()) return "/";
    if (p[0] != '/') p = "/" + p;
    return p;
}

DirMap loadIndex(const std::string& path, uint64_t& updated, std::string& root) {
    DirMap dirs;
    updated = 0;
    root.clear();
    std::string text;
    if (!readFile(path, text) || text.empty()) return dirs;
    JsonValue rootValue = parseJson(text);
    if (!rootValue.isObject()) return dirs;
    // version 2 起索引里带封面图信息；旧索引缺这一段，直接丢弃重扫一次，
    // 否则目录 mtime 没变会一直复用没有封面的旧记录。
    if (const JsonValue* ver = rootValue.find("version")) {
        if (ver->asNumber() < 2) return dirs;
    } else {
        return dirs;
    }
    if (const JsonValue* upd = rootValue.find("updated")) {
        updated = static_cast<uint64_t>(upd->asNumber());
    }
    // 旧索引没有 root 字段：按共享根建库处理。
    if (const JsonValue* r = rootValue.find("root")) {
        if (r->isString()) root = r->asString();
    }
    if (root.empty()) root = "/";
    const JsonValue* dirsValue = rootValue.find("dirs");
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
        if (const JsonValue* images = node.find("images")) {
            for (const auto& item : images->asArray()) {
                out.imageFiles.push_back(item.asString());
            }
        }
        if (const JsonValue* poster = node.find("poster")) {
            if (poster->isString()) out.poster = poster->asString();
        }
        if (const JsonValue* subs = node.find("subs")) {
            for (const auto& sub : subs->asArray()) out.subs.push_back(sub.asString());
        }
        dirs[kv.first] = std::move(out);
    }
    return dirs;
}

std::string dumpIndex(const DirMap& dirs, uint64_t updated, const std::string& root) {
    JsonValue index = JsonValue::object();
    index.setMember("version", JsonValue::number(2));
    index.setMember("root", JsonValue::string(root));
    index.setMember("updated", JsonValue::number(static_cast<double>(updated)));
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
        std::vector<JsonValue> imageValues;
        imageValues.reserve(kv.second.imageFiles.size());
        for (const auto& image : kv.second.imageFiles) {
            imageValues.push_back(JsonValue::string(image));
        }
        node.setMember("images", JsonValue::array(std::move(imageValues)));
        node.setMember("poster", JsonValue::string(kv.second.poster));
        std::vector<JsonValue> subValues;
        subValues.reserve(kv.second.subs.size());
        for (const auto& sub : kv.second.subs) subValues.push_back(JsonValue::string(sub));
        node.setMember("subs", JsonValue::array(std::move(subValues)));
        dirsValue.setMember(kv.first, std::move(node));
    }
    index.setMember("dirs", std::move(dirsValue));
    return toJsonString(index);
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

    const std::string scanRoot = normalizeRoot(root);
    std::string path = indexPath(sourceId);
    uint64_t updated = 0;
    std::string cachedRoot;
    DirMap cached = loadIndex(path, updated, cachedRoot);
    // 扫描根目录变了：旧索引里是另一个目录（甚至整个共享）的内容，必须丢弃重扫，
    // 不能因为目录 mtime 没变就复用，否则选了子目录仍会看到整盘影片。
    if (cachedRoot != scanRoot) {
        brls::Logger::info("media scan source={} root changed [{}] -> [{}], drop cached index",
                           sourceId, cachedRoot, scanRoot);
        cached.clear();
    }

    DirMap out;
    struct Node {
        std::string path;
        int depth;
    };
    std::vector<Node> stack;
    stack.push_back({scanRoot, 0});
    std::set<std::string> visited;
    int scanned = 0;
    int skipped = 0;
    int files = 0;
    std::string rootListError;

    while (!stack.empty()) {
        Node node = stack.back();
        stack.pop_back();
        if (!visited.insert(node.path).second) continue;

        std::vector<SmbEntry> entries;
        uint32_t mtime = 0;
        std::string listError;
        bool listed = SmbManager::instance().listDirMeta(smbId, node.path, entries, mtime, listError);
        if (node.depth == 0) {
            // 根目录一条日志：能区分“目录打不开”和“打开了但里面没东西”。
            brls::Logger::info("media scan root [{}] listed={} entries={}", scanRoot, listed,
                               entries.size());
        }
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
            if (node.path == scanRoot && rootListError.empty()) rootListError = listError;
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
            } else if (isImageFile(entry.name)) {
                nodeOut.imageFiles.push_back(entry.name);
                if (nodeOut.poster.empty() && isPosterName(entry.name)) {
                    nodeOut.poster = entry.name;
                }
            }
        }
        // 没有标准封面名、目录里又只有一张图：当整目录封面用。
        if (nodeOut.poster.empty() && nodeOut.imageFiles.size() == 1) {
            nodeOut.poster = nodeOut.imageFiles.front();
        }
        files += static_cast<int>(nodeOut.files.size());
        out[node.path] = std::move(nodeOut);
        ++scanned;
        if (files >= maxFiles) break;
    }

    if (out.empty()) {
        // 根目录都打不开：把 libsmb2 的原始错误带出去，否则界面上只看到“没有视频”。
        error = rootListError.empty() ? ("SMB 扫描失败：" + scanRoot) : rootListError;
        brls::Logger::error("media scan source={} root=[{}] failed: {}", sourceId, scanRoot,
                            error);
        return 0;
    }

    uint64_t stamp = nowMs();
    std::string text = dumpIndex(out, stamp, scanRoot);
    {
        // 扫描可能来自多个页面，串行写盘并用临时文件保证原子性。
        std::lock_guard<std::mutex> lock(mutex_);
        nxkit::ensureDirectory(nxkit::dataPath("miniapp/media"));
        const std::string tmp = path + ".tmp";
        writeFile(tmp, text);
        std::remove(path.c_str());
        std::rename(tmp.c_str(), path.c_str());
    }
    brls::Logger::info("media scan source={} root=[{}] dirs={} skipped={} files={} bytes={}",
                       sourceId, scanRoot, scanned, skipped, files, text.size());
    return files;
}

std::vector<MediaEntry> MediaLibrary::page(const std::string& sourceId, const std::string& root,
                                           int offset, int limit, int& total,
                                           std::string& error) {
    total = 0;
    std::uint64_t updated = 0;
    std::string indexRoot;
    DirMap dirs = loadIndex(indexPath(sourceId), updated, indexRoot);
    if (dirs.empty()) {
        error = "媒体库为空，请先扫描";
        return {};
    }
    // 索引属于建库时的扫描根；用户换了子目录后旧索引（可能是整个共享）不能再用。
    if (normalizeRoot(root) != indexRoot) {
        error = "媒体库根目录已变更，等待重新扫描";
        return {};
    }
    std::vector<MediaEntry> all;
    for (const auto& kv : dirs) {
        // 同目录内按「去扩展名、小写」配对 sidecar 字幕。
        std::map<std::string, std::string> subtitles;
        for (const auto& subtitle : kv.second.subtitleFiles) {
            subtitles[stemKey(subtitle)] = subtitle;
        }
        // 同名图片优先（a.jpg 配 a.mkv），否则用目录封面。
        std::map<std::string, std::string> images;
        for (const auto& image : kv.second.imageFiles) {
            images[stemKey(image)] = image;
        }
        for (const auto& file : kv.second.files) {
            MediaEntry entry;
            entry.path = file.first;
            entry.size = file.second;
            auto match = subtitles.find(stemKey(file.first));
            if (match != subtitles.end()) entry.subtitle = match->second;
            auto cover = images.find(stemKey(file.first));
            entry.poster = cover != images.end() ? cover->second : kv.second.poster;
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
