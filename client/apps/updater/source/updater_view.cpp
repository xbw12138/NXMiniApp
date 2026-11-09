//
// updater: 更新工具界面。
//

#include "updater/updater_view.hpp"

#include <borealis/core/thread.hpp>

#include <pthread.h>

#include <cstdio>
#include <new>
#include <string>

namespace updater {

namespace {

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

std::string releaseLabel(const ReleaseInfo& release) {
    char size[32];
    if (release.size >= 1024 * 1024) {
        std::snprintf(size, sizeof(size), "%.1f MB",
                      static_cast<double>(release.size) / (1024 * 1024));
    } else {
        std::snprintf(size, sizeof(size), "%lld KB", release.size / 1024);
    }
    std::string text = "v" + release.version + "   " + size;
    if (!release.notes.empty()) text += "   " + release.notes;
    return text;
}

}  // namespace

UpdaterView::UpdaterView() : brls::Box(brls::Axis::COLUMN) {
    this->setPadding(24, 28, 24, 28);

    auto* title = new brls::Label();
    title->setText("Switch小程序 · 更新工具");
    title->setFontSize(26);
    this->addView(title);

    this->statusLabel = new brls::Label();
    this->statusLabel->setText("正在读取容器版本…");
    this->statusLabel->setFontSize(16);
    this->statusLabel->setMarginTop(8);
    this->addView(this->statusLabel);

    auto* buttons = new brls::Box(brls::Axis::ROW);
    buttons->setMarginTop(16);

    this->refreshButton = new brls::Button();
    this->refreshButton->setText("刷新版本");
    this->refreshButton->registerClickAction([this](brls::View*) {
        this->loadReleases();
        return true;
    });
    buttons->addView(this->refreshButton);

    this->uploadButton = new brls::Button();
    this->uploadButton->setText("上传容器日志");
    this->uploadButton->setMarginLeft(12);
    this->uploadButton->registerClickAction([this](brls::View*) {
        this->uploadContainerLog();
        return true;
    });
    buttons->addView(this->uploadButton);

    this->addView(buttons);

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1);
    scroll->setMarginTop(16);

    this->rowBox = new brls::Box(brls::Axis::COLUMN);
    scroll->setContentView(this->rowBox);
    this->addView(scroll);

    this->footerLabel = new brls::Label();
    this->footerLabel->setFontSize(13);
    this->footerLabel->setMarginTop(12);
    this->footerLabel->setText("服务器：" + serverUrl() + "   设备：" + deviceId());
    this->addView(this->footerLabel);

    // 按 B 直接退出：更新工具没有返回栈，避免用户被困住。
    this->registerAction("退出", brls::BUTTON_B, [](brls::View*) {
        brls::Application::quit();
        return true;
    });

    this->loadReleases();
}

UpdaterView::~UpdaterView() { *this->alive = false; }

void UpdaterView::setStatus(const std::string& text) {
    if (this->statusLabel) this->statusLabel->setText(text);
}

void UpdaterView::setBusy(bool value) {
    this->busy = value;
    if (this->refreshButton) {
        this->refreshButton->setState(value ? brls::ButtonState::DISABLED : brls::ButtonState::ENABLED);
    }
    if (this->uploadButton) {
        this->uploadButton->setState(value ? brls::ButtonState::DISABLED : brls::ButtonState::ENABLED);
    }
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
        brls::sync([this, alive, ok, items, error, localSize] {
            if (!*alive) return;
            this->setBusy(false);
            if (!ok) {
                this->releases.clear();
                this->setStatus(error);
                this->rebuildRows();
                return;
            }
            this->releases = items;
            this->installedSize = localSize;
            this->setStatus("共 " + std::to_string(items.size()) + " 个已发布版本，选中后按 A 安装");
            this->rebuildRows();
        });
    });
}

void UpdaterView::rebuildRows() {
    if (!this->rowBox) return;
    this->rowBox->getChildren().clear();

    brls::View* firstButton = nullptr;
    for (const auto& release : this->releases) {
        auto* button = new brls::Button();
        std::string text = releaseLabel(release);
        if (this->installedSize > 0 && release.size == this->installedSize) {
            text += "   ·已安装";
        }
        button->setText(text);
        button->setMarginBottom(8);
        button->registerClickAction([this, release](brls::View*) {
            this->install(release);
            return true;
        });
        this->rowBox->addView(button);
        if (!firstButton) firstButton = button;
    }

    if (this->releases.empty()) {
        auto* empty = new brls::Label();
        empty->setText("暂无可用版本");
        empty->setFontSize(16);
        this->rowBox->addView(empty);
    }

    if (firstButton) brls::Application::giveFocus(firstButton);
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
            brls::sync([this, alive, percent] {
                if (!*alive) return;
                this->setStatus("下载中 " + std::to_string(percent) + "%");
            });
        }, error);
        brls::sync([this, alive, ok, error, release] {
            if (!*alive) return;
            this->setBusy(false);
            if (!ok) {
                this->setStatus("安装失败：" + error);
                return;
            }
            this->installedSize = release.size;
            this->setStatus("已安装 v" + release.version + "，回到 Homebrew 菜单重新启动容器即可生效");
            this->rebuildRows();
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
        brls::sync([this, alive, ok, error] {
            if (!*alive) return;
            this->setBusy(false);
            this->setStatus(ok ? "日志已上传，可在管理后台「设备日志」查看" : "上传失败：" + error);
        });
    });
}

}  // namespace updater
