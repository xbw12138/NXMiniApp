#pragma once

#include <borealis.hpp>

#include <memory>
#include <string>
#include <vector>

#include "updater/updater_service.hpp"

namespace updater {

/// 原生 NRO 应用商店 + 容器更新两个分区。
class UpdaterView : public brls::Box {
public:
    UpdaterView();
    ~UpdaterView() override;

private:
    enum class Mode { Store, Releases };

    void setMode(Mode mode);
    void loadApps();
    void loadReleases();
    void rebuildStore();
    void rebuildCards();
    void rebuildCategories();
    void rebuildReleases();
    void showDetail(const StoreAppInfo& app);
    void openStoreActions(const StoreAppInfo& app);
    void installApp(const StoreAppInfo& app);
    void removeApp(const StoreAppInfo& app);
    void install(const ReleaseInfo& release);
    void uploadContainerLog();
    void setStatus(const std::string& text);
    void setBusy(bool value);

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    Mode mode = Mode::Store;
    std::vector<ReleaseInfo> releases;
    std::vector<StoreAppInfo> apps;
    std::string category = "全部";
    std::string selectedId;
    long long installedSize = -1;
    bool busy = false;

    brls::Label* statusLabel = nullptr;
    brls::Label* footerLabel = nullptr;

    brls::Box* storePane = nullptr;
    brls::Box* categoryRow = nullptr;
    brls::Box* gridBox = nullptr;
    brls::Box* releasePane = nullptr;
    brls::Box* releaseBox = nullptr;

    brls::Box* detailPanel = nullptr;
    brls::Image* detailIcon = nullptr;
    brls::Label* detailTitle = nullptr;
    brls::Label* detailMeta = nullptr;
    brls::Label* detailInfo = nullptr;
    brls::Label* detailBadge = nullptr;
    brls::Label* detailDesc = nullptr;
    brls::Button* actionButton = nullptr;
    brls::Button* removeButton = nullptr;

    brls::Button* refreshButton = nullptr;
    brls::Button* uploadButton = nullptr;
    brls::Button* storeTab = nullptr;
    brls::Button* releaseTab = nullptr;
};

}  // namespace updater
