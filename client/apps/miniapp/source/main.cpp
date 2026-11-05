//
// miniapp: 入口。
//

#include <borealis.hpp>

#include <cstdlib>
#include <string>
#ifdef __SWITCH__
#include <fstream>
#include <sys/stat.h>
#endif

#include "miniapp/api/miniapp_api.hpp"
#include "miniapp/miniapp_activity.hpp"
#include "miniapp/pages/launcher_activity.hpp"
#include "nxkit/app.hpp"
#include "nxkit/config.hpp"

namespace {

#ifdef __SWITCH__
// Switch 上把日志落盘到 SD 卡：真机崩溃后可经 USB 拉取日志定位问题。
void setupFileLog() {
    // ofstream 不会自动建父目录，目录缺失时日志会静默丢失。
    mkdir("sdmc:/switch/miniapp", 0777);
    static std::ofstream file("sdmc:/switch/miniapp/miniapp.log", std::ios::trunc);
    if (!file) return;
    brls::Logger::getLogEvent()->subscribe([](const std::string& line) {
        file << line << "\n";
        file.flush();
    });
    brls::Logger::info("miniapp: file log ready");
}
#else
void setupFileLog() {}
#endif


std::string serverUrl() {
    const char* env = std::getenv("NXKIT_MINIAPP_SERVER");
    if (env && *env) return env;

    auto& config = nxkit::Config::instance();
    std::string value = config.getString("miniapp.serverUrl", "");
    if (value.empty()) value = config.getString("store.serverUrl", "");
    // 默认指向线上商店；本地调试可用 NXKIT_MINIAPP_SERVER 或 config.json 覆盖。
    if (value.empty()) value = "http://ecfun.cc";
    return value;
}

// 容器统一使用深色主题，贴近 Switch 系统观感。
void applyDarkTheme() {
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
}

}  // namespace

int main(int, char**) {
    setupFileLog();
    brls::Logger::info("miniapp: starting");
    nxkit::AppConfig config;
    config.title = "Switch小程序";
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
