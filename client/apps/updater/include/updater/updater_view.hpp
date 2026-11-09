#pragma once

#include <borealis.hpp>

#include <memory>
#include <vector>

#include "updater/updater_service.hpp"

namespace updater {

/// 更新工具主界面：列出版本、下载替换、上传日志。
class UpdaterView : public brls::Box {
public:
    UpdaterView();
    ~UpdaterView() override;

private:
    void loadReleases();
    void rebuildRows();
    void install(const ReleaseInfo& release);
    void uploadContainerLog();
    void setStatus(const std::string& text);
    void setBusy(bool value);

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::vector<ReleaseInfo> releases;
    long long installedSize = -1;
    bool busy = false;

    brls::Label* statusLabel = nullptr;
    brls::Label* footerLabel = nullptr;
    brls::Box* rowBox = nullptr;
    brls::Button* refreshButton = nullptr;
    brls::Button* uploadButton = nullptr;
};

}  // namespace updater
