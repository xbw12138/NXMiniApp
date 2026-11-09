//
// GF: application bootstrap shared by every nxkit app.
//

#include "nxkit/app.hpp"

#include <cpr/async_wrapper.h>
#include <curl/curl.h>

#include <chrono>
#include <cstdlib>

#ifdef __SWITCH__
#include <malloc.h>
#endif

#include <borealis/views/applet_frame.hpp>

#include "nxkit/config.hpp"
#include "nxkit/paths.hpp"
#include "nxkit/ui/auto_tab_frame.hpp"
#include "nxkit/ui/cover_grid_page.hpp"
#include "nxkit/ui/qr_login_view.hpp"
#include "nxkit/ui/recycling_grid.hpp"
#include "nxkit/ui/settings_page.hpp"
#include "nxkit/ui/svg_image.hpp"
#include "nxkit/ui/text_box.hpp"
#include "nxkit/utils/thread_helper.hpp"

namespace nxkit {

static void registerFrameworkViews() {
    brls::Application::registerXMLView("SVGImage", SVGImage::create);
    brls::Application::registerXMLView("TextBox", TextBox::create);
    brls::Application::registerXMLView("AutoTabFrame", AutoTabFrame::create);
    brls::Application::registerXMLView("RecyclingGrid", RecyclingGrid::create);
    brls::Application::registerXMLView("CoverGridPage", CoverGridPage::create);
    brls::Application::registerXMLView("SettingsPage", SettingsPage::create);
    brls::Application::registerXMLView("QRLoginView", QRLoginView::create);
}

static void registerFrameworkTheme(const AppConfig& config) {
    brls::Theme& light = brls::Theme::getLightTheme();
    brls::Theme& dark = brls::Theme::getDarkTheme();

    light.addColor("brls/accent", config.lightAccent);
    dark.addColor("brls/accent", config.darkAccent);

    // Card / skeleton colors used by CoverCard and RecyclingGrid.
    light.addColor("color/grey_1", nvgRGB(245, 246, 247));
    dark.addColor("color/grey_1", nvgRGB(51, 52, 53));

    light.addColor("color/grey_3", nvgRGBA(200, 200, 200, 16));
    dark.addColor("color/grey_3", nvgRGBA(160, 160, 160, 160));

    // Grey text readable in both themes.
    light.addColor("font/grey", nvgRGB(148, 153, 160));
    dark.addColor("font/grey", nvgRGB(148, 153, 160));
}

static brls::Activity* createFallbackActivity(const AppConfig& config) {
    brls::Label* label = new brls::Label();
    label->setText(config.title);
    label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    label->setVerticalAlign(brls::VerticalAlign::CENTER);
    label->setGrow(1);

    brls::Box* box = new brls::Box(brls::Axis::COLUMN);
    box->setGrow(1);
    box->addView(label);
    return new brls::Activity(box);
}

int runApp(const AppConfig& config) {
    brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);

    ensureDataDirectory();
    Config::instance().load(config.configPath);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    cpr::async::startup(THREAD_POOL_MIN_THREAD_NUM, THREAD_POOL_MAX_THREAD_NUM,
                        std::chrono::milliseconds(5000));

    if (!brls::Application::init()) {
        brls::Logger::error("Unable to init Borealis application");
        cpr::async::cleanup();
        curl_global_cleanup();
        return EXIT_FAILURE;
    }

    brls::Application::createWindow(config.title);
    brls::Application::setGlobalQuit(config.globalQuit);

#ifdef __SWITCH__
    // 诊断：GL 初始化 + Mesa 编译着色器之后还剩多少堆（启动崩溃就发生在这附近）。
    {
        struct mallinfo mi = mallinfo();
        brls::Logger::info("miniapp: mem after init arena={}MB used={}MB free={}MB",
                           mi.arena / 1048576, mi.uordblks / 1048576,
                           mi.fordblks / 1048576);
    }
#endif

    registerFrameworkTheme(config);
    registerFrameworkViews();
    if (config.registerViews) config.registerViews();

    brls::Application::pushActivity(
        config.createActivity ? config.createActivity()
                              : createFallbackActivity(config));

    while (brls::Application::mainLoop()) {
    }

    cpr::async::cleanup();
    curl_global_cleanup();
    return EXIT_SUCCESS;
}

}  // namespace nxkit
