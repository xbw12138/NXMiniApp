//
// miniapp: store-style launcher.
//

#pragma once

#include <borealis.hpp>

#include <memory>
#include <string>
#include <vector>

#include "miniapp/api/miniapp_api.hpp"
#include "nxkit/ui/cover_grid_page.hpp"

namespace miniapp {

class LauncherView : public brls::Box {
public:
    LauncherView();
    ~LauncherView() override;

    void willAppear(bool resetState) override;

private:
    void reload();
    void applyItems();
    void openItem(const MiniAppItem& item);
    void enter(const MiniAppItem& item);
    std::vector<std::string> recentIds() const;
    void touchRecent(const std::string& id);

    brls::Label* statusLabel = nullptr;
    nxkit::CoverGridPage* grid = nullptr;

    std::vector<MiniAppItem> items;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    bool loaded = false;
    bool loading = false;
    std::string downloadingId;
};

}  // namespace miniapp
