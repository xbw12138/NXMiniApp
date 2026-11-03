//
// miniapp: launcher activity.
//

#include "miniapp/pages/launcher_activity.hpp"

#include "miniapp/pages/launcher_view.hpp"

namespace miniapp {

brls::View* LauncherActivity::createContentView() { return new LauncherView(); }

}  // namespace miniapp
