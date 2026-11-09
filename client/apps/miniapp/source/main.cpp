//
// miniapp: 入口。
//

#include <borealis.hpp>

#include <cstdlib>
#include <exception>
#include <string>
#ifdef __SWITCH__
#include <unwind.h>
#include <cstdio>
#include <cstdint>
#endif
#ifdef __SWITCH__
#include <fstream>
#include <sys/stat.h>
#endif

#ifdef __SWITCH__
#include <malloc.h>
#include <switch.h>
#include <unistd.h>
#endif

#if defined(__APPLE__) && !defined(__SWITCH__)
#include <limits.h>
#include <mach-o/dyld.h>
#include <unistd.h>
#endif

#include "miniapp/api/miniapp_api.hpp"
#include "miniapp/miniapp_activity.hpp"
#include "miniapp/pages/launcher_activity.hpp"
#include "nxkit/app.hpp"
#include "nxkit/config.hpp"

namespace {

#ifdef __SWITCH__
// 诊断：探测 sbrk 最多还能扩多少（MB），用来判断真机上 newlib 堆到底有多大。
// 探测完立刻归还，不影响后续分配。
int probeHeapMB() {
    const long step = 4L << 20;
    long got = 0;
    while (got < (2048L << 20)) {
        if (sbrk(step) == reinterpret_cast<void*>(-1)) break;
        got += step;
    }
    if (got) sbrk(-got);
    return static_cast<int>(got >> 20);
}

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

#if defined(__APPLE__) && !defined(__SWITCH__)
// borealis 的资源路径是相对工作目录的（../resources/），直接双击 .app 时工作
// 目录是 /，字体等资源都会找不到。启动先切到可执行文件所在目录。
void setupWorkingDirectory() {
    char raw[PATH_MAX];
    uint32_t size = sizeof(raw);
    if (_NSGetExecutablePath(raw, &size) != 0) return;
    std::string path = raw;
    size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return;
    if (chdir(path.substr(0, slash).c_str()) != 0) {
        brls::Logger::warning("miniapp: chdir to executable dir failed");
    }
}
#else
void setupWorkingDirectory() {}
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

#ifdef __SWITCH__
// 诊断：terminate 时抓取调用栈地址，用来定位是哪一行触发了 __terminate。
struct TerminateTrace {
    std::uintptr_t ips[20];
    int count = 0;
};

_Unwind_Reason_Code collectTerminateFrame(_Unwind_Context* context, void* arg) {
    auto* trace = static_cast<TerminateTrace*>(arg);
    std::uintptr_t ip = static_cast<std::uintptr_t>(_Unwind_GetIP(context));
    if (ip != 0 && trace->count < 20) trace->ips[trace->count++] = ip;
    return trace->count < 20 ? _URC_NO_REASON : _URC_END_OF_STACK;
}
#endif

}  // namespace

int main(int, char**) {
    setupWorkingDirectory();
    setupFileLog();
    brls::Logger::info("miniapp: starting");
    // 诊断：未捕获异常会让 std::terminate 直接 abort，导致整个容器退出
    // （崩溃报告里只能看到 __appExit 卸载设备，看不到真正原因）。
    std::set_terminate([] {
        std::string message = "unknown";
        if (std::exception_ptr error = std::current_exception()) {
            try {
                std::rethrow_exception(error);
            } catch (const std::exception& e) {
                message = e.what() ? e.what() : "std exception";
            } catch (...) {
                message = "non-std exception";
            }
        }
        brls::Logger::error("miniapp: terminate: {}", message);
#ifdef __SWITCH__
        TerminateTrace trace;
        _Unwind_Backtrace(collectTerminateFrame, &trace);
        std::string frames;
        char buffer[24];
        for (int i = 0; i < trace.count; ++i) {
            std::snprintf(buffer, sizeof(buffer), " 0x%llx",
                          static_cast<unsigned long long>(trace.ips[i]));
            frames += buffer;
        }
        brls::Logger::error("miniapp: terminate base=0x{:x} frames:{}",
                            static_cast<unsigned long long>(
                                reinterpret_cast<std::uintptr_t>(&main)),
                            frames);
#endif
        std::abort();
    });
    std::atexit([] { brls::Logger::error("miniapp: atexit invoked"); });
#ifdef __SWITCH__
    // 诊断：打印进程内存池与 newlib 堆余量（排查真机 malloc 失败）。
    {
        u64 total = 0, used = 0;
        svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
        svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
        struct mallinfo mi = mallinfo();
        brls::Logger::info("miniapp: mem total={}MB used={}MB arena={}MB free={}MB sbrkAvail={}MB",
                           total / 1048576, used / 1048576, mi.arena / 1048576,
                           mi.fordblks / 1048576, probeHeapMB());
    }
#endif
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
            // 首页小程序用相对路径请求服务端，预览时也要先配置商店地址。
            miniapp::MiniAppApi::instance().configure(serverUrl());
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
