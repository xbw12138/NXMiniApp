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

class MiniappView;

class LauncherView : public brls::Box {
public:
    LauncherView();
    ~LauncherView() override;

    void willAppear(bool resetState) override;

private:
    void reload();
    // 关于弹窗：版本/服务器/源码信息。
    void showAbout();
    // 把内置 NSP 转发器安装到桌面（home menu）作为图标入口。
    void installDesktopIcon();
    // 首页按 B 的退出确认弹窗。
    void showExitDialog();
    // 启动时优先拉起服务端配置的首页小程序；未配置或失败时回落到商店列表。
    void fetchStoreList();
    void openHome(const MiniAppItem& home);
    // 把首页小程序作为内容视图内嵌到标题栏下方（不再 push 独立页面）。
    void embedHome(const MiniAppItem& home);
    void applyItems();
    void openItem(const MiniAppItem& item);
    void enter(const MiniAppItem& item);
    std::vector<std::string> recentIds() const;
    void touchRecent(const std::string& id);
    // 使用统计：次数 + 最近使用时间，持久化在 config.json。
    void bumpUsage(const std::string& id);
    int usageCount(const std::string& id) const;
    // 搜索：弹出系统键盘，按标题/id/分类/开发者过滤。
    void promptSearch();

    brls::Button* clearSearchBtn = nullptr;
    brls::Button* searchBtn = nullptr;
    nxkit::CoverGridPage* grid = nullptr;
    // 内嵌模式下的首页小程序视图；非空表示首页已内嵌，不再使用商店网格。
    MiniappView* homeView = nullptr;
    MiniAppItem homeItem;

    std::vector<MiniAppItem> items;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    bool loaded = false;
    bool loading = false;
    bool homeLaunched = false;
    std::string downloadingId;
    std::string searchQuery;
};

}  // namespace miniapp
