//
// miniapp: 入口。
//

#include <borealis.hpp>

#include <cstdlib>
#include <string>

#include "miniapp/api/miniapp_api.hpp"
#include "miniapp/miniapp_activity.hpp"
#include "miniapp/pages/launcher_activity.hpp"
#include "nxkit/app.hpp"
#include "nxkit/config.hpp"

namespace {

std::string serverUrl() {
    const char* env = std::getenv("NXKIT_MINIAPP_SERVER");
    if (env && *env) return env;

    auto& config = nxkit::Config::instance();
    std::string value = config.getString("miniapp.serverUrl", "");
    if (value.empty()) value = config.getString("store.serverUrl", "");
#ifndef __SWITCH__
    if (value.empty()) value = "http://127.0.0.1:8080";
#endif
    return value;
}

// 容器统一使用深色主题，贴近 Switch 系统观感。
void applyDarkTheme() {
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
}

}  // namespace

int main(int, char**) {
    nxkit::AppConfig config;
    config.title = "NX MiniApp";
    config.lightAccent = nvgRGB(15, 220, 120);
    config.darkAccent = nvgRGB(15, 220, 120);
#ifndef __SWITCH__
    // 桌面开发时绕过商店，直接预览本地包目录。
    if (const char* devPackage = std::getenv("NXKIT_MINIAPP_DEV")) {
        std::string package = devPackage;
        config.createActivity = [package] {
            applyDarkTheme();
            return new miniapp::MiniappActivity(package);
        };
        return nxkit::runApp(config);
    }
#endif
    config.createActivity = [] {
        applyDarkTheme();
        miniapp::MiniAppApi::instance().configure(serverUrl());
        return new miniapp::LauncherActivity();
    };
    return nxkit::runApp(config);
}
