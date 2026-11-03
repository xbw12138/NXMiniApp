//
// miniapp: launcher activity.
//

#pragma once

#include <borealis.hpp>

namespace miniapp {

class LauncherActivity : public brls::Activity {
public:
    brls::View* createContentView() override;
};

}  // namespace miniapp
