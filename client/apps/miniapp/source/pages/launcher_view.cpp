//
// miniapp: store-style launcher.
//

#include "miniapp/pages/launcher_view.hpp"

#include <nlohmann/json.hpp>

#include <borealis/core/thread.hpp>
#include <borealis/views/dialog.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "miniapp/install/package_manager.hpp"
#include "miniapp/miniapp_activity.hpp"
#include "miniapp/miniapp_view.hpp"
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

// 图标字形 Label：直接使用 Material Icons 字体渲染。
// 经主字体回退渲染时，垂直居中按主字体度量计算，无下降部的图标字形会被顶偏；
// 字体与度量同源后，图标才能在按钮里真正居中。
class IconGlyph : public brls::Label {
public:
    IconGlyph(const char* glyph, float size) {
        this->font = brls::Application::getFont(brls::FONT_MATERIAL_ICONS);
        this->setFontSize(size);
        this->setText(glyph);
        this->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        this->setVerticalAlign(brls::VerticalAlign::CENTER);
    }
};

// 图标按钮：默认按钮外观（灰底圆角）内嵌居中的 Material Icons 字形。
brls::Box* makeIconButton(const char* glyph, std::function<bool(brls::View*)> onClick) {
    brls::Theme theme = brls::Application::getTheme();
    brls::Style style = brls::Application::getStyle();

    brls::Box* button = new brls::Box(brls::Axis::ROW);
    button->setFocusable(true);
    button->setJustifyContent(brls::JustifyContent::CENTER);
    button->setAlignItems(brls::AlignItems::CENTER);
    button->setPadding(9, 14, 9, 14);
    button->setCornerRadius(style["brls/button/corner_radius"]);
    button->setHighlightCornerRadius(style["brls/button/corner_radius"]);
    button->setBackgroundColor(theme["brls/button/default_enabled_background"]);
    button->setHideHighlightBackground(true);
    button->setShadowType(brls::ShadowType::GENERIC);

    IconGlyph* icon = new IconGlyph(glyph, 22);
    icon->setTextColor(theme["brls/button/default_enabled_text"]);
    button->addView(icon);

    button->registerClickAction(std::move(onClick));
    button->addGestureRecognizer(new brls::TapGestureRecognizer(button));
    return button;
}

}  // namespace

LauncherView::LauncherView() {
    this->setAxis(brls::Axis::COLUMN);

    brls::Box* header = new brls::Box(brls::Axis::ROW);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setPadding(16, 28, 8, 28);

    brls::Label* title = new brls::Label();
    title->setText("Switch小程序");
    title->setFontSize(26);
    header->addView(title);

    brls::Box* spacer = new brls::Box();
    spacer->setGrow(1);
    header->addView(spacer);

    brls::Button* search = new brls::Button();
    this->searchBtn = search;
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

    // 图标按钮：字形为 Material Icons 码点，直接用图标字体渲染保证居中。
    brls::Box* refresh = makeIconButton("\xee\x97\x95", [this](brls::View*) {  // refresh
        this->loaded = false;
        this->reload();
        return true;
    });
    refresh->setMarginLeft(8);
    header->addView(refresh);

    brls::Box* about = makeIconButton("\xee\xa2\x8f", [this](brls::View*) {  // info_outline
        this->showAbout();
        return true;
    });
    about->setMarginLeft(8);
    header->addView(about);

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
    // 首页小程序已内嵌：保持其运行状态，返回时不重建、不回落商店网格。
    if (this->homeView) return;
    if (!this->loaded) {
        this->reload();
    } else {
        this->applyItems();
    }
}

void LauncherView::showAbout() {
    brls::Dialog* dialog = new brls::Dialog(
        "Switch小程序\n服务器：" + MiniAppApi::instance().serverUrl() +
        "\n设备 ID：" + MiniAppApi::instance().deviceId() +
        "\n源码：github.com/xbw12138/NXMiniApp");
    dialog->addButton("知道了", [] {});
    dialog->open();
}

void LauncherView::reload() {
    if (this->loading) return;
    this->loading = true;

    // 已内嵌首页：刷新 = 重新拉取首页描述（可感知新版本），成功后重建内嵌视图；
    // 失败则保留当前内嵌视图，仅重建一次触发其内部数据重载。
    if (this->homeView) {
        MiniAppApi::instance().fetchHome(
            [this, alive = this->alive](const HomeInfo& info, const std::string& error) {
                brls::sync([this, alive, info, error] {
                    if (!*alive) return;
                    this->loading = false;
                    if (error.empty() && info.hasHome) {
                        this->openHome(info.home);
                    } else if (this->homeItem.id.empty() == false) {
                        this->embedHome(this->homeItem);
                    }
                });
            });
        return;
    }

    // 首页由服务端下发的小程序驱动：先用本地已安装的包立即渲染，
    // 避免每次启动都等 /api/v1/miniapp-home 的网络往返；
    // 随后在后台校验版本，有新版本再热更并重建。
    if (!this->homeLaunched) {
        std::string cachedId = nxkit::Config::instance().getString("miniapp.home.id", "");
        if (!cachedId.empty() && packages::installed(cachedId, "")) {
            MiniAppItem cached;
            cached.id = cachedId;
            cached.title = nxkit::Config::instance().getString("miniapp.home.title", "");
            cached.version = nxkit::Config::instance().getString("miniapp.home.version", "");
            this->openHome(cached);

            MiniAppApi::instance().fetchHome(
                [this, alive = this->alive](const HomeInfo& info, const std::string& error) {
                    brls::sync([this, alive, info, error] {
                        if (!*alive) return;
                        // 仅在磁盘上没有该版本时下载，避免重复重建。
                        if (error.empty() && info.hasHome &&
                            !packages::installed(info.home.id, info.home.version)) {
                            this->openHome(info.home);
                        }
                    });
                });
            return;
        }
    }

    // 首页由服务端下发的小程序驱动：先取首页描述，有则直接内嵌运行，否则回落商店列表。
    if (!this->homeLaunched) {
        this->grid->showSkeleton(6);

        MiniAppApi::instance().fetchHome(
            [this, alive = this->alive](const HomeInfo& info, const std::string& error) {
                brls::sync([this, alive, info, error] {
                    if (!*alive) return;
                    if (error.empty() && info.hasHome) {
                        this->openHome(info.home);
                        return;
                    }
                    this->fetchStoreList();
                });
            });
        return;
    }

    this->fetchStoreList();
}

void LauncherView::openHome(const MiniAppItem& home) {
    this->homeLaunched = true;

    if (packages::installed(home.id, home.version)) {
        this->loading = false;
        this->loaded = true;
        this->embedHome(home);
        return;
    }

    MiniAppApi::instance().download(
        home.id, [this, alive = this->alive, home](const std::string& archive,
                                                   const std::string& error) {
            brls::sync([this, alive, home, archive, error] {
                if (!*alive) return;
                std::string installError;
                if (!error.empty() ||
                    !packages::install(home.id, home.version, archive, installError)) {
                    // 首页包装不上时退回商店列表，避免卡在加载态。
                    brls::Logger::warning("miniapp: home install failed: {}",
                                          error.empty() ? installError : error);
                    this->fetchStoreList();
                    return;
                }
                this->loading = false;
                this->loaded = true;
                this->embedHome(home);
            });
        });
}

void LauncherView::embedHome(const MiniAppItem& home) {
    this->homeLaunched = true;
    this->loaded = true;
    this->homeItem = home;

    // 记住首页小程序，下次启动可直接用本地包渲染。
    nxkit::Config::instance().setString("miniapp.home.id", home.id);
    nxkit::Config::instance().setString("miniapp.home.version", home.version);
    nxkit::Config::instance().setString("miniapp.home.title", home.title);
    nxkit::Config::instance().save();
    // 首页自带搜索入口，原生搜索/清除按钮只服务于商店网格。
    if (this->searchBtn) this->searchBtn->setVisibility(brls::Visibility::GONE);
    if (this->clearSearchBtn) this->clearSearchBtn->setVisibility(brls::Visibility::GONE);

    if (this->homeView) {
        this->removeView(this->homeView);
        this->homeView = nullptr;
    }

    MiniappView* view = new MiniappView(packages::directory(home.id));
    view->setGrow(1);
    this->addView(view);
    this->homeView = view;
    brls::Application::giveFocus(view->getDefaultFocus());

    // 网格在内嵌模式下不再需要，延迟移除避免打断当前焦点。
    if (this->grid) {
        nxkit::CoverGridPage* stale = this->grid;
        this->grid = nullptr;
        this->removeView(stale);
    }
}

void LauncherView::fetchStoreList() {
    this->loading = true;
    this->grid->showSkeleton(6);

    MiniAppApi::instance().fetchList([this, alive = this->alive](const std::vector<MiniAppItem>& list,
                                                                const std::string& error) {
        brls::sync([this, alive, list, error] {
            if (!*alive) return;
            this->loading = false;
            this->loaded = true;

            if (!error.empty()) {
                this->grid->setError(error + "\n请检查网络后重试");
                return;
            }

            this->items = list;
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
    if (!this->grid) return;
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
    // 缓存标题，供首页小程序通过 wx.miniapp.listRecent 展示最近使用。
    nxkit::Config::instance().setValue("miniapp.title." + item.id, item.title);
    nxkit::Config::instance().save();
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
