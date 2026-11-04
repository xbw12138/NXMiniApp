//
// miniapp: store-style launcher.
//

#include "miniapp/pages/launcher_view.hpp"

#include <nlohmann/json.hpp>

#include <borealis/core/thread.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "miniapp/install/package_manager.hpp"
#include "miniapp/miniapp_activity.hpp"
#include "nxkit/config.hpp"

namespace miniapp {

namespace {

constexpr size_t kRecentLimit = 8;

std::string sizeOf(long long bytes) {
    if (bytes <= 0) return "";
    char buffer[32];
    if (bytes < 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%lld KB", bytes / 1024 + (bytes % 1024 ? 1 : 0));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", bytes / 1024.0 / 1024.0);
    }
    return buffer;
}

std::string subtitleOf(const MiniAppItem& item) {
    std::string subtitle = item.category.empty() ? "小程序" : item.category;
    if (!item.version.empty()) subtitle += " · v" + item.version;
    std::string size = sizeOf(item.size);
    if (!size.empty()) subtitle += " · " + size;
    return subtitle;
}

}  // namespace

LauncherView::LauncherView() {
    this->setAxis(brls::Axis::COLUMN);

    brls::Box* header = new brls::Box(brls::Axis::ROW);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setPadding(16, 28, 8, 28);

    brls::Box* heading = new brls::Box(brls::Axis::COLUMN);
    heading->setGrow(1);
    brls::Label* title = new brls::Label();
    title->setText("小程序");
    title->setFontSize(26);
    heading->addView(title);

    this->statusLabel = new brls::Label();
    this->statusLabel->setFontSize(13);
    this->statusLabel->setMarginTop(2);
    this->statusLabel->setTextColor(brls::Application::getTheme()["font/grey"]);
    heading->addView(this->statusLabel);
    header->addView(heading);

    brls::Button* search = new brls::Button();
    search->setText("搜索");
    search->setMarginLeft(12);
    search->registerClickAction([this](brls::View*) {
        this->promptSearch();
        return true;
    });
    header->addView(search);

    this->clearSearchBtn = new brls::Button();
    this->clearSearchBtn->setText("清除");
    this->clearSearchBtn->setMarginLeft(8);
    this->clearSearchBtn->setVisibility(brls::Visibility::GONE);
    this->clearSearchBtn->registerClickAction([this](brls::View*) {
        this->searchQuery.clear();
        this->clearSearchBtn->setVisibility(brls::Visibility::GONE);
        this->applyItems();
        return true;
    });
    header->addView(this->clearSearchBtn);

    brls::Button* refresh = new brls::Button();
    refresh->setText("刷新");
    refresh->setMarginLeft(8);
    refresh->registerClickAction([this](brls::View*) {
        this->loaded = false;
        this->reload();
        return true;
    });
    header->addView(refresh);

    this->addView(header);

    this->grid = new nxkit::CoverGridPage();
    this->grid->setGrow(1);
    this->grid->setOnSelect([this](const nxkit::CoverItem& item) {
        for (const auto& app : this->items) {
            if (app.id == item.id) {
                this->openItem(app);
                return;
            }
        }
    });
    this->grid->setRefreshAction([this] {
        this->loaded = false;
        this->reload();
    });
    this->addView(this->grid);
}

LauncherView::~LauncherView() { *this->alive = false; }

void LauncherView::willAppear(bool resetState) {
    brls::Box::willAppear(resetState);
    if (!this->loaded) {
        this->reload();
    } else {
        this->applyItems();
    }
}

void LauncherView::reload() {
    if (this->loading) return;
    this->loading = true;

    this->statusLabel->setText("正在连接商店…");
    this->grid->showSkeleton(6);

    MiniAppApi::instance().fetchList([this, alive = this->alive](const std::vector<MiniAppItem>& list,
                                                                const std::string& error) {
        brls::sync([this, alive, list, error] {
            if (!*alive) return;
            this->loading = false;
            this->loaded = true;

            if (!error.empty()) {
                this->statusLabel->setText("连接商店失败");
                this->grid->setError(error + "\n请检查网络后重试");
                return;
            }

            this->items = list;
            this->statusLabel->setText("共 " + std::to_string(list.size()) + " 个小程序");
            this->applyItems();
        });
    });
}

std::vector<std::string> LauncherView::recentIds() const {
    std::vector<std::string> result;
    const nlohmann::json& data = nxkit::Config::instance().data();
    if (!data.is_object()) return result;
    auto section = data.find("miniapp");
    if (section == data.end() || !section->is_object()) return result;
    auto recent = section->find("recent");
    if (recent == section->end() || !recent->is_array()) return result;
    for (const auto& entry : *recent) {
        if (entry.is_string()) result.push_back(entry.get<std::string>());
    }
    return result;
}

void LauncherView::touchRecent(const std::string& id) {
    std::vector<std::string> recent = this->recentIds();
    recent.erase(std::remove(recent.begin(), recent.end(), id), recent.end());
    recent.insert(recent.begin(), id);
    if (recent.size() > kRecentLimit) recent.resize(kRecentLimit);

    nlohmann::json updated = nlohmann::json::array();
    for (const auto& entry : recent) updated.push_back(entry);
    nxkit::Config::instance().setValue("miniapp.recent", updated);
    nxkit::Config::instance().save();
}

void LauncherView::applyItems() {
    if (this->items.empty()) {
        this->grid->setEmpty("商店里还没有小程序");
        return;
    }

    // 搜索模式：不按「最近使用」排序，全量过滤标题/id/分类/开发者。
    if (!this->searchQuery.empty()) {
        std::string query = this->searchQuery;
        std::transform(query.begin(), query.end(), query.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        std::vector<nxkit::CoverItem> covers;
        for (const auto& app : this->items) {
            std::string hay = app.title + " " + app.id + " " + app.category + " " + app.developer;
            std::transform(hay.begin(), hay.end(), hay.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (hay.find(query) == std::string::npos) continue;
            nxkit::CoverItem cover;
            cover.id = app.id;
            cover.cover = app.iconUrl;
            cover.title = app.title;
            cover.subtitle = subtitleOf(app);
            covers.push_back(std::move(cover));
        }
        if (covers.empty()) {
            this->grid->setEmpty("没有找到匹配「" + this->searchQuery + "」的小程序");
        } else {
            this->grid->setItems(covers);
        }
        this->statusLabel->setText("搜索「" + this->searchQuery + "」 · " +
                                   std::to_string(covers.size()) + " 个结果");
        return;
    }

    std::vector<std::string> recent = this->recentIds();
    std::vector<nxkit::CoverItem> covers;
    std::vector<std::string> added;

    auto append = [&](const MiniAppItem& app, const std::string& badge) {
        if (std::find(added.begin(), added.end(), app.id) != added.end()) return;
        added.push_back(app.id);
        nxkit::CoverItem cover;
        cover.id = app.id;
        cover.cover = app.iconUrl;
        cover.title = app.title;
        cover.subtitle = subtitleOf(app);
        int used = this->usageCount(app.id);
        if (used > 0) cover.subtitle += " · 用过" + std::to_string(used) + "次";
        cover.badge = badge;
        covers.push_back(std::move(cover));
    };

    for (const auto& id : recent) {
        for (const auto& app : this->items) {
            if (app.id == id) {
                append(app, "最近使用");
                break;
            }
        }
    }
    for (const auto& app : this->items) {
        append(app, packages::installed(app.id, app.version) ? "已安装" : "");
    }

    this->grid->setItems(covers);

#ifndef __SWITCH__
    // 崩溃复现自动驾驶：NXKIT_AUTOPILOT=1 时自动反复「打开首个小程序→返回」。
    static bool autopilot = std::getenv("NXKIT_AUTOPILOT") != nullptr;
    if (autopilot && !this->items.empty()) {
        MiniAppItem first = this->items.front();
        std::thread([this, first]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1200));
            brls::sync([this, first]() { this->openItem(first); });
        }).detach();
    }
#endif
}

void LauncherView::openItem(const MiniAppItem& item) {
    if (packages::installed(item.id, item.version)) {
        this->enter(item);
        return;
    }
    if (!this->downloadingId.empty()) {
        brls::Application::notify("正在下载 " + item.title + "，请稍候");
        return;
    }

    this->downloadingId = item.id;
    brls::Application::notify("正在下载 " + item.title);

    MiniAppApi::instance().download(
        item.id, [this, alive = this->alive, item](const std::string& archive, const std::string& error) {
            brls::sync([this, alive, item, archive, error] {
                if (!*alive) return;
                this->downloadingId.clear();

                if (!error.empty()) {
                    brls::Application::notify("下载失败: " + error);
                    return;
                }

                std::string installError;
                if (!packages::install(item.id, item.version, archive, installError)) {
                    brls::Application::notify("安装失败: " + installError);
                    return;
                }
                this->enter(item);
            });
        });
}

void LauncherView::enter(const MiniAppItem& item) {
    this->touchRecent(item.id);
    this->bumpUsage(item.id);
    brls::Application::pushActivity(new MiniappActivity(packages::directory(item.id)));
}

void LauncherView::bumpUsage(const std::string& id) {
    auto& config = nxkit::Config::instance();
    std::string base = "miniapp.usage." + id;
    config.setValue(base + ".count", config.getInt(base + ".count", 0) + 1);
    config.setValue(base + ".lastUsed",
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count());
    config.save();
}

int LauncherView::usageCount(const std::string& id) const {
    return nxkit::Config::instance().getInt("miniapp.usage." + id + ".count", 0);
}

void LauncherView::promptSearch() {
    brls::Application::getImeManager()->openForText(
        [this](std::string text) {
            this->searchQuery = text;
            this->clearSearchBtn->setVisibility(text.empty() ? brls::Visibility::GONE
                                                             : brls::Visibility::VISIBLE);
            this->applyItems();
        },
        "搜索小程序", "", 64, this->searchQuery);
}

}  // namespace miniapp
