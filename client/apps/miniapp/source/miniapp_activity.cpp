//
// miniapp: 小程序容器活动。
//

#include "miniapp/miniapp_activity.hpp"

#include "miniapp/miniapp_view.hpp"

namespace miniapp {

MiniappActivity::MiniappActivity(std::string path) : packagePath(std::move(path)) {}

brls::View* MiniappActivity::createContentView() {
    return new MiniappView(this->packagePath);
}

}  // namespace miniapp
