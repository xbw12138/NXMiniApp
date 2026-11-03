//
// miniapp: 小程序容器活动。
//

#pragma once

#include <borealis.hpp>

#include <string>

namespace miniapp {

class MiniappActivity : public brls::Activity {
public:
    explicit MiniappActivity(std::string packagePath = "");

    brls::View* createContentView() override;

private:
    std::string packagePath;
};

}  // namespace miniapp
