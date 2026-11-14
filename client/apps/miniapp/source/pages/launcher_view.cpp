//
// miniapp: store-style launcher.
//

#include "miniapp/pages/launcher_view.hpp"

#include <nlohmann/json.hpp>

#include <borealis/core/application.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/views/dialog.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "miniapp/api/miniapp_api.hpp"
#include "miniapp/crash_reporter.hpp"
#include "miniapp/install/package_manager.hpp"
#include "miniapp/miniapp_activity.hpp"
#include "miniapp/miniapp_view.hpp"
#include "nxkit/config.hpp"

#ifdef BUILTIN_NSP
#include <nspmini.hpp>
#include <switch.h>
#endif

namespace miniapp {

namespace {

constexpr size_t kRecentLimit = 9;

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

// 语义化版本比较：逐段比较数字，缺省段按 0 处理。
bool versionNewer(const std::string& latest, const std::string& current) {
    auto parse = [](const std::string& value) {
        std::vector<int> parts;
        int number = 0;
        bool hasDigit = false;
        for (char c : value) {
            if (c >= '0' && c <= '9') {
                number = number * 10 + (c - '0');
                hasDigit = true;
            } else if (c == '.') {
                parts.push_back(hasDigit ? number : 0);
                number = 0;
                hasDigit = false;
            } else {
                break;
            }
        }
        parts.push_back(hasDigit ? number : 0);
        return parts;
    };
    std::vector<int> a = parse(latest);
    std::vector<int> b = parse(current);
    const size_t count = std::max(a.size(), b.size());
    for (size_t i = 0; i < count; ++i) {
        int x = i < a.size() ? a[i] : 0;
        int y = i < b.size() ? b[i] : 0;
        if (x != y) return x > y;
    }
    return false;
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
    this->header = header;
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setPadding(16, 28, 8, 28);

    brls::Label* title = new brls::Label();
    title->setText("Switch小程序");
    title->setFontSize(26);
    header->addView(title);

#ifdef NXMINIAPP_VERSION
    // 容器版本号，便于确认真机上跑的是哪一版。
    brls::Label* version = new brls::Label();
    version->setText("v" NXMINIAPP_VERSION);
    version->setFontSize(15);
    version->setMarginLeft(10);
    version->setMarginTop(8);
    version->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
    header->addView(version);
#endif

    brls::Box* spacer = new brls::Box();
    spacer->setGrow(1);
    header->addView(spacer);

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

#ifdef BUILTIN_NSP
    // 桌面图标：安装 NSP 转发器后可直接从桌面启动。
    brls::Button* desktop = new brls::Button();
    desktop->setText("装到桌面");
    desktop->setMarginLeft(8);
    desktop->registerClickAction([this](brls::View*) {
        this->installDesktopIcon();
        return true;
    });
    header->addView(desktop);
#endif

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

    // 首页按 B：弹确认框再退出到桌面（内嵌小程序根页面的 B 也走这里，
    // 由 MiniappView::onRequestExit 转发过来）。
    this->registerAction("退出", brls::BUTTON_B, [this](brls::View*) {
        this->showExitDialog();
        return true;
    });
    // 首页内嵌后原生标题栏隐藏，X 直接弹出关于信息，里面保留刷新/上传日志/装到桌面。
    this->registerAction("工具", brls::BUTTON_X, [this](brls::View*) {
        this->showAbout();
        return true;
    });
}

LauncherView::~LauncherView() { *this->alive = false; }

void LauncherView::willAppear(bool resetState) {
    brls::Box::willAppear(resetState);
    this->checkContainerUpdate();
    // 首页小程序已内嵌：保持其运行状态，返回时不重建、不回落商店网格。
    if (this->homeView) return;
    if (!this->loaded) {
        this->reload();
    } else {
        this->applyItems();
    }
}

void LauncherView::showAbout() {
    std::string title = "Switch小程序";
#ifdef NXMINIAPP_VERSION
    title += " v" NXMINIAPP_VERSION;
#endif
    brls::Dialog* dialog = new brls::Dialog(
        title + "\n服务器：" + MiniAppApi::instance().serverUrl() +
        "\n设备 ID：" + MiniAppApi::instance().deviceId() +
        "\n源码：github.com/xbw12138/NXMiniApp");
    dialog->addButton("刷新首页", [this] {
        this->loaded = false;
        this->reload();
    });
    dialog->addButton("上传日志", [] {
        auto& api = miniapp::MiniAppApi::instance();
        if (!api.ready()) {
            brls::Application::notify("未配置服务器地址");
            return;
        }
#ifdef NXMINIAPP_VERSION
        const std::string version = NXMINIAPP_VERSION;
#else
        const std::string version = "unknown";
#endif
        brls::Application::notify("正在上传日志…");
        miniapp::crash::reportCurrentLog(
            api.serverUrl(), version, api.deviceId(), "manual",
            [](bool ok, const std::string& message) {
                (void)ok;
                brls::Application::notify(message);
            });
    });
#ifdef BUILTIN_NSP
    dialog->addButton("添加到桌面", [this] { this->installDesktopIcon(); });
#endif
    dialog->open();
}

void LauncherView::showExitDialog() {
    brls::Dialog* dialog = new brls::Dialog("确定要退出到桌面吗？");
    dialog->addButton("取消", [] {});
    dialog->addButton("退出", [] { brls::Application::quit(); });
    dialog->open();
}

void LauncherView::checkContainerUpdate() {
    if (this->updateChecked) return;
    this->updateChecked = true;
#ifdef NXMINIAPP_VERSION
    const std::string current = NXMINIAPP_VERSION;
#else
    return;
#endif
    MiniAppApi::instance().fetchLatestRelease(
        [this, alive = this->alive, current](const LatestRelease& release, const std::string& error) {
            if (!error.empty() || release.version.empty() ||
                !versionNewer(release.version, current)) {
                return;
            }
            // 同一版本只提示一次，避免每次启动打断用户。
            auto& config = nxkit::Config::instance();
            if (config.getString("miniapp.notifiedRelease", "") == release.version) return;
            config.setString("miniapp.notifiedRelease", release.version);
            config.save();

            brls::sync([this, alive, release, current] {
                if (!*alive) return;
                std::string message =
                    "发现容器新版本 v" + release.version + "（当前 v" + current + "）";
                if (!release.notes.empty()) message += "\n" + release.notes;
                message += "\n\n请退出本程序，在桌面运行更新工具完成升级。";
                brls::Dialog* dialog = new brls::Dialog(message);
                dialog->addButton("知道了", [] {});
                dialog->open();
            });
        });
}

void LauncherView::installDesktopIcon() {
#ifdef BUILTIN_NSP
    brls::Dialog* dialog = new brls::Dialog(
        "将在桌面安装「Switch小程序」图标，之后可直接从桌面启动。\n安装完成会自动跳转，是否继续？");
    dialog->addButton("取消", []() {});
    dialog->addButton("安装", []() {
        brls::Application::blockInputs();
        mini::InstallSD("romfs:/nsp_forwarder.nsp");
        u64 titleId = mini::GetTitleID();
        appletRequestLaunchApplication(titleId, NULL);
    });
    dialog->open();
#endif
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

    if (this->homeView) {
        this->removeView(this->homeView);
        this->homeView = nullptr;
    }

    // 首页自带顶栏：隐藏原生标题栏，把整块 720p 高度让给页面，避免底部被裁。
    if (this->header) this->header->setVisibility(brls::Visibility::GONE);

    MiniappView* view = new MiniappView(packages::directory(home.id), home.title);
    view->setGrow(1);
    this->addView(view);
    this->homeView = view;
    miniapp::MiniAppApi::instance().reportOpen(home.id);
    // 内嵌模式下，小程序根页面按 B 转发为「退出到桌面」确认弹窗。
    view->onRequestExit = [this] { this->showExitDialog(); };
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
    // 目录已存在但版本不一致：这是热更新，文案上区分「下载」与「更新」。
    const bool updating = packages::installed(item.id, "");
    brls::Application::notify(updating ? "正在更新 " + item.title : "正在下载 " + item.title);

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
    miniapp::MiniAppApi::instance().reportOpen(item.id);
    // 缓存标题和图标，供首页小程序通过 wx.miniapp.listRecent 展示最近使用。
    nxkit::Config::instance().setValue("miniapp.title." + item.id, item.title);
    if (!item.iconUrl.empty()) {
        nxkit::Config::instance().setValue("miniapp.icon." + item.id, item.iconUrl);
    }
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

}  // namespace miniapp
