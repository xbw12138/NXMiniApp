//
// updater: 原生 NRO 应用商店界面（卡片墙 + 详情面板）+ 容器更新。
//

#include "updater/updater_view.hpp"

#include <borealis/core/thread.hpp>

#include <pthread.h>

#include <cstdio>
#include <algorithm>
#include <new>
#include <string>

namespace updater {

namespace {

constexpr float kCardW = 282;
constexpr float kCardH = 232;
constexpr float kCardGap = 14;
constexpr int kColumns = 3;
constexpr float kDetailW = 340;

const NVGcolor kCardBg = nvgRGBA(27, 30, 36, 255);
const NVGcolor kCardBgAlt = nvgRGBA(22, 25, 31, 255);
const NVGcolor kTextDim = nvgRGBA(150, 158, 170, 255);
const NVGcolor kAccent = nvgRGBA(15, 220, 120, 255);
const NVGcolor kAccentText = nvgRGBA(8, 17, 12, 255);
const NVGcolor kGold = nvgRGBA(255, 207, 92, 255);
const NVGcolor kBlue = nvgRGBA(79, 195, 247, 255);

struct Task {
    std::function<void()> fn;
};

void* taskEntry(void* raw) {
    std::unique_ptr<Task> task(static_cast<Task*>(raw));
    try {
        task->fn();
    } catch (const std::exception& e) {
        brls::Logger::error("updater task: {}", e.what());
    } catch (...) {
        brls::Logger::error("updater task: unknown error");
    }
    return nullptr;
}

// 后台任务用 pthread 显式 DETACHED：std::thread 的临时对象析构会触发
// __terminate（容器上踩过这个坑），这里统一用 pthread。
void runAsync(std::function<void()> fn) {
    auto* task = new (std::nothrow) Task{std::move(fn)};
    if (!task) return;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    const int rc = pthread_create(&thread, &attr, taskEntry, task);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        delete task;
        brls::Logger::error("updater: pthread_create failed rc={}", rc);
    }
}

// 视图已销毁/应用已退出时不再往主线程投递回调，
// 否则 brls::sync 会访问已析构的 Application（表现为退出后再进崩溃）。
void postIfAlive(const std::shared_ptr<bool>& alive, std::function<void()> fn) {
    if (!alive || !*alive) return;
    brls::sync([alive, fn] {
        if (!*alive) return;
        fn();
    });
}

std::string formatSize(long long bytes) {
    if (bytes <= 0) return "-";
    char buffer[32];
    if (bytes >= 1024LL * 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f GB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    } else if (bytes >= 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB",
                      static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%lld KB", bytes / 1024);
    }
    return buffer;
}

std::string truncate(const std::string& text, std::size_t limit) {
    if (text.size() <= limit) return text;
    std::size_t cut = limit;
    // 避免把 UTF-8 多字节字符截断。
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut) + "…";
}

/// 商店卡片：图标 + 标题 + 分类/体积 + 状态角标。
class StoreCard : public brls::Box {
public:
    StoreCard(const StoreAppInfo& entry, const std::string& badge, NVGcolor badgeColor)
        : brls::Box(brls::Axis::COLUMN), info(entry) {
        this->setDimensions(kCardW, kCardH);
        this->setCornerRadius(14);
        this->setHighlightCornerRadius(14);
        this->setHighlightPadding(0);
        this->setBackgroundColor(kCardBg);
        this->setFocusable(true);
        this->setAlignItems(brls::AlignItems::CENTER);
        this->addGestureRecognizer(new brls::TapGestureRecognizer(this));

        auto* icon = new brls::Image();
        icon->setDimensions(84, 84);
        icon->setMarginTop(18);
        icon->setCornerRadius(18);
        icon->setScalingType(brls::ImageScalingType::FIT);
        icon->setBackgroundColor(nvgRGBA(38, 42, 51, 255));
        if (!info.iconPath.empty()) {
            icon->setImageFromFile(info.iconPath);
        } else {
            icon->setImageFromRes("pictures/cover-bg.png");
        }
        this->addView(icon);

        auto* title = new brls::Label();
        title->setText(info.title.empty() ? info.id : info.title);
        title->setFontSize(18);
        title->setSingleLine(true);
        title->setWidth(kCardW - 32);
        title->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        title->setMarginTop(14);
        this->addView(title);

        auto* meta = new brls::Label();
        std::string metaText = info.category.empty() ? "应用" : info.category;
        if (info.size > 0) metaText += "   ·   " + formatSize(info.size);
        meta->setText(metaText);
        meta->setFontSize(13);
        meta->setSingleLine(true);
        meta->setWidth(kCardW - 32);
        meta->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        meta->setTextColor(kTextDim);
        meta->setMarginTop(5);
        this->addView(meta);

        if (!badge.empty()) {
            auto* pill = new brls::Box(brls::Axis::ROW);
            pill->setCornerRadius(9);
            pill->setPadding(2, 10, 2, 10);
            pill->setMarginTop(10);
            pill->setBackgroundColor(nvgRGBAf(badgeColor.r, badgeColor.g, badgeColor.b, 0.14f));
            auto* label = new brls::Label();
            label->setText(badge);
            label->setFontSize(12);
            label->setTextColor(badgeColor);
            pill->addView(label);
            this->addView(pill);
        }
    }

    void setOnFocus(std::function<void()> cb) { this->onFocus = std::move(cb); }

    void onFocusGained() override {
        brls::Box::onFocusGained();
        if (this->onFocus) this->onFocus();
    }

    const StoreAppInfo& appInfo() const { return this->info; }

private:
    StoreAppInfo info;
    std::function<void()> onFocus;
};

// 分类胶囊按钮。
brls::Box* makePill(const std::string& text, bool active,
                    std::function<bool(brls::View*)> onClick) {
    auto* pill = new brls::Box(brls::Axis::ROW);
    pill->setFocusable(true);
    pill->setCornerRadius(15);
    pill->setHighlightCornerRadius(15);
    pill->setHighlightPadding(0);
    pill->setPadding(6, 16, 6, 16);
    pill->setBackgroundColor(active ? kAccent : nvgRGBA(38, 42, 51, 255));
    pill->addGestureRecognizer(new brls::TapGestureRecognizer(pill));

    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(14);
    label->setTextColor(active ? kAccentText : nvgRGBA(210, 216, 224, 255));
    pill->addView(label);

    pill->registerClickAction(std::move(onClick));
    return pill;
}

// 容器版本行。
brls::Box* makeReleaseRow(const ReleaseInfo& release, bool installed,
                          std::function<bool(brls::View*)> onClick) {
    auto* row = new brls::Box(brls::Axis::ROW);
    row->setFocusable(true);
    row->setCornerRadius(12);
    row->setHighlightCornerRadius(12);
    row->setHighlightPadding(0);
    row->setPadding(14, 18, 14, 18);
    row->setMarginBottom(10);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setBackgroundColor(kCardBg);
    row->addGestureRecognizer(new brls::TapGestureRecognizer(row));

    auto* version = new brls::Label();
    version->setText("容器 v" + release.version);
    version->setFontSize(19);
    row->addView(version);

    auto* size = new brls::Label();
    size->setText(formatSize(release.size));
    size->setFontSize(14);
    size->setTextColor(kTextDim);
    size->setMarginLeft(14);
    row->addView(size);

    auto* spacer = new brls::Box();
    spacer->setGrow(1);
    row->addView(spacer);

    if (installed) {
        auto* badge = new brls::Label();
        badge->setText("当前版本");
        badge->setFontSize(14);
        badge->setTextColor(kAccent);
        row->addView(badge);
    }

    row->registerClickAction(std::move(onClick));
    return row;
}

}  // namespace

UpdaterView::UpdaterView() : brls::Box(brls::Axis::COLUMN) {
    this->setPadding(20, 24, 16, 24);

    // ---------- 顶栏 ----------
    auto* header = new brls::Box(brls::Axis::ROW);
    header->setAlignItems(brls::AlignItems::CENTER);

    auto* titleBox = new brls::Box(brls::Axis::COLUMN);
    auto* title = new brls::Label();
    title->setText("应用商店");
    title->setFontSize(28);
    titleBox->addView(title);
    auto* subtitle = new brls::Label();
    subtitle->setText("SWITCH · NRO 应用市场");
    subtitle->setFontSize(12);
    subtitle->setTextColor(kTextDim);
    titleBox->addView(subtitle);
    header->addView(titleBox);

    auto* spacer = new brls::Box();
    spacer->setGrow(1);
    header->addView(spacer);

    this->storeTab = new brls::Button();
    this->storeTab->setText("应用商店");
    this->storeTab->registerClickAction([this](brls::View*) {
        this->setMode(Mode::Store);
        return true;
    });
    header->addView(this->storeTab);

    this->releaseTab = new brls::Button();
    this->releaseTab->setText("容器更新");
    this->releaseTab->setMarginLeft(8);
    this->releaseTab->registerClickAction([this](brls::View*) {
        this->setMode(Mode::Releases);
        return true;
    });
    header->addView(this->releaseTab);

    this->refreshButton = new brls::Button();
    this->refreshButton->setText("刷新");
    this->refreshButton->setMarginLeft(8);
    this->refreshButton->registerClickAction([this](brls::View*) {
        if (this->mode == Mode::Store) {
            this->loadApps();
        } else {
            this->loadReleases();
        }
        return true;
    });
    header->addView(this->refreshButton);

    this->uploadButton = new brls::Button();
    this->uploadButton->setText("上传容器日志");
    this->uploadButton->setMarginLeft(8);
    this->uploadButton->registerClickAction([this](brls::View*) {
        this->uploadContainerLog();
        return true;
    });
    header->addView(this->uploadButton);
    this->uploadButton->setVisibility(brls::Visibility::GONE);

    this->addView(header);

    this->statusLabel = new brls::Label();
    this->statusLabel->setText("正在读取应用商店…");
    this->statusLabel->setFontSize(14);
    this->statusLabel->setTextColor(kTextDim);
    this->statusLabel->setMarginTop(8);
    this->addView(this->statusLabel);

    // ---------- 内容区 ----------
    auto* content = new brls::Box(brls::Axis::ROW);
    content->setGrow(1);
    content->setMarginTop(12);

    // 应用商店：左侧分类 + 卡片墙，右侧详情面板。
    this->storePane = new brls::Box(brls::Axis::ROW);
    this->storePane->setGrow(1);

    auto* left = new brls::Box(brls::Axis::COLUMN);
    left->setGrow(1);

    this->categoryRow = new brls::Box(brls::Axis::ROW);
    this->categoryRow->setMarginBottom(12);
    left->addView(this->categoryRow);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1);
    this->gridBox = new brls::Box(brls::Axis::COLUMN);
    scroll->setContentView(this->gridBox);
    left->addView(scroll);
    this->storePane->addView(left);

    this->detailPanel = new brls::Box(brls::Axis::COLUMN);
    this->detailPanel->setWidth(kDetailW);
    this->detailPanel->setPadding(18, 18, 18, 18);
    this->detailPanel->setMarginLeft(16);
    this->detailPanel->setCornerRadius(16);
    this->detailPanel->setBackgroundColor(kCardBgAlt);
    this->detailPanel->setAlignItems(brls::AlignItems::CENTER);

    this->detailIcon = new brls::Image();
    this->detailIcon->setDimensions(112, 112);
    this->detailIcon->setCornerRadius(24);
    this->detailIcon->setScalingType(brls::ImageScalingType::FIT);
    this->detailIcon->setBackgroundColor(nvgRGBA(38, 42, 51, 255));
    this->detailIcon->setMarginTop(6);
    this->detailPanel->addView(this->detailIcon);

    this->detailTitle = new brls::Label();
    this->detailTitle->setFontSize(22);
    this->detailTitle->setWidth(kDetailW - 44);
    this->detailTitle->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    this->detailTitle->setMarginTop(14);
    this->detailPanel->addView(this->detailTitle);

    this->detailMeta = new brls::Label();
    this->detailMeta->setFontSize(13);
    this->detailMeta->setSingleLine(true);
    this->detailMeta->setWidth(kDetailW - 44);
    this->detailMeta->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    this->detailMeta->setTextColor(kTextDim);
    this->detailMeta->setMarginTop(6);
    this->detailPanel->addView(this->detailMeta);

    this->detailInfo = new brls::Label();
    this->detailInfo->setFontSize(13);
    this->detailInfo->setSingleLine(true);
    this->detailInfo->setWidth(kDetailW - 44);
    this->detailInfo->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    this->detailInfo->setTextColor(kTextDim);
    this->detailInfo->setMarginTop(4);
    this->detailPanel->addView(this->detailInfo);

    this->detailBadge = new brls::Label();
    this->detailBadge->setFontSize(13);
    this->detailBadge->setMarginTop(8);
    this->detailPanel->addView(this->detailBadge);

    this->detailDesc = new brls::Label();
    this->detailDesc->setFontSize(14);
    this->detailDesc->setWidth(kDetailW - 44);
    this->detailDesc->setMarginTop(14);
    this->detailDesc->setTextColor(nvgRGBA(196, 202, 212, 255));
    this->detailPanel->addView(this->detailDesc);

    auto* detailSpacer = new brls::Box();
    detailSpacer->setGrow(1);
    this->detailPanel->addView(detailSpacer);

    auto* actions = new brls::Box(brls::Axis::ROW);
    this->actionButton = new brls::Button();
    this->actionButton->setText("安装");
    this->actionButton->setGrow(1);
    this->actionButton->registerClickAction([this](brls::View*) {
        for (const auto& app : this->apps) {
            if (app.id == this->selectedId) {
                this->openStoreActions(app);
                break;
            }
        }
        return true;
    });
    actions->addView(this->actionButton);

    this->removeButton = new brls::Button();
    this->removeButton->setText("卸载");
    this->removeButton->setMarginLeft(8);
    this->removeButton->registerClickAction([this](brls::View*) {
        for (const auto& app : this->apps) {
            if (app.id == this->selectedId) {
                this->removeApp(app);
                break;
            }
        }
        return true;
    });
    actions->addView(this->removeButton);
    this->detailPanel->addView(actions);

    this->storePane->addView(this->detailPanel);

    content->addView(this->storePane);

    // 容器更新：版本列表。
    this->releasePane = new brls::Box(brls::Axis::COLUMN);
    this->releasePane->setGrow(1);
    this->releasePane->setVisibility(brls::Visibility::GONE);
    auto* releaseScroll = new brls::ScrollingFrame();
    releaseScroll->setGrow(1);
    this->releaseBox = new brls::Box(brls::Axis::COLUMN);
    releaseScroll->setContentView(this->releaseBox);
    this->releasePane->addView(releaseScroll);
    content->addView(this->releasePane);

    this->addView(content);

    this->footerLabel = new brls::Label();
    this->footerLabel->setFontSize(12);
    this->footerLabel->setMarginTop(10);
    this->footerLabel->setTextColor(kTextDim);
    this->footerLabel->setText("服务器：" + serverUrl());
    this->addView(this->footerLabel);
    brls::Logger::info("updater: view created, server={}", serverUrl());

    // 按 B 退出前先确认；后台任务还在跑时拒绝退出，否则线程会在
    // Application 销毁后回调，退出后再进就会崩。
    this->registerAction("退出", brls::BUTTON_B, [this](brls::View*) {
        if (this->busy) {
            this->setStatus("还有任务在执行，请等它结束再退出");
            return true;
        }
        auto* dialog = new brls::Dialog("确定要退出 Switch应用商店 吗？");
        dialog->addButton("取消", [] {});
        dialog->addButton("退出", [this] {
            *this->alive = false;
            brls::Application::quit();
        });
        dialog->setCancelable(true);
        dialog->open();
        return true;
    });

    this->loadApps();
}

UpdaterView::~UpdaterView() { *this->alive = false; }

void UpdaterView::setStatus(const std::string& text) {
    if (!this->statusLabel) return;
    // 兜底截断：异常信息过长时避免撑爆字体图集。
    constexpr std::size_t kMaxStatusChars = 96;
    if (text.size() > kMaxStatusChars) {
        this->statusLabel->setText(text.substr(0, kMaxStatusChars) + "…");
    } else {
        this->statusLabel->setText(text);
    }
}

void UpdaterView::setBusy(bool value) {
    this->busy = value;
    if (this->refreshButton) {
        this->refreshButton->setState(value ? brls::ButtonState::DISABLED
                                            : brls::ButtonState::ENABLED);
    }
    if (this->uploadButton) {
        this->uploadButton->setState(value ? brls::ButtonState::DISABLED
                                           : brls::ButtonState::ENABLED);
    }
    if (this->actionButton) {
        this->actionButton->setState(value ? brls::ButtonState::DISABLED
                                           : brls::ButtonState::ENABLED);
    }
    if (this->removeButton) {
        this->removeButton->setState(value ? brls::ButtonState::DISABLED
                                           : brls::ButtonState::ENABLED);
    }
}

void UpdaterView::setMode(Mode value) {
    this->mode = value;
    if (this->refreshButton) this->refreshButton->setText(value == Mode::Store ? "刷新" : "刷新版本");
    if (this->uploadButton) {
        this->uploadButton->setVisibility(value == Mode::Releases ? brls::Visibility::VISIBLE
                                                                  : brls::Visibility::GONE);
    }
    if (this->storePane) {
        this->storePane->setVisibility(value == Mode::Store ? brls::Visibility::VISIBLE
                                                            : brls::Visibility::GONE);
    }
    if (this->releasePane) {
        this->releasePane->setVisibility(value == Mode::Releases ? brls::Visibility::VISIBLE
                                                                 : brls::Visibility::GONE);
    }
    if (value == Mode::Store) {
        this->loadApps();
    } else {
        this->loadReleases();
    }
}

void UpdaterView::loadApps() {
    if (this->busy) return;
    this->setBusy(true);
    this->setStatus("正在读取应用商店…");
    auto alive = this->alive;
    runAsync([this, alive] {
        std::vector<StoreAppInfo> items;
        std::string error;
        const bool ok = fetchStoreApps(items, error);
        if (ok) {
            // 图标走本地缓存，首次下载后不再联网。
            for (auto& app : items) {
                if (!*alive) return;
                app.iconPath = fetchIcon(app.id, app.iconUrl);
            }
        }
        postIfAlive(alive, [this, ok, items, error] {
            this->setBusy(false);
            if (!ok) {
                this->apps.clear();
                this->setStatus(error);
                this->rebuildStore();
                return;
            }
            this->apps = items;
            this->setStatus("共 " + std::to_string(items.size()) +
                            " 款应用 · 选中后按 A 安装，装好的 NRO 在 /switch 下");
            this->rebuildStore();
        });
    });
}

void UpdaterView::rebuildStore() {
    if (!this->storePane) return;
    this->rebuildCategories();
    this->rebuildCards();

    if (this->selectedId.empty() && !this->apps.empty()) {
        this->selectedId = this->apps.front().id;
    }
    for (const auto& app : this->apps) {
        if (app.id == this->selectedId) {
            this->showDetail(app);
            break;
        }
    }
}

void UpdaterView::rebuildCategories() {
    if (!this->categoryRow) return;
    this->categoryRow->clearViews(true);

    std::vector<std::string> categories{"全部"};
    for (const auto& app : this->apps) {
        if (app.category.empty()) continue;
        if (std::find(categories.begin(), categories.end(), app.category) == categories.end()) {
            categories.push_back(app.category);
        }
    }
    if (std::find(categories.begin(), categories.end(), this->category) == categories.end()) {
        this->category = "全部";
    }
    for (const auto& name : categories) {
        auto* pill = makePill(name, name == this->category, [this, name](brls::View*) {
            this->category = name;
            this->rebuildStore();
            return true;
        });
        pill->setMarginRight(8);
        this->categoryRow->addView(pill);
    }
    this->categoryRow->invalidate();
}

void UpdaterView::rebuildCards() {
    if (!this->gridBox) return;
    this->gridBox->clearViews(true);

    std::vector<const StoreAppInfo*> filtered;
    for (const auto& app : this->apps) {
        if (this->category == "全部" || app.category == this->category) {
            filtered.push_back(&app);
        }
    }

    brls::View* firstCard = nullptr;
    brls::Box* row = nullptr;
    for (size_t i = 0; i < filtered.size(); ++i) {
        if (i % kColumns == 0) {
            row = new brls::Box(brls::Axis::ROW);
            row->setMarginBottom(kCardGap);
            this->gridBox->addView(row);
        }
        const StoreAppInfo& app = *filtered[i];
        std::string badge;
        NVGcolor badgeColor = kAccent;
        if (this->appOutdated(app)) {
            badge = "可更新";
            badgeColor = kBlue;
        } else if (this->appInstalled(app)) {
            badge = "已安装";
        } else if (app.featured) {
            badge = "精选";
            badgeColor = kGold;
        }

        auto* card = new StoreCard(app, badge, badgeColor);
        if (i % kColumns != 0) card->setMarginLeft(kCardGap);
        card->setOnFocus([this, app] { this->showDetail(app); });
        card->registerClickAction([this, app](brls::View*) {
            this->showDetail(app);
            this->openStoreActions(app);
            return true;
        });
        row->addView(card);
        if (!firstCard) firstCard = card;
    }

    if (filtered.empty()) {
        auto* empty = new brls::Label();
        empty->setText(this->apps.empty() ? "商店里还没有上架的应用" : "这个分类下暂无应用");
        empty->setFontSize(16);
        empty->setTextColor(kTextDim);
        empty->setMarginTop(20);
        this->gridBox->addView(empty);
    }

    this->gridBox->invalidate();
    if (firstCard) brls::Application::giveFocus(firstCard);
    brls::Logger::info("updater: store rebuilt ({}/{} apps, category={})", filtered.size(),
                       this->apps.size(), this->category);
}

bool UpdaterView::appInstalled(const StoreAppInfo& app) const {
    return !installedNroPath(app.id).empty();
}

bool UpdaterView::appOutdated(const StoreAppInfo& app) const {
    if (app.size <= 0) return false;
    const long long local = installedNroSize(app.id);
    return local > 0 && local != app.size;
}

void UpdaterView::showDetail(const StoreAppInfo& app) {
    if (!this->detailPanel) return;
    this->selectedId = app.id;

    if (!app.iconPath.empty()) {
        this->detailIcon->setImageFromFile(app.iconPath);
    } else {
        this->detailIcon->setImageFromRes("pictures/cover-bg.png");
    }
    this->detailTitle->setText(app.title.empty() ? app.id : app.title);

    std::string meta = app.developer.empty() ? (app.category.empty() ? "应用" : app.category)
                                             : app.developer;
    if (!app.developer.empty() && !app.category.empty()) meta += " · " + app.category;
    this->detailMeta->setText(meta);

    std::string info = app.version.empty() ? "版本未知" : "v" + app.version;
    if (app.size > 0) info += "   ·   " + formatSize(app.size);
    if (app.downloads > 0) info += "   ·   " + std::to_string(app.downloads) + " 次下载";
    this->detailInfo->setText(info);

    const bool installed = this->appInstalled(app);
    const std::string installedVersion = installedNroVersion(app.id);
    if (!installed) {
        this->detailBadge->setText(app.featured ? "★ 精选推荐" : "未安装");
        this->detailBadge->setTextColor(app.featured ? kGold : kTextDim);
        this->actionButton->setText("安装");
        this->removeButton->setVisibility(brls::Visibility::GONE);
    } else if (this->appOutdated(app)) {
        std::string from = installedVersion.empty() || installedVersion == "-"
                               ? formatSize(installedNroSize(app.id))
                               : "v" + installedVersion;
        this->detailBadge->setText("可更新：" + from + " → " +
                                   (app.version.empty() ? "最新" : "v" + app.version));
        this->detailBadge->setTextColor(kBlue);
        this->actionButton->setText("更新");
        this->removeButton->setVisibility(brls::Visibility::VISIBLE);
    } else {
        this->detailBadge->setText(installedVersion.empty() || installedVersion == "-"
                                       ? "已安装到 /switch"
                                       : "已安装 v" + installedVersion + "（已是最新）");
        this->detailBadge->setTextColor(kAccent);
        this->actionButton->setText("重新安装");
        this->removeButton->setVisibility(brls::Visibility::VISIBLE);
    }

    std::string desc = app.description.empty() ? app.summary : app.description;
    if (desc.empty()) desc = "暂无简介";
    this->detailDesc->setText(truncate(desc, 140));
}

void UpdaterView::openStoreActions(const StoreAppInfo& app) {
    if (this->busy) return;
    auto* dialog = new brls::Dialog(app.title.empty() ? app.id : app.title);
    if (!this->appInstalled(app)) {
        dialog->addButton("安装", [this, app] { this->installApp(app); });
    } else if (this->appOutdated(app)) {
        dialog->addButton("更新", [this, app] { this->installApp(app); });
        dialog->addButton("卸载", [this, app] { this->removeApp(app); });
    } else {
        dialog->addButton("重新安装", [this, app] { this->installApp(app); });
        dialog->addButton("卸载", [this, app] { this->removeApp(app); });
    }
    dialog->addButton("取消", [] {});
    dialog->setCancelable(true);
    dialog->open();
}

void UpdaterView::installApp(const StoreAppInfo& app) {
    if (this->busy) return;
    const std::string name = app.title.empty() ? app.id : app.title;
    this->setBusy(true);
    this->setStatus("开始下载 " + name + "…");
    auto alive = this->alive;
    runAsync([this, alive, app, name] {
        std::string error;
        const bool ok = installStoreApp(app, [this, alive, name](long long done, long long total) {
            if (total <= 0) return;
            const int percent = static_cast<int>(done * 100 / total);
            postIfAlive(alive, [this, name, percent] {
                this->setStatus("下载 " + name + " " + std::to_string(percent) + "%");
            });
        }, error);
        postIfAlive(alive, [this, ok, error, app, name] {
            this->setBusy(false);
            this->setStatus(ok ? "已安装 " + name + "，NRO 位于 /switch/" + app.id + ".nro"
                               : "安装失败：" + error);
            this->rebuildStore();
            if (ok) this->showDetail(app);
        });
    });
}

void UpdaterView::removeApp(const StoreAppInfo& app) {
    if (this->busy) return;
    const std::string name = app.title.empty() ? app.id : app.title;
    this->setBusy(true);
    this->setStatus("正在卸载 " + name + "…");
    auto alive = this->alive;
    runAsync([this, alive, app, name] {
        std::string error;
        const bool ok = removeStoreApp(app.id, error);
        postIfAlive(alive, [this, ok, error, app, name] {
            this->setBusy(false);
            this->setStatus(ok ? "已卸载 " + name : "卸载失败：" + error);
            this->rebuildStore();
            this->showDetail(app);
        });
    });
}

void UpdaterView::loadReleases() {
    if (this->busy) return;
    this->setBusy(true);
    this->setStatus("正在读取容器版本…");
    auto alive = this->alive;
    runAsync([this, alive] {
        std::vector<ReleaseInfo> items;
        std::string error;
        const bool ok = fetchReleases(items, error);
        const long long localSize = installedNroSize();
        postIfAlive(alive, [this, ok, items, error, localSize] {
            this->setBusy(false);
            if (!ok) {
                this->releases.clear();
                this->setStatus(error);
                this->rebuildReleases();
                return;
            }
            this->releases = items;
            this->installedSize = localSize;
            this->setStatus("共 " + std::to_string(items.size()) + " 个已发布版本，选中后按 A 安装");
            this->rebuildReleases();
        });
    });
}

void UpdaterView::rebuildReleases() {
    if (!this->releaseBox) return;
    this->releaseBox->clearViews(true);

    brls::View* firstRow = nullptr;
    for (const auto& release : this->releases) {
        const bool installed = this->installedSize > 0 && release.size == this->installedSize;
        auto* row = makeReleaseRow(release, installed, [this, release](brls::View*) {
            this->install(release);
            return true;
        });
        this->releaseBox->addView(row);
        if (!firstRow) firstRow = row;
    }

    if (this->releases.empty()) {
        auto* empty = new brls::Label();
        empty->setText("暂无已发布的容器版本");
        empty->setFontSize(16);
        empty->setTextColor(kTextDim);
        this->releaseBox->addView(empty);
    }

    this->releaseBox->invalidate();
    if (firstRow) brls::Application::giveFocus(firstRow);
}

void UpdaterView::install(const ReleaseInfo& release) {
    if (this->busy) return;
    this->setBusy(true);
    this->setStatus("开始下载 v" + release.version + "…");

    auto alive = this->alive;
    runAsync([this, alive, release] {
        std::string error;
        const bool ok = installRelease(release, [this, alive](long long done, long long total) {
            if (total <= 0) return;
            const int percent = static_cast<int>(done * 100 / total);
            postIfAlive(alive, [this, percent] {
                this->setStatus("下载中 " + std::to_string(percent) + "%");
            });
        }, error);
        postIfAlive(alive, [this, ok, error, release] {
            this->setBusy(false);
            if (!ok) {
                this->setStatus("安装失败：" + error);
                return;
            }
            this->installedSize = release.size;
            this->setStatus("已安装 v" + release.version + "，回 Homebrew 菜单重新启动容器即可生效");
            this->rebuildReleases();
        });
    });
}

void UpdaterView::uploadContainerLog() {
    if (this->busy) return;
    this->setBusy(true);
    this->setStatus("正在上传 miniapp.log…");

    auto alive = this->alive;
    runAsync([this, alive] {
        std::string error;
        const bool ok = uploadLog(error);
        postIfAlive(alive, [this, ok, error] {
            this->setBusy(false);
            this->setStatus(ok ? "日志已上传，可在管理后台「设备日志」查看" : "上传失败：" + error);
        });
    });
}

}  // namespace updater
