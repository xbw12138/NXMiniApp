//
// miniapp: store-style launcher.
//

#include "miniapp/pages/launcher_view.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>

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

    brls::Button* refresh = new brls::Button();
    refresh->setText("刷新");
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
                this->statusLabel->setText(MiniAppApi::instance().serverUrl());
                this->grid->setError(error + "\n请确认商店服务已启动，或在 config.json 设置 miniapp.serverUrl");
                return;
            }

            this->items = list;
            this->statusLabel->setText(MiniAppApi::instance().serverUrl() + " · " +
                                       std::to_string(list.size()) + " 个小程序");
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
    brls::Application::pushActivity(new MiniappActivity(packages::directory(item.id)));
}

}  // namespace miniapp
