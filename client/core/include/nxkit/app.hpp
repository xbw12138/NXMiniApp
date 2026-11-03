//
// GF: application bootstrap shared by every nxkit app.
//

#pragma once

#include <borealis.hpp>

#include <functional>
#include <string>

namespace nxkit {

/// Everything an app has to provide to boot on top of the framework.
struct AppConfig {
    /// Window / applet title.
    std::string title = "NXKit";

    /// Empty means <data dir>/config.json.
    std::string configPath;

    NVGcolor lightAccent = nvgRGB(0, 122, 255);
    NVGcolor darkAccent = nvgRGB(0, 199, 255);

    /// Quit the whole app when the user presses the home button.
    bool globalQuit = false;

    /// Register extra XML views (called after the framework views).
    std::function<void()> registerViews;

    /// Creates the first activity. A filler frame is used when empty.
    std::function<brls::Activity*()> createActivity;
};

/// Initializes paths, config, curl, the thread pool and borealis, pushes the
/// first activity, then blocks until the user quits. Returns an exit code.
int runApp(const AppConfig& config);

}  // namespace nxkit
