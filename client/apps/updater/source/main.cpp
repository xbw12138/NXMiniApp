//
// updater: 入口。独立于容器的更新工具：拉取最新 NRO、替换容器、上传日志。
//

#include <borealis.hpp>

#include <fstream>
#include <string>

#ifdef __SWITCH__
#include <sys/stat.h>
#endif

#include "nxkit/app.hpp"
#include "updater/updater_view.hpp"

namespace {

#ifdef __SWITCH__
// 更新工具自己的日志，和容器日志分开，方便排查下载/替换失败。
void setupFileLog() {
    mkdir("sdmc:/switch/miniapp", 0777);
    static std::ofstream file("sdmc:/switch/miniapp/updater.log", std::ios::trunc);
    if (!file) return;
    brls::Logger::getLogEvent()->subscribe([](const std::string& line) {
        file << line << "\n";
        file.flush();
    });
}
#else
void setupFileLog() {}
#endif

}  // namespace

int main(int, char**) {
    setupFileLog();

    nxkit::AppConfig config;
    config.title = "Switch小程序更新工具";
    config.lightAccent = nvgRGB(15, 220, 120);
    config.darkAccent = nvgRGB(15, 220, 120);
    config.createActivity = [] {
        brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
        auto* activity = new brls::Activity(new updater::UpdaterView());
        return activity;
    };
    return nxkit::runApp(config);
}
